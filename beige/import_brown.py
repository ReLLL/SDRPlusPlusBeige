#!/usr/bin/env python3
"""Copy a private Brown profile into Beige and relocate its installed plugins."""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import sqlite3
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def import_profile(source, destination, app, brown_app):
    source, destination, app = source.resolve(), destination.resolve(), app.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise SystemExit('Source and destination must be separate, non-nested profiles.')
    if not (source / 'config.json').is_file():
        raise SystemExit('Brown configuration not found.')
    if not (app / 'Contents/Frameworks/libsdrpp_core.dylib').is_file():
        raise SystemExit('Build and package Beige first.')
    if destination.exists():
        stamp = datetime.datetime.now().strftime('%Y%m%dT%H%M%S%f')
        destination.rename(destination.with_name(destination.name + '-before-import-' + stamp))
    destination.mkdir(parents=True)
    dx_source = pathlib.Path.home() / 'Library/Application Support/SDR++Brown DX Notebook'
    dx_target = destination / 'dx-notebook'
    cw_source = pathlib.Path.home() / 'Documents/SDR++Brown/CW Logs'
    original_config = json.loads((source / 'config.json').read_text())
    manifest = {'source_files': {}, 'relocated_binaries': [], 'database_counts': {}}
    for item in source.iterdir():
        if item.is_file() and (item.suffix == '.json' or item.suffix == '.TXT') and 'backup' not in item.name:
            shutil.copy2(item, destination / item.name)
            manifest['source_files'][item.name] = digest(item)
    for name in ('plugins', 'resources-oz-bandplan'):
        if (source / name).is_dir():
            shutil.copytree(source / name, destination / name)
    resources = pathlib.Path(original_config.get('resourcesDirectory', ''))
    if resources.is_dir() and not resources.is_relative_to(source):
        shutil.copytree(resources, destination / 'resources-imported')

    def relocate(value):
        if isinstance(value, str):
            return value.replace(str(source), str(destination)).replace(str(brown_app), str(app)).replace(
                str(dx_source), str(dx_target)).replace(str(cw_source), str(destination / 'cw-notebook'))
        if isinstance(value, list):
            return [relocate(item) for item in value]
        if isinstance(value, dict):
            return {relocate(key): relocate(item) for key, item in value.items()}
        return value

    for path in destination.rglob('*.json'):
        old = json.loads(path.read_text())
        new = relocate(old)
        if path == destination / 'config.json':
            new['spectrum3D'] = True
            if (destination / 'resources-imported').exists():
                new['resourcesDirectory'] = str(destination / 'resources-imported')
        if new != old:
            path.write_text(json.dumps(new, indent=4) + '\n')

    # Coherent read-only SQLite backup includes transactions still in Brown's WAL.
    if dx_source.is_dir():
        dx_target.mkdir()
        for item in dx_source.iterdir():
            if item.name == 'backups' or item.name.endswith(('-wal', '-shm')):
                continue
            target = dx_target / item.name
            if item.suffix in ('.sqlite3', '.sqlite', '.db'):
                with sqlite3.connect(item.as_uri() + '?mode=ro', uri=True) as old_db, sqlite3.connect(target) as new_db:
                    old_db.backup(new_db)
                    settings = list(new_db.execute('SELECT key, payload FROM settings'))
                    for key, payload in settings:
                        old_value = json.loads(payload)
                        new_value = relocate(old_value)
                        if new_value != old_value:
                            new_db.execute('UPDATE settings SET payload=? WHERE key=?', (json.dumps(new_value), key))
                    for (table,) in new_db.execute("SELECT name FROM sqlite_master WHERE type='table'"):
                        quoted = '"' + table.replace('"', '""') + '"'
                        manifest['database_counts'][table] = new_db.execute('SELECT COUNT(*) FROM ' + quoted).fetchone()[0]
                    assert new_db.execute('PRAGMA quick_check').fetchone()[0] == 'ok'
            elif item.is_dir():
                shutil.copytree(item, target)
            else:
                shutil.copy2(item, target)
    if cw_source.is_dir():
        shutil.copytree(cw_source, destination / 'cw-notebook')

    frameworks = app / 'Contents/Frameworks'
    for path in (destination / 'plugins').rglob('*'):
        if not path.is_file() or not (path.suffix == '.dylib' or path.stat().st_mode & 0o111):
            continue
        try:
            linked = run('otool', '-arch', 'arm64', '-L', str(path))
        except subprocess.CalledProcessError:
            continue
        old_hash = digest(path)
        needs_frameworks = False
        for line in linked.splitlines()[1:]:
            dependency = line.strip().split(' (compatibility')[0]
            name = pathlib.Path(dependency).name
            # Keep private Codec2/RADE/Fortran runtimes beside each plugin.
            if (path.parent / name).is_file() and name != path.name:
                replacement = '@loader_path/' + name
            elif (frameworks / name).is_file():
                replacement = '@rpath/' + name
                needs_frameworks = True
            else:
                continue
            if dependency != replacement:
                run('install_name_tool', '-change', dependency, replacement, str(path))
        commands = run('otool', '-arch', 'arm64', '-l', str(path))
        paths = re.findall(r'cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset', commands)
        for old_path in paths:
            run('install_name_tool', '-delete_rpath', old_path, str(path))
        new_paths = ['@loader_path']
        if needs_frameworks:
            new_paths.append(str(frameworks))
        for new_path in new_paths:
            run('install_name_tool', '-add_rpath', new_path, str(path))
        run('codesign', '--force', '--sign', '-', str(path))
        run('codesign', '--verify', '--strict', str(path))
        manifest['relocated_binaries'].append({'file': str(path.relative_to(destination)),
            'source_sha256': old_hash, 'beige_sha256': digest(path)})

    for path in (destination / 'plugins').rglob('manifest.json'):
        value = json.loads(path.read_text())
        if isinstance(value, dict) and all(isinstance(v, str) and len(v) == 64 for v in value.values()):
            for name in value:
                if (path.parent / name).is_file():
                    value[name] = digest(path.parent / name)
            path.write_text(json.dumps(value, indent=2) + '\n')
    copied = json.loads((destination / 'config.json').read_text())
    assert copied['moduleInstances'] == original_config['moduleInstances']
    assert copied['bandPlan'] == original_config['bandPlan']
    assert digest(source / 'frequency_manager_config.json') == digest(destination / 'frequency_manager_config.json')
    manifest['frequency_manager_sha256'] = digest(destination / 'frequency_manager_config.json')
    manifest['module_instances'] = len(copied['moduleInstances'])
    manifest['custom_modules'] = len(copied['modules'])
    (destination / 'beige-migration.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Imported {len(copied["moduleInstances"])} module settings, {len(copied["modules"])} custom plugins, '
          f'band plan and exact frequency-manager bytes into {destination}.')
    print('Database counts:', manifest['database_counts'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=pathlib.Path)
    parser.add_argument('app', type=pathlib.Path)
    parser.add_argument('--source', type=pathlib.Path,
        default=pathlib.Path.home() / 'Library/Application Support/sdrpp-brown')
    parser.add_argument('--brown-app', type=pathlib.Path, default=pathlib.Path('/Applications/SDR++Brown.app'))
    args = parser.parse_args()
    import_profile(args.source, args.destination, args.app, args.brown_app)

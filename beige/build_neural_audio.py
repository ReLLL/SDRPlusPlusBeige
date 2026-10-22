#!/usr/bin/env python3
"""Prepare pinned, private neural-audio dependencies for the local ARM64 app."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ORT_VERSION = '1.31.0'
ORT_SHA256 = '1031b36dd3bdaa8d976a6130d9cf2a05e9be320b93ae804f8427b91dc3432afc'
GTCRN_COMMIT = '502ebfab64da7c4a9af78dcb9c6ceef1ebb01c73'
MODEL_SHA256 = 'b4718df6228e7bdf1a8a435cf98f838636eb2fd331acabf86ba87c5192ebcb87'


def download(url, destination, digest):
    if not destination.is_file():
        temporary = destination.with_suffix(destination.suffix + '.partial')
        with urllib.request.urlopen(url, timeout=60) as source, temporary.open('wb') as target:
            shutil.copyfileobj(source, target)
        temporary.rename(destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Checksum mismatch: {destination.name}')


def prepare(build):
    work = build.resolve() / 'neural-dependency'
    work.mkdir(parents=True, exist_ok=True)
    name = f'onnxruntime-osx-arm64-{ORT_VERSION}'
    archive = work / f'{name}.tgz'
    download(f'https://github.com/microsoft/onnxruntime/releases/download/v{ORT_VERSION}/{name}.tgz', archive, ORT_SHA256)
    prefix = work / name
    if not (prefix / 'include/onnxruntime_cxx_api.h').exists():
        subprocess.run(['tar', '-xzf', str(archive), '-C', str(work)], check=True)
    model = work / 'gtcrn.onnx'
    upstream = f'https://raw.githubusercontent.com/Xiaobin-Rong/gtcrn/{GTCRN_COMMIT}'
    download(upstream + '/stream/onnx_models/gtcrn_simple.onnx', model, MODEL_SHA256)
    license_path = work / 'GTCRN-LICENSE'
    if not license_path.exists():
        with urllib.request.urlopen(upstream + '/LICENSE', timeout=30) as response:
            license_path.write_bytes(response.read())
    (work / 'provenance.json').write_text(json.dumps({
        'onnxruntime_version': ORT_VERSION, 'onnxruntime_archive_sha256': ORT_SHA256,
        'gtcrn_commit': GTCRN_COMMIT, 'gtcrn_model_sha256': MODEL_SHA256,
        'runtime': 'Native CPU execution provider; Apple FFT preferred with FFTW fallback',
    }, indent=2) + '\n')
    print(prefix)


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))

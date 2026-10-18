#!/usr/bin/env python3
"""Compare public Brown plugin object sizes/offsets against Beige."""
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
work = root / 'build-beige/abi'
reference = work / 'reference'
reference.mkdir(parents=True, exist_ok=True)
pin = '679f48deee5299a199d15d30e6e785fac6054896'
archive = subprocess.run(['git', 'archive', pin, 'core', 'decoder_modules/radio/src/radio_interface.h'], cwd=root, capture_output=True, check=True).stdout
subprocess.run(['tar', '-x', '-C', str(reference)], input=archive, check=True)
source = work / 'layout.cpp'
source.write_text(r'''
#include <cstdio>
#include <cstddef>
#include <gui/widgets/waterfall.h>
#include <gui/main_window.h>
#include <module.h>
#include <signal_path/source.h>
#include <signal_path/sink.h>
#include <radio_interface.h>
int main() {
    printf("%zu %zu %zu %zu %zu %zu %zu %zu %zu %zu %d %d %d\n",
        sizeof(ImGui::WaterFall), sizeof(ImGui::WaterfallVFO), sizeof(MainWindow),
        sizeof(ModuleManager::Instance), sizeof(SinkManager::Stream),
        offsetof(ImGui::WaterFall, vfos), offsetof(ImGui::WaterFall, selectedVFO),
        offsetof(ImGui::WaterFall, onFFTRedraw), offsetof(ImGui::WaterFall, fftAreaMin),
        offsetof(ImGui::WaterFall, afterWaterfallDraw), RADIO_IFACE_CMD_GET_SQUELCH_ENABLED,
        RADIO_IFACE_CMD_SET_SQUELCH_ENABLED, RADIO_IFACE_CMD_ADD_TO_AFCHAIN);
}
''')
results = {}
for label, tree in [('brown', reference), ('beige', root)]:
    binary = work / label
    subprocess.run(['clang++', '-std=c++17', '-Wno-invalid-offsetof',
        '-Wno-nontrivial-memcall', '-Wno-deprecated-declarations',
        '-I' + str(tree / 'core/src'), '-I' + str(tree / 'core/src/imgui'),
        '-I' + str(tree / 'decoder_modules/radio/src'),
        '-I/opt/homebrew/include', str(source), '-o', str(binary)], check=True)
    results[label] = subprocess.check_output([str(binary)], text=True).strip()
assert results['brown'] == results['beige'], results
(work / 'result.json').write_text(json.dumps(results, indent=2))
print('Brown/Beige public object sizes and WaterFall offsets match:', results['beige'])

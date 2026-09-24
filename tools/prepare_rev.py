#!/usr/bin/env python3
"""Stage the pinned Rev modules and embed their static resources in the build tree.

Rev's upstream File() uses paths into its development checkout. Only literal
resource calls are rewritten, preserving the framework API and enabling relocation.
"""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--platform', choices=('lnx', 'win', 'mac'), required=True)
args = parser.parse_args()
root = args.source.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
resources = {}
modules = []

def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')

def embed(match, source):
    ref = match.group(1)
    if ref == 'Rev/resources/Fonts/Arial/Arial.ttf':
        path = root / 'resources/DejaVuSans.ttf'
    elif ref.startswith('./'):
        path = source.parent / ref[2:]
    elif ref.startswith('Rev/'):
        path = root / ref[4:]
    else:
        raise ValueError(f'Unsupported resource path {ref} in {source}')
    path = path.resolve()
    if path not in resources:
        resources[path] = f'resource_{len(resources)}'
    symbol = resources[path]
    return ('::Rev::Core::Resource{::datapump_rev_resources::' + symbol +
            ', ::datapump_rev_resources::' + symbol + '_size}')

for source in sorted((root / 'source').rglob('*.ixx')):
    markers = source.name.lower().split('.')[1:-1]
    if any(marker in ('win','lnx','mac') and marker != args.platform for marker in markers):
        continue
    renderer = 'metal' if args.platform == 'mac' else 'opengl'
    if any(marker in ('opengl','metal','vulkan') and marker != renderer for marker in markers):
        continue
    text = source.read_text(encoding='utf-8')
    # Resource's explanatory File() examples are comments, not calls.
    if source.name != 'Resource.ixx':
        text = ''.join(line if line.lstrip().startswith('//') else re.sub(
            r'\bFile\(\s*"([^"]+)"\s*\)', lambda m: embed(m, source), line)
            for line in text.splitlines(keepends=True))
    text = text.replace('module;', 'module;\n#include "rev_embedded.hpp"', 1)
    target = output / source.relative_to(root)
    write_changed(target, text)
    modules.append(target.as_posix())

header = '#pragma once\n#include <cstddef>\nnamespace datapump_rev_resources {\n'
implementation = '#include "rev_embedded.hpp"\nnamespace datapump_rev_resources {\n'
for path, symbol in resources.items():
    data = path.read_bytes()
    header += f'extern const unsigned char {symbol}[];\ninline constexpr std::size_t {symbol}_size = {len(data)};\n'
    implementation += f'const unsigned char {symbol}[] = {{\n'
    for begin in range(0, len(data), 32):
        implementation += ','.join(str(b) for b in data[begin:begin+32]) + ',\n'
    implementation += '};\n'
header += '}\n'
implementation += '}\n'
write_changed(output / 'rev_embedded.hpp', header)
write_changed(output / 'rev_embedded.cpp', implementation)
write_changed(output / 'modules.cmake', 'set(DATAPUMP_REV_MODULES\n' + ''.join(f'  "{m}"\n' for m in modules) + ')\n')
print(f'Staged {len(modules)} Rev modules and embedded {len(resources)} resources')

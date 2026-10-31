"""Regenerate the EE C++ sources on macOS and check them against the frozen Windows inventory.

The qualified producer (scripts/ee_producer.py generate) accepts only the Windows clang-cl translator receipt. This
script is the macOS counterpart of that stage: it builds the frozen donor translator with the host clang++, runs it on
the ELF that `ee_producer.py prepare` reconstructed, and requires every generated file to match the frozen inventory
(external-recomp-references.json, `generated`). It reuses the producer's own receipt and inventory checks; there is no
permissive mode.

Usage (from the repository root, after `ee_producer.py prepare --output private/<fresh producer>`):
  private/tooling-venv/bin/python -E -s ports/macos/ee_generate.py private/<fresh producer>
"""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import ee_producer  # noqa: E402

TRANSLATOR_SOURCES = ('main.cpp', 'r5900.cpp', 'elf.cpp', 'analysis.cpp', 'codegen.cpp', 'snr2.cpp')


def generate(output):
    output, metadata, study = ee_producer.existing_producer(output)
    if (output / 'generated').exists() or (output / 'tool-build').exists() or (output / 'generation.json').exists():
        raise ValueError('Generation already attempted; preserve producer and choose a fresh identity')
    donor = Path(metadata['donor'])
    tool_build = output / 'tool-build'
    tool_build.mkdir()
    translator = tool_build / 'eerecomp'
    compiler = ['clang++', '-std=c++20', '-O2', '-o', str(translator)]
    compiler += [str(donor / 'recomp' / name) for name in TRANSLATOR_SOURCES]
    subprocess.run(compiler, check=True)
    version = subprocess.run(['clang++', '--version'], capture_output=True, text=True, check=True).stdout
    (tool_build / 'toolchain.txt').write_text('command=' + ' '.join(compiler) + '\n' + version, encoding='utf-8')
    arguments = [str(translator), str(output / 'SCUS_973.28.elf'), '--out', str(output / 'generated')]
    with (output / 'generation.log').open('xb') as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True)
    records = ee_producer.generated_inventory(output / 'generated')
    expected = study['generated']
    observed = ee_producer.inventory_summary(records)
    result = {'schema_version': 1, 'translator': ee_producer.identity(translator),
              'tool_origin': 'macOS host clang++ translator; receipt in tool-build/toolchain.txt',
              'arguments': arguments, 'generated': records, 'comparison': observed,
              'identical_to_frozen_generated': observed == expected,
              'qualification': 'Source identity only; not runtime or PS2 fidelity proof'}
    ee_producer.write_json(output / 'generation.json', result)
    if observed != expected:
        raise ValueError('Generated source differs from frozen producer; see private generation.json')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('output', type=Path, help='Prepared producer directory (immediate child of private/)')
    result = generate(parser.parse_args(argv).output)
    print(f"Verified {result['comparison']['file_count']} generated files against frozen provenance.")


if __name__ == '__main__':
    main()

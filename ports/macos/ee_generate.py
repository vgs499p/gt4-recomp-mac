"""Regenerate the EE C++ sources on macOS and check them against the frozen Windows inventory.

The qualified producer (scripts/ee_producer.py generate) accepts only the Windows clang-cl translator receipt. This
script is the macOS counterpart of that stage: it builds the frozen donor translator with the host clang++, runs it on
the ELF that `ee_producer.py prepare` reconstructed, and requires every generated file to match the frozen inventory
(external-recomp-references.json, `generated`). It reuses the producer's own receipt and inventory checks; there is no
permissive mode.

Profiles:
  production  the plain ELF, checked against the frozen inventory (external-recomp-references.json)
  aspect      the TASK-080 P2 derivative (scripts/aspect_elf_patch.py: race HUD constants become loads from the HUD
              parameter block) translated with the pinned accepted seed set, checked against the slice217 source
              inventory (docs/inputs/slice217-ee-libraries.json). These are the units of upstream's releases from 1.2.x,
              before scripts/camera360_units.py adds the hook sites.

Usage (from the repository root, after `ee_producer.py prepare --output private/<fresh producer>`):
  private/tooling-venv/bin/python -E -s ports/macos/ee_generate.py private/<fresh producer> [--profile aspect]
"""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import ee_producer  # noqa: E402

TRANSLATOR_SOURCES = ('main.cpp', 'r5900.cpp', 'elf.cpp', 'analysis.cpp', 'codegen.cpp', 'snr2.cpp')
ASPECT_MANIFEST = ROOT / 'docs' / 'inputs' / 'slice217-ee-libraries.json'


def build_translator(output, metadata):
    """The frozen donor translator, built with the host clang++ into <producer>/tool-build."""
    if (output / 'generated').exists() or (output / 'tool-build').exists():
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
    return translator


def generate_aspect(output):
    import aspect_elf_patch
    import ee_entries
    output, metadata, study = ee_producer.existing_producer(output)
    if (output / 'aspect-generation.json').exists():
        raise ValueError('Generation already attempted; preserve producer and choose a fresh identity')
    pin = ee_producer.read_json(ASPECT_MANIFEST)['profiles']['aspect']
    original = (output / 'SCUS_973.28.elf').read_bytes()
    if aspect_elf_patch.sha256(original) != aspect_elf_patch.ELF_SHA256:
        raise ValueError('reconstructed ELF differs from the pinned ELF')
    patched = aspect_elf_patch.patch_elf(original)
    if aspect_elf_patch.sha256(patched) != pin['patched_elf_sha256']:
        raise ValueError('patched ELF differs from the slice217 pin')
    patched_path = output / 'SCUS_973.28.aspect.elf'
    with patched_path.open('xb') as destination:
        destination.write(patched)
    seed_file, seeds, seed_pin = ee_entries.seed_set_pin(aspect_elf_patch.SEED_SET)
    if seed_pin['sha256'] != pin['seed_sha256']:
        raise ValueError('seed set differs from the one slice217 used')
    ee_entries.check_seeds_executable(patched, seeds)
    translator = build_translator(output, metadata)
    arguments = [str(translator), str(patched_path), '--out', str(output / 'generated'), '--stats', '--entries',
                 str(seed_file)]
    log_path = output / 'generation-aspect.log'
    with log_path.open('xb') as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1200)
    stats = ee_entries.parse_stats(log_path.read_text(encoding='utf-8', errors='replace'))
    ee_entries.validate_stats(stats, seed_pin)
    records = ee_producer.generated_inventory(output / 'generated')
    observed = ee_producer.inventory_summary(records)
    expected = pin['source_inventory']
    identical = (observed['file_count'], observed['inventory_sha256'], observed['size_bytes']) == (
        expected['file_count'], expected['inventory_sha256'], expected['size_bytes'])
    result = {'schema_version': 1, 'profile': 'aspect', 'translator': ee_producer.identity(translator),
              'tool_origin': 'macOS host clang++ translator; receipt in tool-build/toolchain.txt',
              'patched_elf': {'path': patched_path.name, 'sha256': aspect_elf_patch.sha256(patched)},
              'arguments': arguments, 'stats': stats, 'generated': records, 'comparison': observed,
              'identical_to_slice217_inventory': identical,
              'qualification': 'Source identity only; not runtime or PS2 fidelity proof'}
    ee_producer.write_json(output / 'aspect-generation.json', result)
    if not identical:
        raise ValueError(f'Generated source differs from the slice217 inventory: {observed} vs {expected}')
    return result


def generate(output):
    output, metadata, study = ee_producer.existing_producer(output)
    if (output / 'generation.json').exists():
        raise ValueError('Generation already attempted; preserve producer and choose a fresh identity')
    translator = build_translator(output, metadata)
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
    parser.add_argument('--profile', choices=('production', 'aspect'), default='production')
    args = parser.parse_args(argv)
    result = generate_aspect(args.output) if args.profile == 'aspect' else generate(args.output)
    print(f"Verified {result['comparison']['file_count']} generated files against frozen provenance.")


if __name__ == '__main__':
    main()

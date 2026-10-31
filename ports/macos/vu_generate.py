"""Regenerate the VU1 and VU0 AOT tables on macOS and check them against the release pins.

The release runtime (slice191 profile) compiles two generated tables into the runtime: the VU1 table of all 2,048
entries (docs/inputs/slice157-vu1-all.json) and the VU0 table of the 14 VCALLMS entries (slice160-vu0-aot.json). This
script rebuilds both from the reconstructed ELF of `ee_producer.py prepare`:

- VU1 image: the decrypted ELF blob (scripts/vu1_image.py decrypt_blob) plus the 16 tail bytes pinned in
  slice155-vu1-image.json. The Windows recipe takes the tail from recorded VU1 snapshots, which a fresh machine does
  not have; the pinned image hash (`vu1_image.py check`) then confirms the whole image.
- VU0 image: `vu0_image.py build`, unchanged.
- Entry lists from `vu1_image.py entries --policy all` and `vu0_image.py entries`, then the emitter
  (tools/vu_recomp, built with the host clang++) with the release options.

Every image, entry list, table and manifest hash must equal its pin; nothing is repinned.

Usage (from the repository root):
  private/tooling-venv/bin/python -E -s ports/macos/vu_generate.py private/<prepared producer> private/<fresh output>
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import vu1_image  # noqa: E402

PYTHON = [sys.executable, '-E', '-s']


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(path, expected, label):
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f'{label} differs from its pin: {actual}')


def run(arguments):
    subprocess.run([str(argument) for argument in arguments], check=True, cwd=ROOT)


def generate(producer, output):
    producer = producer.resolve()
    output = output.resolve()
    if output.exists():
        raise ValueError('Output already exists; choose a fresh directory')
    vu1_pin = json.loads((ROOT / 'docs/inputs/slice155-vu1-image.json').read_text(encoding='utf-8'))
    vu1_table_pin = json.loads((ROOT / 'docs/inputs/slice157-vu1-all.json').read_text(encoding='utf-8'))['vu1_aot']
    vu0_pin = json.loads((ROOT / 'docs/inputs/slice160-vu0-aot.json').read_text(encoding='utf-8'))['vu0_aot']
    elf = producer / 'SCUS_973.28.elf'
    require(elf, vu1_pin['elf']['sha256'], 'Reconstructed ELF')
    output.mkdir(parents=True)

    # VU1 image: decrypted blob plus the pinned tail, confirmed by the pinned image hash.
    elf_bytes = elf.read_bytes()
    blob = vu1_image.decrypt_blob(elf_bytes[vu1_image.ELF_OFFSET:vu1_image.ELF_OFFSET + vu1_image.BLOB_BYTES])
    if hashlib.sha256(blob).hexdigest() != vu1_pin['blob_sha256']:
        raise ValueError('Decrypted VU1 blob differs from its pin')
    vu1_dir = output / 'vu1-image'
    vu1_dir.mkdir()
    image = vu1_dir / 'vu1_image.bin'
    image.write_bytes(blob + bytes.fromhex(vu1_pin['tail_hex']))
    run(PYTHON + ['scripts/vu1_image.py', 'check', '--image', image])
    vu1_entries = vu1_dir / 'entries-all.txt'
    run(PYTHON + ['scripts/vu1_image.py', 'entries', '--image', image, '--policy', 'all', '--out', vu1_entries])
    require(vu1_entries, vu1_table_pin['entry_list_sha256'], 'VU1 entry list')

    # VU0 image and the 14 VCALLMS entries.
    vu0_dir = output / 'vu0-image'
    run(PYTHON + ['scripts/vu0_image.py', 'build', '--elf', elf, '--out', vu0_dir])
    require(vu0_dir / 'vu0_image.bin', vu0_pin['image_sha256'], 'VU0 image')
    vu0_entries = output / 'entries-vcallms.txt'
    run(PYTHON + ['scripts/vu0_image.py', 'entries', '--image', vu0_dir / 'vu0_image.bin', '--out', vu0_entries])
    require(vu0_entries, vu0_pin['entry_list_sha256'], 'VU0 entry list')

    # The emitter, built for this host.
    donor = ROOT / 'private/references/ps2-web-recomp'
    emitter = output / 'gt4_vu_recomp'
    source = ROOT / 'tools/vu_recomp/src'
    run(['clang++', '-std=c++20', '-O2', '-I', source, '-I', donor / 'runtime/include', '-I', donor / 'runtime/src',
         '-o', emitter, source / 'vurecomp_main.cpp', source / 'vurecomp.cpp', source / 'sha256.cpp'])

    vu1_generated = output / 'vu1-gen'
    run([emitter, '--programs-per-file', '16', '--entry-policy', 'all', '--image', image, '--entries', vu1_entries,
         vu1_generated])
    require(vu1_generated / 'vu1_generated.cpp', vu1_table_pin['generated_source_sha256'], 'VU1 table')
    require(vu1_generated / 'manifest.json', vu1_table_pin['generated_manifest_sha256'], 'VU1 manifest')

    vu0_generated = output / 'vu0-gen'
    run([emitter, '--unit', '0', '--namespace', 'gt4::vugen::vu0', '--entry-policy', 'vcallms',
         '--image', vu0_dir / 'vu0_image.bin', '--entries', vu0_entries, vu0_generated])
    require(vu0_generated / 'vu0_generated.cpp', vu0_pin['table_sha256'], 'VU0 table')
    require(vu0_generated / 'manifest.json', vu0_pin['manifest_sha256'], 'VU0 manifest')
    return vu1_generated, vu0_generated


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('producer', type=Path, help='Producer directory prepared by ee_producer.py prepare')
    parser.add_argument('output', type=Path, help='Fresh output directory')
    arguments = parser.parse_args(argv)
    vu1, vu0 = generate(arguments.producer, arguments.output)
    print(f'VU1 and VU0 tables match the release pins: {vu1} {vu0}')


if __name__ == '__main__':
    main()

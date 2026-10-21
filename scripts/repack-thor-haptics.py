#!/usr/bin/env python3
"""Repack kernel22's Thor DTB without rebuilding its Image or other DTBs."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess


def properties(blob: bytes) -> dict[tuple[str, str], bytes]:
    """Read every FDT property for an exact semantic comparison."""
    header = struct.unpack_from('>10I', blob)
    structure = blob[header[2]:header[2] + header[9]]
    strings = blob[header[3]:header[3] + header[8]]
    nodes: list[str] = []
    result: dict[tuple[str, str], bytes] = {}
    position = 0
    while position < len(structure):
        token = struct.unpack_from('>I', structure, position)[0]
        position += 4
        if token == 1:
            end = structure.index(0, position)
            nodes.append(structure[position:end].decode())
            position = (end + 4) & ~3
            result[('/'.join(nodes) or '/', '')] = b''
        elif token == 2:
            nodes.pop()
        elif token == 3:
            size, offset = struct.unpack_from('>II', structure, position)
            position += 8
            end = strings.index(0, offset)
            name = strings[offset:end].decode()
            result[('/'.join(nodes) or '/', name)] = structure[position:position + size]
            position = (position + size + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            break
        else:
            raise ValueError(f'unknown FDT token {token}')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--dtb', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    tool = Path(__file__).resolve().parents[1] / 'packages/thorch-bsp/payload/usr/lib/thorch/boot_image.py'
    spec = importlib.util.spec_from_file_location('boot_image', tool)
    assert spec is not None and spec.loader is not None
    boot_image = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(boot_image)
    path = args.artifacts / 'boot/KERNEL'
    boot = boot_image.BootImage.read(path)
    image, dtbs = boot_image.analyze_kernel(
        boot, allow_raw_fallback=False, require_symbols=True,
        require_thor=True, forbid_aim300=True,
    )
    assert image == (args.artifacts / 'boot/Image').read_bytes(), 'Image mismatch'
    index = next(i for i, dtb in enumerate(dtbs)
                 if boot_image.fdt_facts(dtb)[0].get(b'model') == b'AYN Thor\0')
    old = dtbs[index]
    before = properties(old)
    candidates = [node for (node, name), value in before.items()
                  if name == 'compatible' and value == b'qcom,hv-haptics\0']
    assert len(candidates) == 1, candidates
    node = candidates[0]
    assert before[(node, 'qcom,vmax-mv')] == struct.pack('>I', 5000)
    assert (node, 'qcom,use-erm') in before
    args.dtb.write_bytes(old)
    subprocess.run(['fdtput', '-t', 'i', str(args.dtb), node,
                    'qcom,vmax-mv', '1300'], check=True)
    subprocess.run(['fdtput', '-d', str(args.dtb), node, 'qcom,use-erm'], check=True)
    changed = args.dtb.read_bytes()
    expected = dict(before)
    expected[(node, 'qcom,vmax-mv')] = struct.pack('>I', 1300)
    del expected[(node, 'qcom,use-erm')]
    assert properties(changed) == expected, 'Unexpected DT semantic change'
    # fdtput may compact block offsets, but reserve entries must remain identical.
    def reservations(data: bytes) -> bytes:
        offset = struct.unpack_from('>I', data, 16)[0]
        end = offset
        while data[end:end + 16] != bytes(16):
            end += 16
        return data[offset:end + 16]
    assert reservations(old) == reservations(changed), 'Reservation changed'
    compressed_size = len(boot.kernel_payload) - sum(len(d) for d in dtbs)
    dtbs[index] = changed
    trailer = b''.join(dtbs)
    payload = boot.kernel_payload[:compressed_size] + trailer
    header = bytearray(boot.header)
    struct.pack_into('<I', header, 8, len(payload))
    candidate = (bytes(header) + boot_image.padded(payload, boot.page_size)
                 + boot_image.padded(boot.ramdisk, boot.page_size)
                 + boot_image.padded(boot.second, boot.page_size) + boot.tail)
    check = boot_image.BootImage(path, candidate)
    after_image, after_dtbs = boot_image.analyze_kernel(
        check, allow_raw_fallback=False, require_symbols=True,
        require_thor=True, forbid_aim300=True,
    )
    assert after_image == image and check.ramdisk == boot.ramdisk
    assert check.second == boot.second and check.tail == boot.tail
    assert check.command_line == boot.command_line
    assert len(after_dtbs) == len(dtbs)
    assert after_dtbs == dtbs
    sha = lambda data: hashlib.sha256(data).hexdigest()
    report = {
        'template_before_sha256': sha(boot.data),
        'template_after_sha256': sha(candidate),
        'image_sha256': sha(image), 'dtb_count': len(dtbs),
        'thor_slot': index, 'thor_dtb_before_sha256': sha(old),
        'thor_dtb_after_sha256': sha(changed), 'haptics_node': node,
        'only_property_changes': {'qcom,vmax-mv': [5000, 1300], 'qcom,use-erm': 'removed'},
        'other_dtbs_byte_identical': True, 'kernel_gzip_byte_identical': True,
        'ramdisk_byte_identical': True, 'other_nodes_properties_identical': True,
        'other_effect_voltage_properties_unchanged': True,
    }
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    path.write_bytes(candidate)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

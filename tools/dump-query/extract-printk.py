#!/usr/bin/env python3
"""Extract the ARM64 static printk ring from a DDR dump and matching vmlinux.

Supports the 88-byte printk_info layout used by the Thor diagnostic kernel.
The exact Linux banner anchors physical placement; no deployment addresses.
"""
import argparse
import mmap
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vmlinux', type=Path)
    parser.add_argument('ddr', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.vmlinux.open('rb') as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name('.symtab')
        symbols = {s.name: s['st_value'] for s in symtab.iter_symbols()
                   if s.name in ('linux_banner', 'printk_rb_static',
                                 '_printk_rb_static_infos', 'prb')}
        banner_addr = symbols['linux_banner']
        section = next(s for s in elf.iter_sections()
                       if s['sh_addr'] <= banner_addr < s['sh_addr'] + s['sh_size'])
        banner = section.data()[banner_addr - section['sh_addr']:].split(b'\0', 1)[0]
    with args.ddr.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as ram:
        candidates = []
        pos = ram.find(banner)
        while pos >= 0:
            base = pos - banner_addr
            rb = base + symbols['printk_rb_static']
            if 0 <= rb < len(ram) - 88:
                bits = struct.unpack_from('<I', ram, rb)[0]
                text_bits = struct.unpack_from('<I', ram, rb + 48)[0]
                if 4 <= bits <= 22 and 12 <= text_bits <= 28:
                    candidates.append((base, rb, bits, text_bits))
            pos = ram.find(banner, pos + len(banner))
        if len(candidates) != 1:
            raise ValueError(f'expected one valid static ring, found {len(candidates)}')
        base, rb, bits, text_bits = candidates[0]
        u64 = lambda off: struct.unpack_from('<Q', ram, off)[0]
        infos_ptr = u64(rb + 16)
        relocation = infos_ptr - symbols['_printk_rb_static_infos']
        offset = lambda ptr: base + ptr - relocation
        if offset(u64(base + symbols['prb'])) != rb:
            raise ValueError('active ring is not the supported static ring')
        infos = offset(infos_ptr)
        descs = offset(u64(rb + 8))
        text = offset(u64(rb + 56))
        count, text_size = 1 << bits, 1 << text_bits
        for start, size in ((infos, count * 88), (descs, count * 24), (text, text_size)):
            if start < 0 or start + size > len(ram):
                raise ValueError('ring buffers outside captured DDR range')
        records = []
        for i in range(count):
            state, begin, end = struct.unpack_from('<QQQ', ram, descs + i * 24)
            if state >> 62 not in (1, 2) or begin & 1:
                continue
            seq, ts, length = struct.unpack_from('<QQH', ram, infos + i * 88)
            if not length or length > text_size - 8:
                continue
            # Wrapped allocations restart at offset zero of the data ring.
            at = begin & (text_size - 1)
            if at + 8 + length > text_size:
                at = 0
            if (u64(text + at) & ((1 << 62) - 1)) != state & ((1 << 62) - 1):
                continue
            message = ram[text + at + 8:text + at + 8 + length].decode(errors='replace')
            records.append((seq, ts, message))
        records.sort()
        if not records:
            raise ValueError('no valid printk records')
        args.output.write_text(''.join(f'[{ts / 1e9:13.6f}] {msg}\n'
                                       for _, ts, msg in records))
        print(f'Extracted {len(records)} records to {args.output}')


if __name__ == '__main__':
    main()

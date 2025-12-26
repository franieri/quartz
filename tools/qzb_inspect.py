#!/usr/bin/env python3
import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


QZB1 = 0x31425A51  # 'QZB1' little-endian u32
JLB1_LEGACY = 0x4A4C4231


@dataclass
class QzbHeader:
    magic: int
    version: int
    quartz_api: int | None = None
    flags: int | None = None
    meta_size: int | None = None
    meta_crc: int | None = None
    payload_size: int | None = None
    payload_crc: int | None = None


def crc32(data: bytes) -> int:
    # Same polynomial as the C++ implementation in src/core/bytecode/bytecode.cpp
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = ((crc >> 1) ^ (0xEDB88320 & mask)) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def read_u32(buf: bytes, off: int) -> tuple[int, int]:
    return struct.unpack_from('<I', buf, off)[0], off + 4


def read_u16(buf: bytes, off: int) -> tuple[int, int]:
    return struct.unpack_from('<H', buf, off)[0], off + 2


def parse_header(data: bytes) -> tuple[QzbHeader, int]:
    off = 0
    magic, off = read_u32(data, off)
    ver, off = read_u16(data, off)
    _reserved, off = read_u16(data, off)

    h = QzbHeader(magic=magic, version=ver)

    if ver >= 4:
        h.quartz_api, off = read_u32(data, off)
        h.flags, off = read_u32(data, off)
        h.meta_size, off = read_u32(data, off)
        h.meta_crc, off = read_u32(data, off)
        h.payload_size, off = read_u32(data, off)
        h.payload_crc, off = read_u32(data, off)
        return h, off

    if ver == 3:
        h.quartz_api, off = read_u32(data, off)
        h.flags, off = read_u32(data, off)
        h.payload_size, off = read_u32(data, off)
        h.payload_crc, off = read_u32(data, off)
        return h, off

    return h, off


def parse_metadata(meta: bytes) -> dict:
    off = 0
    meta_ver, off = read_u32(meta, off)
    src_count, off = read_u32(meta, off)

    sources = []
    for _ in range(src_count):
        path_len, off = read_u32(meta, off)
        path = meta[off:off + path_len].decode('utf-8')
        off += path_len
        size_bytes = struct.unpack_from('<Q', meta, off)[0]
        off += 8
        sha = meta[off:off + 32].hex()
        off += 32
        sources.append({'path': path, 'size': size_bytes, 'sha256': sha})

    opt_len, off = read_u32(meta, off)
    opts = meta[off:off + opt_len].decode('utf-8')
    off += opt_len

    return {'meta_version': meta_ver, 'sources': sources, 'compiler_options': opts}


def main() -> int:
    ap = argparse.ArgumentParser(description='Inspect/validate Quartz .qzb files (header, metadata, CRCs).')
    ap.add_argument('qzb', help='Path to .qzb file')
    ap.add_argument('--verify', action='store_true', help='Exit non-zero on CRC mismatch or bad header')
    args = ap.parse_args()

    p = Path(args.qzb)
    if not p.exists():
        print(f'Missing file: {p}', file=sys.stderr)
        return 2

    data = p.read_bytes()
    try:
        header, off = parse_header(data)
    except Exception as e:
        print(f'Failed to parse header: {e}', file=sys.stderr)
        return 2
    if ver >= 5:
        if len(data) < off + 12:
            raise ValueError('Truncated v5 header')
        quartz_api = u32le(data, off)
        flags = u32le(data, off + 4)
        section_count = u32le(data, off + 8)
        off += 12

        if section_count == 0 or section_count > 32:
            raise ValueError('Invalid section count')

        desc_bytes = section_count * 12
        if len(data) < off + desc_bytes:
            raise ValueError('Truncated section table')

        sections = []
        for i in range(section_count):
            t = u32le(data, off + i * 12)
            sz = u32le(data, off + i * 12 + 4)
            crc = u32le(data, off + i * 12 + 8)
            sections.append((t, sz, crc))
        off += desc_bytes

        meta = b''
        payload = b''
        for (t, sz, crc) in sections:
            if len(data) < off + sz:
                raise ValueError('Truncated section payload')
            blob = data[off:off + sz]
            off += sz
            if crc32(blob) != crc:
                raise ValueError('CRC mismatch in section')
            if t == 0x4154454D:  # 'META'
                meta = blob
            elif t == 0x4C594150:  # 'PAYL'
                payload = blob

        if not payload:
            raise ValueError('Missing PAYL section')
        return QzbFile(
            version=ver,
            quartz_api=quartz_api,
            flags=flags,
            meta=meta,
            payload=payload,
            meta_crc=crc32(meta) if meta else 0,
            payload_crc=crc32(payload),
        )

    if ver >= 4:
    ok = True
    if header.magic not in (QZB1, JLB1_LEGACY):
        print(f'Bad magic: {hex(header.magic)}', file=sys.stderr)
        ok = False

    print(f'file: {p}')
    print(f'magic: {hex(header.magic)}')
    print(f'version: {header.version}')

    if header.version >= 4:
        print(f'quartz_api: {header.quartz_api}')
        print(f'flags: {header.flags}')
        print(f'meta_size: {header.meta_size}')
        print(f'meta_crc: {hex(header.meta_crc or 0)}')
        print(f'payload_size: {header.payload_size}')
        print(f'payload_crc: {hex(header.payload_crc or 0)}')

        meta = data[off:off + (header.meta_size or 0)]
        off2 = off + (header.meta_size or 0)
        payload = data[off2:off2 + (header.payload_size or 0)]

        calc_meta_crc = crc32(meta)
        calc_payload_crc = crc32(payload)
        if header.meta_crc is not None and calc_meta_crc != header.meta_crc:
            print(f'ERROR: metadata CRC mismatch (expected {hex(header.meta_crc)}, got {hex(calc_meta_crc)})', file=sys.stderr)
            ok = False
        if header.payload_crc is not None and calc_payload_crc != header.payload_crc:
            print(f'ERROR: payload CRC mismatch (expected {hex(header.payload_crc)}, got {hex(calc_payload_crc)})', file=sys.stderr)
            ok = False

        meta_info = parse_metadata(meta)
        print(f'metadata_version: {meta_info["meta_version"]}')
        print(f'sources: {len(meta_info["sources"])}')
        for s in meta_info['sources']:
            print(f'- {s["path"]}')
            print(f'  size: {s["size"]}')
            print(f'  sha256: {s["sha256"]}')
        print(f'compiler_options: {meta_info["compiler_options"]}')

    elif header.version == 3:
        payload = data[off:off + (header.payload_size or 0)]
        calc_payload_crc = crc32(payload)
        print(f'quartz_api: {header.quartz_api}')
        print(f'flags: {header.flags}')
        print(f'payload_size: {header.payload_size}')
        print(f'payload_crc: {hex(header.payload_crc or 0)}')
        if header.payload_crc is not None and calc_payload_crc != header.payload_crc:
            print(f'ERROR: payload CRC mismatch (expected {hex(header.payload_crc)}, got {hex(calc_payload_crc)})', file=sys.stderr)
            ok = False
        print('metadata: (none in v3)')

    else:
        print('metadata: (none in v1/v2)')

    if args.verify and not ok:
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

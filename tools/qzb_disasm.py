#!/usr/bin/env python3
import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


QZB1 = 0x31425A51  # 'QZB1'
JLB1_LEGACY = 0x4A4C4231


OPCODES = [
    'NOP',
    'PUSH_INT32',
    'PUSH_DOUBLE64',
    'PUSH_BOOL',
    'PUSH_STRING',
    'POP',
    'LOAD_VAR',
    'STORE_VAR',
    'DECLARE_ARRAY',
    'DECLARE_DICT',
    'DECLARE_LAMBDA',
    'BINARY_OP',
    'UNARY_OP',
    'INDEX_GET',
    'JUMP',
    'JUMP_IF_FALSE',
    'JUMP_IF_TRUE',
    'CALL_NAME',
    'NEW_OBJECT',
    'MAKE_LAMBDA',
    'TRY_PUSH',
    'TRY_POP',
    'CATCH_CLEAR',
    'THROW_VALUE',
    'THROW_NEW',
    'FINALLY_END',
    'DEF_CLASS',
    'DEF_INTERFACE',
    'SET_CURRENT_MODULE',
    'CLEAR_CURRENT_MODULE',
    'RETURN_VALUE',
    'RETURN_VOID',
    'LOAD_SLOT',
    'STORE_SLOT',
    'MAKE_ARRAY_EXPR',
    'MAKE_DICT_EXPR',
    # New specialized opcodes
    'PUSH_INT32_0',
    'PUSH_INT32_1',
    'PUSH_INT32_NEG1',
    'PUSH_TRUE',
    'PUSH_FALSE',
    'PUSH_NULL',
    'LOAD_SLOT_0',
    'STORE_SLOT_0',
    'CALL_NAME_0',
    'CALL_NAME_1',
    'CALL_NAME_2',
    'INCREMENT_SLOT',
    'DECREMENT_SLOT',
    'LOAD_SLOT_PUSH_INT32',
    'BINARY_OP_STORE_SLOT',
]



@dataclass
class Program:
    strings: list[str]
    functions: list[dict]
    entry: int
    modules: dict[str, int]


def read_u8(b: bytes, o: int) -> tuple[int, int]:
    return b[o], o + 1


def read_u16(b: bytes, o: int) -> tuple[int, int]:
    return struct.unpack_from('<H', b, o)[0], o + 2


def read_u32(b: bytes, o: int) -> tuple[int, int]:
    return struct.unpack_from('<I', b, o)[0], o + 4


def read_i32(b: bytes, o: int) -> tuple[int, int]:
    return struct.unpack_from('<i', b, o)[0], o + 4


def read_f64(b: bytes, o: int) -> tuple[float, int]:
    return struct.unpack_from('<d', b, o)[0], o + 8


def parse_qzb(path: Path) -> tuple[int, bytes]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError('too small')
    magic = struct.unpack_from('<I', data, 0)[0]
    ver = struct.unpack_from('<H', data, 4)[0]
    off = 8

    if magic not in (QZB1, JLB1_LEGACY):
        raise ValueError(f'bad magic: {hex(magic)}')

    if ver >= 5:
        if len(data) < off + 12:
            raise ValueError('truncated v5 header')
        quartz_api, flags, section_count = struct.unpack_from('<III', data, off)
        off += 12
        if section_count == 0 or section_count > 32:
            raise ValueError('bad section count')

        desc_off = off
        off += section_count * 12
        if len(data) < off:
            raise ValueError('truncated section table')

        payload = b''
        for i in range(section_count):
            t, sz, crc = struct.unpack_from('<III', data, desc_off + i * 12)
            if len(data) < off + sz:
                raise ValueError('truncated section payload')
            blob = data[off:off + sz]
            off += sz
            # crc validated by C++ reader; keep light here
            if t == 0x4C594150:  # 'PAYL'
                payload = blob

        if not payload:
            raise ValueError('missing PAYL section')
        return ver, payload

    if ver >= 4:
        # quartz_api, flags, meta_size, meta_crc, payload_size, payload_crc
        quartz_api, flags, meta_size, meta_crc, payload_size, payload_crc = struct.unpack_from('<IIIIII', data, off)
        off += 24
        off += meta_size
        payload = data[off:off + payload_size]
        return ver, payload

    if ver == 3:
        quartz_api, flags, payload_size, payload_crc = struct.unpack_from('<IIII', data, off)
        off += 16
        payload = data[off:off + payload_size]
        return ver, payload

    # v1/v2: entire remaining file is payload
    payload = data[off:]
    return ver, payload


def parse_payload(payload: bytes, version: int) -> Program:
    o = 0
    strings = []
    sc, o = read_u32(payload, o)
    for _ in range(sc):
        ln, o = read_u32(payload, o)
        s = payload[o:o + ln].decode('utf-8', 'replace')
        o += ln
        strings.append(s)

    fn_count, o = read_u32(payload, o)
    entry, o = read_u32(payload, o)

    functions = []
    for _ in range(fn_count):
        name_string, o = read_u32(payload, o)
        param_count, o = read_u32(payload, o)
        params = []
        for _ in range(param_count):
            pidx, o = read_u32(payload, o)
            params.append(pidx)

        if version >= 2:
            local_count, o = read_u32(payload, o)
            locals_ = []
            for _ in range(local_count):
                lidx, o = read_u32(payload, o)
                locals_.append(lidx)
        else:
            locals_ = params[:]

        code_size, o = read_u32(payload, o)
        code = payload[o:o + code_size]
        o += code_size

        functions.append({
            'name_string': name_string,
            'params': params,
            'locals': locals_,
            'code': code,
        })

    modules = {}
    mod_count, o = read_u32(payload, o)
    for _ in range(mod_count):
        mod_str_idx, o = read_u32(payload, o)
        fn_idx, o = read_u32(payload, o)
        if 0 <= mod_str_idx < len(strings):
            modules[strings[mod_str_idx]] = fn_idx

    return Program(strings=strings, functions=functions, entry=entry, modules=modules)


def sidx_to_str(strings: list[str], idx: int) -> str:
    if idx == 0xFFFFFFFF:
        return '<invalid>'
    if 0 <= idx < len(strings):
        return strings[idx]
    return f'<bad:{idx}>'


def disasm_def_class(code: bytes, o: int, strings: list[str]) -> tuple[str, int]:
    name_idx, o = read_u32(code, o)
    parent_idx, o = read_u32(code, o)
    iface_count, o = read_u32(code, o)
    ifaces = []
    for _ in range(iface_count):
        x, o = read_u32(code, o)
        ifaces.append(sidx_to_str(strings, x))
    gen_count, o = read_u32(code, o)
    for _ in range(gen_count):
        _, o = read_u32(code, o)
    field_count, o = read_u32(code, o)
    fields = []
    for _ in range(field_count):
        x, o = read_u32(code, o)
        fields.append(sidx_to_str(strings, x))
    static_count, o = read_u32(code, o)
    for _ in range(static_count):
        _, o = read_u32(code, o)
        _, o = read_u8(code, o)
        _, o = read_u32(code, o)
    method_count, o = read_u32(code, o)
    for _ in range(method_count):
        _, o = read_u32(code, o)  # name
        _, o = read_u8(code, o)   # isStatic
        pc, o = read_u8(code, o)
        for _ in range(pc):
            _, o = read_u32(code, o)
        _, o = read_u32(code, o)  # fn idx
    has_ctor, o = read_u8(code, o)
    if has_ctor:
        pc, o = read_u8(code, o)
        for _ in range(pc):
            _, o = read_u32(code, o)
        _, o = read_u32(code, o)
        init_count, o = read_u32(code, o)
        for _ in range(init_count):
            _, o = read_u32(code, o)
            _, o = read_u32(code, o)

    return f"name={sidx_to_str(strings, name_idx)} parent={sidx_to_str(strings, parent_idx)} ifaces={ifaces} fields={fields}", o


def disasm_def_interface(code: bytes, o: int, strings: list[str]) -> tuple[str, int]:
    name_idx, o = read_u32(code, o)
    ext_count, o = read_u32(code, o)
    for _ in range(ext_count):
        _, o = read_u32(code, o)
    gen_count, o = read_u32(code, o)
    for _ in range(gen_count):
        _, o = read_u32(code, o)
    mcount, o = read_u32(code, o)
    for _ in range(mcount):
        _, o = read_u32(code, o)
        _, o = read_u8(code, o)
    return f"name={sidx_to_str(strings, name_idx)}", o


def disassemble_function(fn: dict, strings: list[str]) -> list[str]:
    code = fn['code']
    out = []
    o = 0
    while o < len(code):
        ip = o
        op, o = read_u8(code, o)
        opname = OPCODES[op] if op < len(OPCODES) else f'OP_{op}'
        extra = ''

        try:
            if opname == 'PUSH_INT32':
                v, o = read_i32(code, o)
                extra = str(v)
            elif opname == 'PUSH_DOUBLE64':
                v, o = read_f64(code, o)
                extra = str(v)
            elif opname == 'PUSH_BOOL':
                v, o = read_u8(code, o)
                extra = 'true' if v else 'false'
            elif opname == 'PUSH_STRING':
                sidx, o = read_u32(code, o)
                extra = f'{sidx} ({sidx_to_str(strings, sidx)})'
            elif opname in ('LOAD_VAR', 'STORE_VAR', 'INDEX_GET'):
                sidx, o = read_u32(code, o)
                extra = f'{sidx_to_str(strings, sidx)}'
            elif opname == 'DECLARE_ARRAY':
                name, o = read_u32(code, o)
                cnt, o = read_u16(code, o)
                extra = f'{sidx_to_str(strings, name)} count={cnt}'
            elif opname == 'DECLARE_DICT':
                name, o = read_u32(code, o)
                cnt, o = read_u16(code, o)
                keys = []
                for _ in range(cnt):
                    k, o = read_u32(code, o)
                    keys.append(sidx_to_str(strings, k))
                extra = f'{sidx_to_str(strings, name)} keys={keys}'
            elif opname == 'DECLARE_LAMBDA':
                name, o = read_u32(code, o)
                fnidx, o = read_u32(code, o)
                extra = f'{sidx_to_str(strings, name)} fn={fnidx}'
            elif opname == 'BINARY_OP':
                bop, o = read_u8(code, o)
                extra = str(bop)
            elif opname == 'UNARY_OP':
                uop, o = read_u8(code, o)
                extra = str(uop)
            elif opname in ('JUMP', 'JUMP_IF_FALSE', 'JUMP_IF_TRUE'):
                rel, o = read_i32(code, o)
                extra = f'rel={rel} -> {ip + 1 + 4 + rel}'
            elif opname == 'CALL_NAME':
                name, o = read_u32(code, o)
                argc, o = read_u8(code, o)
                extra = f'{sidx_to_str(strings, name)} argc={argc}'
            elif opname == 'NEW_OBJECT':
                cname, o = read_u32(code, o)
                argc, o = read_u8(code, o)
                extra = f'{sidx_to_str(strings, cname)} argc={argc}'
            elif opname == 'MAKE_LAMBDA':
                fnidx, o = read_u32(code, o)
                extra = f'fn={fnidx}'
            elif opname == 'TRY_PUSH':
                catch_ip, o = read_u32(code, o)
                finally_ip, o = read_u32(code, o)
                has_fin, o = read_u8(code, o)
                catch_var, o = read_u32(code, o)
                catch_type, o = read_u32(code, o)
                extra = f'catch={catch_ip} finally={finally_ip} hasFinally={has_fin} var={sidx_to_str(strings, catch_var)} type={sidx_to_str(strings, catch_type)}'
            elif opname == 'CATCH_CLEAR':
                v, o = read_u32(code, o)
                extra = sidx_to_str(strings, v)
            elif opname == 'THROW_NEW':
                t, o = read_u32(code, o)
                extra = sidx_to_str(strings, t)
            elif opname == 'SET_CURRENT_MODULE':
                m, o = read_u32(code, o)
                extra = sidx_to_str(strings, m)
            elif opname in ('LOAD_SLOT', 'STORE_SLOT'):
                slot, o = read_u16(code, o)
                extra = f'slot={slot}'
            elif opname == 'MAKE_ARRAY_EXPR':
                cnt, o = read_u16(code, o)
                extra = f'count={cnt}'
            elif opname == 'MAKE_DICT_EXPR':
                cnt, o = read_u16(code, o)
                keys = []
                for _ in range(cnt):
                    k, o = read_u32(code, o)
                    keys.append(sidx_to_str(strings, k))
                extra = f'keys={keys}'
            elif opname == 'DEF_CLASS':
                extra, o = disasm_def_class(code, o, strings)
            elif opname == 'DEF_INTERFACE':
                extra, o = disasm_def_interface(code, o, strings)
            # New specialized opcodes
            elif opname in ('PUSH_INT32_0', 'PUSH_INT32_1', 'PUSH_INT32_NEG1', 
                           'PUSH_TRUE', 'PUSH_FALSE', 'PUSH_NULL', 
                           'LOAD_SLOT_0', 'STORE_SLOT_0'):
                # No operands
                pass
            elif opname in ('CALL_NAME_0', 'CALL_NAME_1', 'CALL_NAME_2'):
                name, o = read_u32(code, o)
                extra = f'{sidx_to_str(strings, name)}'
            elif opname in ('INCREMENT_SLOT', 'DECREMENT_SLOT'):
                slot, o = read_u16(code, o)
                extra = f'slot={slot}'
            elif opname == 'LOAD_SLOT_PUSH_INT32':
                slot, o = read_u16(code, o)
                v, o = read_i32(code, o)
                extra = f'slot={slot} value={v}'
            elif opname == 'BINARY_OP_STORE_SLOT':
                bop, o = read_u8(code, o)
                slot, o = read_u16(code, o)
                extra = f'op={bop} slot={slot}'
        except Exception as e:
            extra = f'<<decode error: {e}>>'
            # bail to avoid infinite loop
            out.append(f'{ip:04d}: {opname} {extra}')
            break

        out.append(f'{ip:04d}: {opname}' + (f' {extra}' if extra else ''))

    return out


def main() -> int:
    ap = argparse.ArgumentParser(description='Disassemble Quartz .qzb bytecode payload.')
    ap.add_argument('qzb', help='Path to .qzb')
    ap.add_argument('--function', type=int, default=None, help='Disassemble only a single function index')
    ap.add_argument('--show-strings', action='store_true', help='Print string table')
    args = ap.parse_args()

    p = Path(args.qzb)
    if not p.exists():
        print(f'Missing file: {p}', file=sys.stderr)
        return 2

    try:
        ver, payload = parse_qzb(p)
        prog = parse_payload(payload, ver)
    except Exception as e:
        print(f'Failed to parse qzb: {e}', file=sys.stderr)
        return 2

    print(f'version={ver}')
    print(f'strings={len(prog.strings)} functions={len(prog.functions)} entry={prog.entry} modules={len(prog.modules)}')

    if args.show_strings:
        for i, s in enumerate(prog.strings):
            print(f'S[{i}] = {s!r}')

    fn_indices = [args.function] if args.function is not None else list(range(len(prog.functions)))
    for idx in fn_indices:
        if idx < 0 or idx >= len(prog.functions):
            print(f'Bad function index: {idx}', file=sys.stderr)
            return 2
        fn = prog.functions[idx]
        name = sidx_to_str(prog.strings, fn['name_string'])
        print(f'\n== function[{idx}] {name} ==')
        for line in disassemble_function(fn, prog.strings):
            print(line)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())

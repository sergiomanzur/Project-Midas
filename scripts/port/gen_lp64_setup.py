#!/usr/bin/env python3
"""
gen_lp64_setup.py - generate port/generated/lp64_setup_ops.h

A retail setup file keeps its prop definitions in the N64 layout: pointer fields are 4 bytes.
On a 64-bit host the same C structs are larger (pointers are 8 bytes), so the game cannot read
the records in place. port/src/lp64_setup.c expands every record of a ROM-served setup into the
host layout at stage load; this script produces the field map it uses.

It asks clang for both layouts of every prop-definition record struct (an i686 target stands in
for the N64: 4-byte pointers, 4-byte alignment for 8-byte scalars) and an x86_64 target (the host
layout), then writes, per record type, the runs of plain data that move and the pointer slots that
widen. A pointer slot keeps its 32-bit value zero-extended (a few carry setup-file offsets).

Usage: python scripts/port/gen_lp64_setup.py [--clang PATH]
Needs clang on PATH (any recent LLVM). The result is committed, so only a change to the record
structs in src/bondtypes.h needs this re-run.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# PROPDEF type -> record struct (src/bondconstants.h PROPDEF_*, prop.c's casts)
TYPES = [
    (0x01, 'DOOR', 'DoorRecord'),
    (0x02, 'DOOR_SCALE', 'GlobalDoorScaleRecord'),
    (0x03, 'PROP', 'ObjectRecord'),
    (0x04, 'KEY', 'KeyRecord'),
    (0x05, 'ALARM', 'ObjectRecord'),
    (0x06, 'CCTV', 'CCTVRecord'),
    (0x07, 'MAGAZINE', 'AmmoCrateRecord'),
    (0x08, 'COLLECTABLE', 'WeaponObjRecord'),
    (0x09, 'GUARD', 'GuardRecord'),
    (0x0A, 'MONITOR', 'MonitorObjRecord'),
    (0x0B, 'MULTI_MONITOR', 'MultiMonitorObjRecord'),
    (0x0C, 'RACK', 'ObjectRecord'),
    (0x0D, 'AUTOGUN', 'AutogunRecord'),
    (0x11, 'HAT', 'ObjectRecord'),
    (0x14, 'AMMO', 'MultiAmmoCrateRecord'),
    (0x15, 'ARMOUR', 'BodyArmourRecord'),
    (0x24, 'GAS_RELEASING', 'ObjectRecord'),
    (0x27, 'VEHICHLE', 'VehichleRecord'),
    (0x28, 'AIRCRAFT', 'AircraftRecord'),
    (0x2A, 'GLASS', 'ObjectRecord'),
    (0x2B, 'SAFE', 'ObjectRecord'),
    (0x2D, 'TANK', 'TankRecord'),
    (0x2F, 'TINTED_GLASS', 'TintedGlassRecord'),
    (0x0E, 'LINK', 'LinkRecord'),
    (0x12, 'GUARD_ATTRIBUTE', 'GuardAttributeRecord'),
    (0x13, 'SWITCH', 'LinkRecord'),
    (0x16, 'TAG', 'TagObjectRecord'),
    (0x17, 'OBJECTIVE_START', 'MissionObjectiveRecord'),
    (0x19, 'OBJECTIVE_DESTROY_OBJECT', 'DestroyObjectRecord'),
    (0x1A, 'OBJECTIVE_COMPLETE_CONDITION', 'CompleteConditionRecord'),
    (0x1B, 'OBJECTIVE_FAIL_CONDITION', 'FailConditionRecord'),
    (0x1C, 'OBJECTIVE_COLLECT_OBJECT', 'CollectObjectRecord'),
    (0x1D, 'OBJECTIVE_DEPOSIT_OBJECT', 'DepositObjectRecord'),
    (0x1E, 'OBJECTIVE_PHOTOGRAPH', 'PhotographObjectRecord'),
    (0x1F, 'OBJECTIVE_NULL', 'NULLObjectRecord'),
    (0x20, 'OBJECTIVE_ENTER_ROOM', 'EnterRoomRecord'),
    (0x21, 'OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM', 'DepositObjectInRoomRecord'),
    (0x22, 'OBJECTIVE_COPY_ITEM', 'CoopyObjectRecord'),
    (0x23, 'WATCH_MENU_OBJECTIVE_TEXT', 'WatchMenuObjectiveTextRecord'),
    (0x25, 'RENAME', 'RenameObjectRecord'),
    (0x26, 'LOCK_DOOR', 'LockDoorRecord'),
    (0x2C, 'SAFE_ITEM', 'SafeObjectRecord'),
    (0x2E, 'CAMERAPOS', 'CutsceneRecord'),
]

DEFINES = ['-D__USE_MINGW_ANSI_STDIO=0', '-DGE_MODDED_CHEATS=1', '-DAVOID_UB', '-DBUGFIX_R0',
           '-DLANG_US', '-DNONMATCHING', '-DREFRESH_NTSC', '-DVERSION_US', '-D_LANGUAGE_C']
INCLUDES = ['', 'include', 'include/ultra64', 'include/PR', 'src', 'src/game', 'src/inflate',
            'port/include', 'src/libultra', 'build/generated']
CFLAGS = ['-funsigned-char', '-fms-extensions', '-fcommon', '-std=gnu11', '-w', '-ffreestanding',
          '-Xclang', '-fdump-record-layouts-complete', '-fsyntax-only']

LINE = re.compile(r'^\s*(\d+)(?::(\d+)-(\d+))?\s*\|(\s*)(.*)$')
SIZEOF = re.compile(r'^\s*\|\s*\[sizeof=(\d+), align=(\d+)')


def dump(clang, target):
    with tempfile.NamedTemporaryFile('w', suffix='.c', delete=False, dir=ROOT) as f:
        f.write('#include <ultra64.h>\n#include <bondgame.h>\n#include <bondtypes.h>\n')
        for _, _, s in TYPES:
            f.write('%s probe_%s;\n' % (s, s))
        probe = f.name
    try:
        cmd = [clang, '-target', target] + DEFINES + ['-I' + os.path.join(ROOT, i) for i in INCLUDES] \
            + CFLAGS + [probe]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            sys.stderr.write(r.stderr[-3000:])
            sys.exit('clang failed for ' + target)
        return r.stdout
    finally:
        os.unlink(probe)


def parse(text):
    """-> {struct name: (sizeof, [(offset, indent, content)])} for every dumped record."""
    out = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith('*** Dumping AST Record Layout'):
            i += 1
            head = LINE.match(lines[i])
            name = head.group(5).strip() if head else ''
            fields = []
            size = None
            i += 1
            while i < len(lines) and not lines[i].startswith('***'):
                m = SIZEOF.match(lines[i])
                if m:
                    size = int(m.group(1))
                else:
                    m = LINE.match(lines[i])
                    if m:
                        fields.append((int(m.group(1)), len(m.group(4)), m.group(5)))
                i += 1
            if name.startswith('struct ') or name.startswith('union '):
                out[name.split(' ', 1)[1]] = (size, fields)
            continue
        i += 1
    return out


ARRAY = re.compile(r'^(?:struct |union )?([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\] ')


def leaves(fields, d, base=0):
    """Pointer slots and plain leaves of one record: [(offset, is_pointer, text)].
    A field line is a leaf when the next line is not nested deeper than it. An array of records
    is expanded element by element from that record's own layout."""
    res = []
    for k, (off, ind, content) in enumerate(fields):
        nxt = fields[k + 1][1] if k + 1 < len(fields) else -1
        if nxt > ind:
            continue
        m = ARRAY.match(content)
        if m and m.group(1) in d:
            esize, efields = d[m.group(1)]
            for i in range(int(m.group(2))):
                res.extend(leaves(efields, d, base + off + i * esize))
            continue
        typ = content.rsplit(' ', 1)[0] if ' ' in content else content
        res.append((base + off, '*' in typ, content))
    return res


def build(l32, l64, name, d32, d64, wire_words):
    """-> (ops, host stride words) for one record type. The wire stride (setup_kinds.h) is the
    truth for how many bytes of the struct a setup file carries; a struct may be larger (the
    runtime fields at its end are not in the file)."""
    s32, f32 = l32
    s64, f64 = l64
    a, b = leaves(f32, d32), leaves(f64, d64)
    if len(a) != len(b):
        sys.exit('%s: leaf count differs between layouts (%d vs %d)' % (name, len(a), len(b)))
    pairs = []
    for (o32, p32, c32), (o64, p64, c64) in zip(a, b):
        if p32 != p64 or c32.split(' ')[-1] != c64.split(' ')[-1]:
            sys.exit('%s: field mismatch %r vs %r' % (name, c32, c64))
        pairs.append((o32, o64, p32, c32))
    wire_bytes = wire_words * 4
    # a record may be padded past its struct to a whole word: those bytes ride along with the last run
    slots = sorted({(o32, o64) for o32, o64, p, _ in pairs if p and o32 + 4 <= wire_bytes})
    ops = []
    delta = 0
    pos32 = 0
    for o32, o64 in slots + [(wire_bytes, None)]:
        if o32 > pos32:
            ops.append([pos32, pos32 + delta, o32 - pos32, 0])
        if o64 is None:
            break
        ops.append([o32, o64, 4, 1])
        pos32 = o32 + 4
        delta = (o64 + 8) - (o32 + 4)
    for o32, o64, p, c in pairs:
        if p or o32 >= wire_bytes:
            continue
        run = [op for op in ops if op[3] == 0 and op[0] <= o32 < op[0] + op[2]]
        if not run:
            continue  # inside a pointer slot (a union overlay)
        if run[0][1] + (o32 - run[0][0]) != o64:
            sys.exit('%s: %s moves by a different amount than its run (%d vs %d)' %
                     (name, c, o64 - o32, run[0][1] - run[0][0]))
    host_end = max(op[1] + (8 if op[3] else op[2]) for op in ops)
    host_words = (host_end + 7) // 8 * 2   # even: the next record's pointers stay 8-byte aligned
    return ops, host_words


def wire_table():
    """PROPDEF type -> (label, wire words) from port/generated/setup_kinds.h."""
    text = open(os.path.join(ROOT, 'port', 'generated', 'setup_kinds.h')).read()
    body = text[text.index('kSetupPropdefKinds'):text.index('kSetupIntroKinds')]
    return {int(m.group(1)): (m.group(2), int(m.group(3)))
            for m in re.finditer(r'/\*\s*(\d+)\s+(\w+)\s*\*/\s*\{\s*(\d+),', body)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--clang', default='clang')
    args = ap.parse_args()
    d32 = parse(dump(args.clang, 'i686-linux-gnu'))
    d64 = parse(dump(args.clang, 'x86_64-linux-gnu'))

    out = ['/* GENERATED by scripts/port/gen_lp64_setup.py - do not edit.',
           ' * Per prop-definition record type: how port/src/lp64_setup.c expands the N64-layout',
           ' * record of a ROM-served setup into the 64-bit host struct. */',
           'struct LpSetupOp { unsigned short wire, host, len; unsigned char ptr; };',
           'struct LpSetupType { unsigned char type; unsigned short wireWords, hostWords; unsigned short firstOp, numOps; };',
           '']
    wt = wire_table()
    by_id = {tid: (label, struct) for tid, label, struct in TYPES}
    ops_all = []
    types = []
    seen = {}
    for tid in sorted(wt):
        label, wire_words = wt[tid]
        if tid in by_id:
            struct = by_id[tid][1]
            if struct not in d32 or struct not in d64:
                sys.exit('struct %s not found in the layout dump' % struct)
            key = (struct, wire_words)
            if key not in seen:
                seen[key] = build(d32[struct], d64[struct], struct, d32, d64, wire_words)
            ops, h = seen[key]
        else:
            struct = '(raw)'
            ops = [[0, 0, wire_words * 4, 0]]
            h = (wire_words + 1) // 2 * 2
        types.append((tid, label, struct, wire_words, h, len(ops_all), len(ops)))
        ops_all.extend(ops)
    out.append('static const struct LpSetupOp kLpSetupOps[] = {')
    for op in ops_all:
        out.append('    { %d, %d, %d, %d },' % tuple(op))
    out.append('};')
    out.append('')
    out.append('/* indexed by PROPDEF type */')
    out.append('static const struct LpSetupType kLpSetupTypes[] = {')
    for tid, label, struct, w, h, first, n in types:
        out.append('    /* %2d %-32s %-24s */ { %d, %d, %d, %d, %d },' % (tid, label, struct, tid, w, h, first, n))
    out.append('};')
    out.append('#define LP_SETUP_TYPE_COUNT %d' % len(types))
    out.append('')
    dst = os.path.join(ROOT, 'port', 'generated', 'lp64_setup_ops.h')
    with open(dst, 'w', newline='\n') as f:
        f.write('\n'.join(out))
    print('wrote', dst)
    for tid, label, struct, w, h, first, n in types:
        print('%-14s %-24s wire=%3d words  host=%3d words  ops=%d' % (label, struct, w, h, n))


if __name__ == '__main__':
    main()

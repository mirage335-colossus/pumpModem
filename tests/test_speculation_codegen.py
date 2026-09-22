#!/usr/bin/env python3
"""Inspect optimized bounds/load witnesses; this is not a side-channel proof."""
import argparse
import re
import subprocess


class Instructions(list):
    def __init__(self):
        super().__init__()
        self.addresses = []


def function(disassembly, name):
    match = re.search(rf'^[0-9a-f]+ <_?{name}>:\n(.*?)(?=\n[0-9a-f]+ <|\nDisassembly|\Z)',
                      disassembly, re.M | re.S)
    if not match:
        raise AssertionError(f'missing generated-code witness {name}')
    instructions = Instructions()
    for line in match.group(1).splitlines():
        instruction = re.match(r'\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s*(.*)', line)
        if instruction:
            instructions.addresses.append(int(instruction[1], 16))
            instructions.append((instruction[2], instruction[3].strip()))
    return instructions


def verify_prefix_branches(instructions, read, arm=False):
    # These small witnesses have a straight-line protected read. A direct
    # forward exit past that read is harmless (Clang can put its stack-canary
    # failure branch before LFENCE). Reject all other pre-read transfers,
    # including indirect destinations and entries into the mask/barrier path.
    # This deliberately does not attempt to prove an arbitrary control-flow graph.
    for op, operand in instructions[:read]:
        branch = (op in ('b', 'bl', 'br', 'blr') or op.startswith(('b.', 'cb', 'tb'))) if arm else (
            op.startswith('j') or op in ('call', 'callq'))
        if not branch:
            continue
        destination = re.match(r'^(?:#)?(?:0x)?([0-9a-f]+)(?:\s|$)',
                               operand.rsplit(',', 1)[-1].strip())
        assert destination, instructions
        assert int(destination[1], 16) > instructions.addresses[read], instructions


def verify_x86(load, boundary):
    # The whole compare/mask/clip dependency lives in one inline-asm block.
    position = next(i for i, (op, _) in enumerate(load) if op in ('cmp', 'cmpq', 'cmpl'))
    compare, borrow, clip = load[position:position + 3]
    assert borrow[0] in ('sbb', 'sbbq', 'sbbl'), load
    assert clip[0] in ('and', 'andq', 'andl'), load
    index = compare[1].split(',')[1].strip()
    mask = borrow[1].split(',')[0].strip()
    assert borrow[1].replace(' ', '') == f'{mask},{mask}', load
    assert clip[1].replace(' ', '') == f'{mask},{index}', load
    # These witnesses read one byte. Ignore stack-canary loads/stores, but
    # inspect the path through the first payload read: an epilogue branch after
    # that read cannot bypass the mask and is legitimate with stack protection.
    def byte_read(instructions):
        return next(i for i, (op, operand) in enumerate(instructions)
                    if op in ('movzbl', 'movzbq', 'movzbw', 'movb') and
                    '(' in operand.split(',')[0])

    read = byte_read(load)
    assert position + 2 < read, load
    verify_prefix_branches(load, read)
    assert re.search(rf'\([^)]*{re.escape(index)}(?:,|\))', load[read][1]), load
    barrier = next(i for i, (op, _) in enumerate(boundary) if op == 'lfence')
    read = byte_read(boundary)
    assert barrier < read, boundary
    verify_prefix_branches(boundary, read)


def verify_aarch64(load, boundary):
    position = next(i for i, (op, _) in enumerate(load) if op == 'cmp')
    compare, clip, barrier = load[position:position + 3]
    assert clip[0] == 'csel', load
    assert barrier[0] == 'csdb' or (barrier[0] == 'hint' and
                                  barrier[1] in ('#20', '#0x14')), load
    operands = [part.strip() for part in clip[1].split(',')]
    index = compare[1].split(',')[0].strip()
    assert operands[1:] in ([index, 'xzr', 'cc'], [index, 'xzr', 'lo']), load
    read = next(i for i, (op, _) in enumerate(load) if op == 'ldrb')
    assert position + 2 < read, load
    verify_prefix_branches(load, read, arm=True)
    assert re.search(rf'\[[^]]*\b{operands[0]}\b', load[read][1]), load
    dsb = next(i for i, (op, _) in enumerate(boundary) if op == 'dsb')
    isb = next(i for i, (op, _) in enumerate(boundary) if op == 'isb')
    read = next(i for i, (op, _) in enumerate(boundary) if op == 'ldrb')
    assert dsb < isb < read, boundary
    verify_prefix_branches(boundary, read, arm=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--objdump', required=True)
    parser.add_argument('binary')
    args = parser.parse_args()
    output = subprocess.run([args.objdump, '-d', '--no-show-raw-insn', args.binary],
                            capture_output=True, text=True, check=True).stdout
    load = function(output, 'datapump_nospec_load')
    boundary = function(output, 'datapump_barrier_load')
    if any(op.startswith('sbb') for op, _ in load):
        verify_x86(load, boundary)
    elif any(op == 'csel' for op, _ in load):
        verify_aarch64(load, boundary)
    else:
        raise AssertionError(f'no supported nospec mask in witness: {load}')
    print('generated bounds/load dependency and validation barrier verified')


if __name__ == '__main__':
    main()

import io
import os
import struct
import yaml
from bitarray import bitarray
from bitarray.util import int2ba
from shutil import copyfile
import click

def decode_program(f: io.BytesIO):
    p_insts = []
    s_insts = []

    f.seek(60)
    p_inst_count = struct.unpack('I', f.read(4))[0]
    p_inst_offset = struct.unpack('I', f.read(4))[0]
    f.read(p_inst_offset - 4)
    for i in range(p_inst_count):
        inst = struct.unpack('Q', f.read(8))[0]
        p_insts.append(inst)

    f.seek(68)
    s_inst_count = struct.unpack('I', f.read(4))[0]
    s_inst_offset = struct.unpack('I', f.read(4))[0]
    f.read(s_inst_offset - 4)
    for i in range(s_inst_count):
        inst = struct.unpack('Q', f.read(8))[0]
        s_insts.append(inst)

    return {
        'p_insts': p_insts,
        's_insts': s_insts
    }


db = yaml.load(open("grammar.yaml"))

def destruct_member(member):
        name = list(member.keys())[0]
        fields = member[name]
        offset = None
        match = None
        if isinstance(fields, str):
            size = len(fields)
            match = fields
        elif isinstance(fields, int):
            size = fields
        elif 'size' in fields:
            size = fields['size']
            if 'offset' in fields:
                offset = 64 - fields['offset'] - size
            if 'match' in fields:
                match = fields['match']
        
        return (name, size, offset, match)

def find_decoder(inst):
    for key, item in db.items():
        reject = False
        pointer = 0
        for member in item['members']:
            name, size, offset, match = destruct_member(member)
            if offset is not None:
                if offset != pointer:
                    pointer = offset
            if match is not None:
                if inst[pointer:pointer+size].to01() != match:
                    reject = True
            pointer += size
                    
        if not reject:
            return (key, item['members'])
    return None

def generate_fields(insts):
    out = []

    for inst in insts:
        inst_ba = int2ba(inst, 64)
        inst_name, members = find_decoder(inst_ba)
        desc = []
        values = []
        pointer = 0
        for member in members:
            name, size, offset, match = destruct_member(member)
            if offset is not None:
                if offset != pointer:
                    desc.append('-' * (offset - pointer))
                    values.append(inst_ba[pointer:offset].to01())
                    pointer = offset

            if match is not None:
                if match != inst_ba[pointer:pointer+size].to01():
                    print(inst_name + " not matching")
            desc.append(name)
            values.append(inst_ba[pointer:pointer+size].to01())
            pointer += size
        if pointer != 64:
            desc.append('-' * (64 - pointer))
            values.append(inst_ba[pointer:64].to01())
        pad_desc = []
        pad_values = []
        for i in range(len(desc)):
            d = desc[i]
            v = values[i]
            if len(d) > len(v):
                v += ' ' * (len(d) - len(v))
            else:
                d += ' ' * (len(v) - len(d))
            pad_desc.append(d)
            pad_values.append(v)
        out.append(inst_name)
        out.append('|'.join(pad_desc))
        out.append('|'.join(pad_values))
    
    return '\n'.join(out)

def generate_split(program):
    out = ''
    out += 'Primary program start:\n'
    out += generate_fields(program['p_insts']) + '\n'
    out += 'Primary program end:\n'
    out += '\n\n\n'
    out += 'Secondary program start:\n'
    out += generate_fields(program['s_insts']) + '\n'
    out += 'Secondary program end:\n'
    return out

def patch_program(ff, split):
    lines = split.split('\n')
    p_start = 0
    p_end = 0
    s_start = 0
    s_end = 0
    for i in range(len(lines)):
        if lines[i] == 'Primary program start:':
            p_start = i + 1
        elif lines[i] == 'Primary program end:':
            p_end = i
        elif lines[i] == 'Secondary program start:':
            s_start = i + 1
        elif lines[i] == 'Secondary program end:':
            s_end = i
    
    ff.seek(64)
    p_inst_offset = struct.unpack('I', ff.read(4))[0]
    ff.read(p_inst_offset - 4)

    i = p_start
    while i < p_end:
        inst = lines[i+2].replace(' ','').replace('|', '')
        ff.write(struct.pack('Q', int(inst, 2)))
        i += 3

    ff.seek(72)
    s_inst_offset = struct.unpack('I', ff.read(4))[0]
    ff.read(s_inst_offset - 4)
    i = s_start
    while i < s_end:
        inst = lines[i+2].replace(' ','').replace('|', '')
        ff.write(struct.pack('Q', int(inst, 2)))
        i += 3

@click.group()
def gxpsplit():
    pass

@gxpsplit.command()
@click.argument("gxp")
def split(gxp):
    with open(gxp, 'rb') as f:
        program = decode_program(f) 
        out = generate_split(program)
        with open(gxp + '.split', 'w') as ff:
            ff.write(out)

@gxpsplit.command()
@click.argument("gxp")
def patch(gxp):
    with open(gxp + '.split', 'r') as f:
        split = f.read()
        copyfile(gxp, 'patched_' + gxp)
        with open('patched_'+gxp, 'r+b') as ff:
            patch_program(ff, split)

import random

TEST_N = 50
FLOAT_MIN = 0.0
FLOAT_MAX = 1.0
INPUT_N = 144

@gxpsplit.command()
@click.argument("gxp")
def gent(gxp):
    os.mkdir('test')
    os.mkdir('test/gxp')
    os.mkdir('test/input')
    os.mkdir('test/res')

    for i in range(50):
        copyfile(gxp, 'test/gxp/' + gxp + str(i))
        with open('test/input/' + gxp + str(i) + '.txt', 'w') as f:
            out = []
            for j in range(INPUT_N):
                ip = random.uniform(FLOAT_MIN, FLOAT_MAX)
                out.append(str(ip))
            f.write('\n'.join(out) + '\n')

if __name__ == '__main__':
    gxpsplit()

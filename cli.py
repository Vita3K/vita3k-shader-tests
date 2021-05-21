
import click
import pathlib
import os
import signal
import subprocess
import shutil
import time
from threading import Thread

VITA3K_PATH = "C:\\Users\\sunho\\Documents\\dev\\Vita3K\\build-windows\\bin\\RelWithDebInfo"
VITA3K_ENV_PATH = os.environ.get('VITA3K_DATA')

print("VITA3K_PATH: ", VITA3K_PATH)
print("VITA3K_DATA: ", VITA3K_ENV_PATH)

@click.group()
def ftvg():
    pass

def waiter_thread(testname):
    while True:
        from os import listdir
        from os.path import isfile, join
        mypath = os.path.join('tests', testname, 'res')
        ftvg_path = os.path.join(VITA3K_ENV_PATH, 'ux0', 'data', 'ftvg')
        onlyfiles = [f for f in listdir(mypath) if isfile(join(mypath, f))]
        
        noexit = False        
        for file in onlyfiles:
            if not os.path.exists(os.path.join(ftvg_path, 'res', file)):
                noexit = True

        if not noexit:
            return
        time.sleep(0.5)

import re

CRITERIA = 3
import struct

vertex_pat = re.compile(r'vertex ([0-9]+):\n(o.+)')
o_reg_pat = re.compile(r'o([0-9]+): ([0-9a-z]+)')

vertex_pat2 = re.compile(r'vertex ([0-9]+):\n(p.+)')
pa_reg_pat = re.compile(r'pa([0-9]+): ([0-9a-z]+)')

def get_pa_regs(text):
    out = dict()
    for (vnum, oregs) in re.findall(vertex_pat2, text):
        item = dict()
        for (onum, val) in re.findall(pa_reg_pat, oregs):
            if val == '0':
                item[onum] = 0
            else:
                item[onum] = struct.unpack('!f', bytes.fromhex(val))[0]
        out[vnum] = item
    return out

def get_o_regs(text):
    out = dict()
    for (vnum, oregs) in re.findall(vertex_pat, text):
        item = dict()
        for (onum, val) in re.findall(o_reg_pat, oregs):
            item[onum] = int(val, base=16)
        out[vnum] = item
    return out

@ftvg.command() 
@click.argument('testname')
def test(testname):
    ftvg_path = os.path.join(VITA3K_ENV_PATH, 'ux0', 'data', 'ftvg')
    if os.path.exists(ftvg_path):
        shutil.rmtree(ftvg_path)
    os.mkdir(ftvg_path)
    shutil.copytree(os.path.join('tests', testname, 'gxp'), os.path.join(ftvg_path, 'gxp'))
    shutil.copytree(os.path.join('tests', testname, 'input'), os.path.join(ftvg_path, 'input'))
    os.mkdir(os.path.join(ftvg_path, 'res'))

    pro = subprocess.Popen([os.path.join(VITA3K_PATH, 'Vita3K'), '-r', 'FTVGWK000'], cwd=VITA3K_PATH)

    waiter = Thread(target=waiter_thread, args=(testname,))
    waiter.start()
    waiter.join()

    from os import listdir
    from os.path import isfile, join
    mypath = os.path.join('tests', testname, 'res')
    ftvg_path = os.path.join(VITA3K_ENV_PATH, 'ux0', 'data', 'ftvg')
    onlyfiles = [f for f in listdir(mypath) if isfile(join(mypath, f))]

    maxo = 1000
    if os.path.exists(os.path.join('tests', testname, 'regs.txt')):
        with open(os.path.join('tests', testname, 'regs.txt')) as f:
            maxo = int(f.read().replace('o0-o', ''))
    total = 0
    pass_total = 0
    for file in onlyfiles:
        wrongs = list()
        passed = True
        with open(os.path.join(ftvg_path, 'res', file)) as f:
            with open(os.path.join(mypath, file)) as f2:
                buf = f.read()
                calculated = get_o_regs(buf)
                expexted = get_o_regs(f2.read())
                inputs = get_pa_regs(buf)
                # for i in range(0,4):
                #     for j in range(8,21):
                #         wrongs.append('vertex {} pa{}: {}'.format(i, j, inputs[str(i)][str(j)]))
#pa0.xyzw, pa8.xyzw, pa12.xyzw, pa16.xyzw
                for v, item in calculated.items():
                    for o, val in item.items():
                        if int(o) > maxo:
                            break
                        if abs(val - expexted[v][o]) > CRITERIA:
                            passed = False
                            wrongs.append('vertex {} o{} calculated: {} expexted: {}'.format(v, o, val, expexted[v][o]))
        total += 1
        if not passed:
            print('test {} did not pass\n------------------\n{}\n\n'.format(file, '\n'.join(wrongs)))
        else:
            pass_total += 1
    print('{} passed in {} test cases'.format(pass_total, total))
    pro.send_signal(signal.CTRL_BREAK_EVENT)
    pro.kill()

if __name__ == '__main__':
    ftvg()

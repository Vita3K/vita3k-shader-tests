# gxpsplit

Simple gxp patcher.

## Prerequisites

```
pip install click bitarray pyyaml
```

## How to use

1. "split" gxp file
```
python gxpsplit.py split name.gxp
```

This will create "name.gxp.split" file. Its content is like this:
```
Primary program start:
PHAS
op1  |op2|sprvv|phas|end|imm|src1_bank_ext|src2_bank_ext|--|mode|rate_hi|rate_lo_or_nosched|wait_cond|temp_count|src1_bank|src2_bank|--------|exe_addr_high|src1_n_or_exe_addr_mid|src2_n_or_exe_addr_low
11111|010|0    |100 |0  |1  |0            |0            |00|0   |0      |0                 |111      |00000000  |00       |00       |00000000|000000       |0000000               |0000000               
V32NMAD
opcode1|pred|skipinv|src1_swiz_10_11|syncstart|dest_bank_ext|src1_swiz_9|src1_bank_ext|src2_bank_ext|src2_swiz|nosched|dest_mask|src1_mod|src2_mod|src1_swiz_7_8|dest_bank_sel|src1_bank_sel|src2_bank_sel|dest_n|src1_swiz_0_6|op2|src1_n|src2_n
00001  |000 |1      |01             |0        |0            |1          |0            |0            |0100     |1      |0011     |00      |0       |01           |01           |10           |10           |000000|0001000      |001|001100|001010
```
Edit bit fields as you much as you want. You can just even if bits line doesn't contain | or spaces, it works. (e.g. 10101101010...)

2. Generate patched program
```
python gxpsplit.py patch name.gxp
```

This will generate patched_name.gxp patched according to name.gxp.split file.
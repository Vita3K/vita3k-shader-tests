# Invalid instructions

## What happens?

When invalid instruction is executed, vita either driver crashes or discard the pixel.

## Out of bound registers

This is an interesting case. When instruction accesses output registers that are not used as output of program or primary registers that are not loaded by input parameters, it discards the pixel. E.g. if some program has 2 float output vertex attributes and instruction tries to write to o9, it discards the pixel.

## End field

Sometimes instruction has "end" field. If this is flagged in the middle instruction, vita driver crashes.

## PACK

1)

F32 xyzw <- xyzw does not work. (maybe max data transferable is two words)

2)

F32 xy <- F16 xy
F32 --zw <- F16 --zw 

In this sequence the second one is broken.

F32 xy <- F16 xy
F32 xy <- f16 zw

Use this. (dest mask keeps 0011, comp_sel0 = 10 comp_sel1 = 11, src2 = next register)

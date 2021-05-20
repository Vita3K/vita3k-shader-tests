# Invalid instructions

## What happens?

When invalid instruction is executed, vita either driver crashes or discard the pixel.

## Out of bound registers

This is an interesting case. When instruction accesses output registers that are not used as output of program or primary registers that are not loaded by input parameters, it discards the pixel. E.g. if some program has 2 float output vertex attributes and instruction tries to write to o9, it discards the pixel.

## End field

Sometimes instruction has "end" field. If this is flagged in the middle instruction, vita driver crashes.

## PCKF32F32 
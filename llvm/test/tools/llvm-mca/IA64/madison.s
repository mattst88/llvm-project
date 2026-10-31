# RUN: llvm-mca -mtriple=ia64-unknown-linux-gnu -mcpu=itanium2 -resource-pressure=false \
# RUN:   -instruction-info=false -iterations=100 < %s | FileCheck %s --check-prefix=I2
# RUN: llvm-mca -mtriple=ia64-unknown-linux-gnu -mcpu=madison -resource-pressure=false \
# RUN:   -instruction-info=false -iterations=100 < %s | FileCheck %s --check-prefix=MAD

# Madison issues shifts, extracts and popcnt only on I0, but multimedia shifts
# on either I port.
# I2: Block RThroughput: 1.0
# MAD: Block RThroughput: 2.0

shr.u r14 = r32, 3
shr.u r15 = r33, 5

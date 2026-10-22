# RUN: llvm-mca -mtriple=ia64-unknown-linux-gnu -mcpu=itanium2 -iterations=100 \
# RUN:   -resource-pressure=false -instruction-info=false < %s | FileCheck %s

# A general register's latency depends on what reads it. Each region is a
# chain through itself, so it takes its latency in cycles an iteration; the
# 9340 takes as many (ia64probe's lat_add_shl, lat_mux2 and lat_pmpy2,
# lat_ld8 and lat_shl_ld, and lat_ld2_pmpy_z).

# An ALU result reaches a multimedia op in three cycles, and that op's result
# an ALU in three.
# CHECK-LABEL: Code Region - alu-mm-alu
# CHECK:       Total Cycles: 601
# LLVM-MCA-BEGIN alu-mm-alu
add r32 = r32, r33
shl r32 = r32, r34
# LLVM-MCA-END

# One multimedia op reaches another in two.
# CHECK-LABEL: Code Region - mm-mm
# CHECK:       Total Cycles: 402
# LLVM-MCA-BEGIN mm-mm
pmpy2.r r32 = r32, r33
mux1 r32 = r32, @rev
# LLVM-MCA-END

# An integer load reaches an ALU in one cycle, an address in two and a
# multimedia op in three.
# CHECK-LABEL: Code Region - load-address
# CHECK:       Total Cycles: 200
# LLVM-MCA-BEGIN load-address
ld8 r32 = [r32]
# LLVM-MCA-END

# CHECK-LABEL: Code Region - load-mm-alu
# CHECK:       Total Cycles: 701
# LLVM-MCA-BEGIN load-mm-alu
ld8 r35 = [r32]
pmpy2.r r35 = r35, r35
add r32 = r32, r35
# LLVM-MCA-END

# A multimedia result used as an address takes sixteen cycles, the core
# replaying the load.
# CHECK-LABEL: Code Region - mm-address
# CHECK:       Total Cycles: 1899
# LLVM-MCA-BEGIN mm-address
shl r32 = r32, r33
ld8 r32 = [r32]
# LLVM-MCA-END

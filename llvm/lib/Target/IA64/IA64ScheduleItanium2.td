//===-- IA64ScheduleItanium2.td - Itanium 2 scheduling -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Itanium 2 issues in order, up to two bundles per cycle. It has four
// memory ports, two integer ports, two FP ports and three branch ports; an
// instruction that can run on either an M or an I unit can use any of the
// first six. Loads go to the first two memory ports and stores to the last
// two. Only the multimedia multiplies and the moves to and from branch,
// predicate and I-unit application registers need the first I port: the
// Itanium 2 issued shifts, extracts, deposits, bit tests and popcnt only
// there too, but Montecito and its successors issue them on either.
//
//===----------------------------------------------------------------------===//

def Itanium2Model : SchedMachineModel {
  let IssueWidth = 6;
  let MicroOpBufferSize = 0; // In order.
  let LoadLatency = 1;
  let MispredictPenalty = 6;
  let CompleteModel = 1;
}

let SchedModel = Itanium2Model in {

let BufferSize = 0 in {
def I2PortM0 : ProcResource<1>;
def I2PortM1 : ProcResource<1>;
def I2PortM2 : ProcResource<1>;
def I2PortM3 : ProcResource<1>;
def I2PortI0 : ProcResource<1>;
def I2PortI1 : ProcResource<1>;
def I2PortF  : ProcResource<2>;
def I2PortB  : ProcResource<3>;

def I2PortM01 : ProcResGroup<[I2PortM0, I2PortM1]>;
def I2PortM23 : ProcResGroup<[I2PortM2, I2PortM3]>;
def I2PortM   : ProcResGroup<[I2PortM0, I2PortM1, I2PortM2, I2PortM3]>;
def I2PortI   : ProcResGroup<[I2PortI0, I2PortI1]>;
def I2PortA   : ProcResGroup<[I2PortM0, I2PortM1, I2PortM2, I2PortM3,
                              I2PortI0, I2PortI1]>;
}

// One instruction on one port, with the given result latency.
class I2Write<SchedWrite write, ProcResourceKind port, int latency>
    : WriteRes<write, [port]> {
  let Latency = latency;
}

// An additional result of an instruction, at no extra cost.
class I2Also<SchedWrite write, int latency> : WriteRes<write, []> {
  let Latency = latency;
  let NumMicroOps = 0;
}

def : I2Write<WriteALU,   I2PortA, 1>;
def : I2Write<WriteICmp,  I2PortA, 1>;
def : I2Write<WriteMMALU, I2PortA, 3>;
def : I2Also<WritePred2, 1>;

// A copy is an add of zero or an fmerge of a register with itself; the
// integer one is far more common.
def : InstRW<[WriteALU], (instrs COPY)>;

def : I2Write<WriteMMMul,    I2PortI0, 3>;
def : I2Write<WriteMMShift,  I2PortI,  3>;
def : I2Write<WriteMMShiftI, I2PortI,  3>;
def : I2Write<WriteShift,    I2PortI,  1>;
def : I2Write<WriteTBit,     I2PortI,  1>;
def : I2Write<WriteExt,      I2PortI,  1>;
def : I2Write<WriteToBR,     I2PortI0, 1>;
def : I2Write<WriteFromBR,   I2PortI0, 2>;
def : I2Write<WriteToPR,     I2PortI0, 1>;
def : I2Write<WriteFromPR,   I2PortI0, 2>;

// A 64-bit constant fills two slots of its bundle.
def : I2Write<WriteMovL, I2PortI, 1> {
  let NumMicroOps = 2;
}

// ar.pfs, ar.lc and ar.ec are the I-unit application registers, and the
// loop counters among them are quick to move to and from. The others cost
// as the M-unit ones do.
class I2IsAR<int op, list<Register> regs>
    : MCSchedPredicate<CheckAny<!foreach(r, regs, CheckRegOperand<op, r>)>>;
def I2IsToLoopAR : I2IsAR<0, [AR65, AR66]>;
def I2IsToIAR : I2IsAR<0, [AR64]>;
def I2IsFromLoopAR : I2IsAR<1, [AR65, AR66]>;
def I2IsFromIAR : I2IsAR<1, [AR64]>;
def I2ToLoopAR : SchedWriteRes<[I2PortI0]> { let Latency = 1; }
def I2ToIAR : SchedWriteRes<[I2PortI0]> { let Latency = 5; }
def I2ToMAR : SchedWriteRes<[I2PortM2]> { let Latency = 5; }
def I2FromLoopAR : SchedWriteRes<[I2PortI0]> { let Latency = 2; }
def I2FromIAR : SchedWriteRes<[I2PortI0]> { let Latency = 6; }
def I2FromMAR : SchedWriteRes<[I2PortM2]> { let Latency = 6; }
def : SchedAlias<WriteToAR, SchedWriteVariant<[
  SchedVar<I2IsToLoopAR, [I2ToLoopAR]>,
  SchedVar<I2IsToIAR, [I2ToIAR]>,
  SchedVar<NoSchedPred, [I2ToMAR]>]>>;
def : SchedAlias<WriteFromAR, SchedWriteVariant<[
  SchedVar<I2IsFromLoopAR, [I2FromLoopAR]>,
  SchedVar<I2IsFromIAR, [I2FromIAR]>,
  SchedVar<NoSchedPred, [I2FromMAR]>]>>;
def : I2Write<WriteToLC, I2PortI0, 1>;

def : I2Write<WriteLoad,     I2PortM01, 1>;
def : I2Write<WriteStore,    I2PortM23, 1>;
def : I2Write<WriteFLoad,    I2PortM01, 6>;
def : I2Write<WriteFStore,   I2PortM23, 1>;
def : I2Write<WritePrefetch, I2PortM01, 1>;
def : I2Write<WriteSem,      I2PortM23, 2>;
def : I2Write<WriteChk,      I2PortM,   1>;
def : I2Write<WriteAlloc,    I2PortM2,  1>;
def : I2Also<WriteFLoad2,  6>;
def : I2Also<WritePostInc, 1>;

// The latencies above are to an ALU. A multimedia op waits two cycles more
// for an ALU or an integer load and one more for a getf or a move from a
// branch, predicate or application register, but one fewer for another
// multimedia op. An address waits one more for a load or a getf; there is
// no bypass from the multimedia units to an address, so it waits thirteen
// more for them, while the core flushes and replays the access (308065-001
// Table 2-8, and ia64probe's lat_ld8_shl and lat_shl_ld: 19 cycles each).
defvar I2ALUWrites = [WriteALU, WriteICmp, WriteSelect, WriteShift, WriteExt,
                      WriteMovL, WritePostInc, WriteLoad];
defvar I2MMWrites = [WriteMMALU, WriteMMMul, WriteMMShift, WriteMMShiftI];
defvar I2IEU2Writes = [WriteGetF, WriteFromBR, WriteFromPR, WriteFromAR,
                       WriteAlloc];
def : ReadAdvance<ReadMM, 0, !listconcat(I2ALUWrites, I2MMWrites,
                                         I2IEU2Writes),
                  !listconcat(!listsplat(-2, !size(I2ALUWrites)),
                              !listsplat(1, !size(I2MMWrites)),
                              !listsplat(-1, !size(I2IEU2Writes)))>;
def : ReadAdvance<ReadAddr, 0, !listconcat([WriteLoad, WriteGetF],
                                           I2MMWrites),
                  !listconcat([-1, -1], !listsplat(-13, !size(I2MMWrites)))>;

def : I2Write<WriteSetF, I2PortM23, 6>;
def : I2Write<WriteGetF, I2PortM2,  5>;

def : I2Write<WriteFMA,    I2PortF, 4>;
def : I2Write<WriteXMA,    I2PortF, 4>;
def : I2Write<WriteFMisc,  I2PortF, 4>;
def : I2Write<WriteFCmp,   I2PortF, 2>;
def : I2Write<WriteFRecip, I2PortF, 4>;
def : I2Write<WriteFCvt,   I2PortF, 4>;
def : I2Also<WriteFPred2,     2>;
def : I2Also<WriteFRecipPred, 4>;

// A division is frcpa plus nine fmas in single precision and twelve in
// double, up to three of which can issue in parallel: the dependence chain is
// six instructions long in single precision and seven in double. These are
// the double-precision numbers.
def : I2Write<WriteFDiv, I2PortF, 28> {
  let NumMicroOps = 13;
}

// A division by a constant is frcpa and three fmas with a chain of three.
def : I2Write<WriteFDivConst, I2PortF, 12> {
  let NumMicroOps = 4;
}

// An integer division is two conversions to FP, frcpa, seven fmas and the
// conversion back, with a chain of nine instructions.
def : I2Write<WriteIDiv, I2PortF, 36> {
  let NumMicroOps = 11;
}

// With 32-bit integers, four fmas with a chain of three.
def : I2Write<WriteIDiv32, I2PortF, 24> {
  let NumMicroOps = 8;
}

def : I2Write<WriteBr,  I2PortB, 1>;
def : I2Write<WriteSys, I2PortM, 1>;

// A nop takes its slot and no port.
def : WriteRes<WriteNop, []>;

def : I2Write<WriteSelect, I2PortA, 2> {
  let NumMicroOps = 3;
}
def : I2Write<WriteFSelect, I2PortF, 3> {
  let NumMicroOps = 3;
}

} // SchedModel = Itanium2Model

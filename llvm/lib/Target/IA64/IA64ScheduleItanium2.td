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
// two, and many I-unit-only integer instructions need the first I port.
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
def : I2Write<WriteMMALU, I2PortA, 2>;
def : I2Also<WritePred2, 1>;

// A copy is an add of zero or an fmerge of a register with itself; the
// integer one is far more common.
def : InstRW<[WriteALU], (instrs COPY)>;

def : I2Write<WriteMMMul,    I2PortI0, 2>;
def : I2Write<WriteMMShift,  I2PortI,  2>;
def : I2Write<WriteMMShiftI, I2PortI,  1>;
def : I2Write<WriteShift,    I2PortI0, 1>;
def : I2Write<WriteTBit,     I2PortI0, 1>;
def : I2Write<WriteExt,      I2PortI,  1>;
def : I2Write<WriteToBR,     I2PortI0, 1>;
def : I2Write<WriteFromBR,   I2PortI0, 2>;
def : I2Write<WriteToPR,     I2PortI0, 1>;
def : I2Write<WriteFromPR,   I2PortI0, 2>;

// A 64-bit constant fills two slots of its bundle.
def : I2Write<WriteMovL, I2PortI, 1> {
  let NumMicroOps = 2;
}

// These are the M-unit costs, which apply to the application registers the
// compiler uses on its own; ar.pfs and ar.lc only matter in prologues and
// counted loops.
def : I2Write<WriteToAR,   I2PortM2, 5>;
def : I2Write<WriteFromAR, I2PortM2, 6>;

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

def : I2Write<WriteSetF, I2PortM23, 5>;
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

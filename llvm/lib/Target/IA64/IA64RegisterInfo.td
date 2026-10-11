//===-- IA64RegisterInfo.td - IA-64 Register defs ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
// The IA-64 application register state that instructions name directly: 128
// general registers, 128 floating-point registers, 64 one-bit predicates and
// eight branch registers.
//
//===----------------------------------------------------------------------===//

class IA64Reg<bits<16> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "IA64";
  let AltNames = alt;
}

// DWARF register numbers follow the IA-64 psABI convention that GCC, gdb and
// libunwind share: general 0-127, floating-point 128-255, predicate 256-319,
// branch 320-327.

// General registers. r0 reads as zero, r1 is the global pointer, r12 the
// memory stack pointer and r13 the thread pointer. r32-r127 are the stacked
// registers, renamed through the register stack frame on every call.
foreach i = 0-127 in
  def R#i : IA64Reg<i, "r"#i,
                    !cond(!eq(i, 1): ["gp"], !eq(i, 12): ["sp"],
                          !eq(i, 13): ["tp"], true: [])>,
            DwarfRegNum<[i]>;

// Floating-point registers. f0 reads as +0.0 and f1 as +1.0.
foreach i = 0-127 in
  def F#i : IA64Reg<i, "f"#i>, DwarfRegNum<[!add(i, 128)]>;

// Predicate registers. p0 reads as 1.
foreach i = 0-63 in
  def P#i : IA64Reg<i, "p"#i>, DwarfRegNum<[!add(i, 256)]>;

// Branch registers. b0 holds the return address.
foreach i = 0-7 in
  def B#i : IA64Reg<i, "b"#i, !if(!eq(i, 0), ["rp"], [])>,
            DwarfRegNum<[!add(i, 320)]>;

// Application registers. The architected ones are known by name; the rest
// only by number.
class ARName<int i> {
  string Name = !cond(
    !lt(i, 8):   "ar.k"#i,
    !eq(i, 16):  "ar.rsc",      !eq(i, 17):  "ar.bsp",
    !eq(i, 18):  "ar.bspstore", !eq(i, 19):  "ar.rnat",
    !eq(i, 21):  "ar.fcr",      !eq(i, 24):  "ar.eflag",
    !eq(i, 25):  "ar.csd",      !eq(i, 26):  "ar.ssd",
    !eq(i, 27):  "ar.cflg",     !eq(i, 28):  "ar.fsr",
    !eq(i, 29):  "ar.fir",      !eq(i, 30):  "ar.fdr",
    !eq(i, 32):  "ar.ccv",      !eq(i, 36):  "ar.unat",
    !eq(i, 40):  "ar.fpsr",     !eq(i, 44):  "ar.itc",
    !eq(i, 45):  "ar.ruc",      !eq(i, 64):  "ar.pfs",
    !eq(i, 65):  "ar.lc",       !eq(i, 66):  "ar.ec",
    true:        "ar"#i);
}
foreach i = 0-127 in
  def AR#i : IA64Reg<i, ARName<i>.Name,
                     !if(!eq(ARName<i>.Name, "ar"#i), [], ["ar"#i])>,
             DwarfRegNum<[!add(i, 334)]>;

// Control registers.
class CRName<int i> {
  string Name = !cond(
    !eq(i, 0):   "cr.dcr",      !eq(i, 1):   "cr.itm",
    !eq(i, 2):   "cr.iva",      !eq(i, 8):   "cr.pta",
    !eq(i, 9):   "cr.gpta",
    !eq(i, 16):  "cr.ipsr",     !eq(i, 17):  "cr.isr",
    !eq(i, 19):  "cr.iip",      !eq(i, 20):  "cr.ifa",
    !eq(i, 21):  "cr.itir",     !eq(i, 22):  "cr.iipa",
    !eq(i, 23):  "cr.ifs",      !eq(i, 24):  "cr.iim",
    !eq(i, 25):  "cr.iha",      !eq(i, 26):  "cr.iib0",
    !eq(i, 27):  "cr.iib1",     !eq(i, 64):  "cr.lid",
    !eq(i, 65):  "cr.ivr",      !eq(i, 66):  "cr.tpr",
    !eq(i, 67):  "cr.eoi",      !eq(i, 68):  "cr.irr0",
    !eq(i, 69):  "cr.irr1",     !eq(i, 70):  "cr.irr2",
    !eq(i, 71):  "cr.irr3",     !eq(i, 72):  "cr.itv",
    !eq(i, 73):  "cr.pmv",      !eq(i, 74):  "cr.cmcv",
    !eq(i, 80):  "cr.lrr0",     !eq(i, 81):  "cr.lrr1",
    true:        "cr"#i);
}
foreach i = 0-127 in
  def CR#i : IA64Reg<i, CRName<i>.Name,
                     !if(!eq(CRName<i>.Name, "cr"#i), [], ["cr"#i])>;

// The stacked registers as codegen sees them. A register stack frame is the
// inputs and locals followed by the outputs, and where the outputs start is
// not known until after register allocation. So r32-r119 are the inputs and
// locals as themselves, and r120-r127 stand for the eight outputs, renamed
// at emission. That is the maximum frame size: 96 registers.
//
// Allocation order: scratch registers first, then the stacked registers,
// which the RSE preserves for free, then the callee-saved static registers,
// which must be spilled, with the fixed-purpose registers last.
def GR : RegisterClass<"IA64", [i64], 64, (add
  (sequence "R%u", 14, 31),    // scratch
  (sequence "R%u", 8, 11),     // return values
  R2, R3,                      // scratch; also addl's only base registers
  (sequence "R%u", 32, 119),   // inputs and locals
  (sequence "R%u", 120, 127),  // outputs
  (sequence "R%u", 4, 7),      // preserved
  R0, R1, R12, R13)>;          // zero, gp, sp, tp (reserved)

def FR : RegisterClass<"IA64", [f64, f32], 64, (add
  (sequence "F%u", 6, 7),      // scratch
  (sequence "F%u", 8, 15),     // arguments and return values
  (sequence "F%u", 32, 127),   // scratch
  (sequence "F%u", 2, 5),      // preserved
  (sequence "F%u", 16, 31),    // preserved
  F0, F1)>;                    // 0.0 and 1.0 (reserved)

// Valid fma addends. With f0 as the addend the instruction is a plain
// multiply, and a zero product keeps its own sign, whereas adding 0.0 would
// make it positive.
def FRAddend : RegisterClass<"IA64", [f64, f32], 64, (sub FR, F0)>;

def PR : RegisterClass<"IA64", [i1], 8, (add
  (sequence "P%u", 6, 15),     // scratch
  (sequence "P%u", 1, 5),      // preserved
  (sequence "P%u", 16, 63),    // preserved (rotating)
  P0)> {                       // always 1 (reserved)
  let Size = 8;
  // No value lives in a predicate: a compare writes a predicate pair and its
  // users read it immediately.
  let isAllocatable = 0;
}

def BR : RegisterClass<"IA64", [i64], 64, (add
  (sequence "B%u", 6, 7),      // scratch
  (sequence "B%u", 1, 5),      // preserved
  B0)>;                        // return address

// The only base registers addl accepts.
def GR0_3 : RegisterClass<"IA64", [i64], 64, (add R2, R3, R0, R1)>;

// The application and control registers are never allocated; the classes
// exist for the assembler and disassembler.
let isAllocatable = 0 in {
def AR : RegisterClass<"IA64", [i64], 64, (sequence "AR%u", 0, 127)>;
def CR : RegisterClass<"IA64", [i64], 64, (sequence "CR%u", 0, 127)>;
}

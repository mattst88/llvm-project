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

// Allocation order: the scratch registers first, then the preserved static
// ones, then the stacked registers, with the fixed-purpose registers last.
def GR : RegisterClass<"IA64", [i64], 64, (add
  (sequence "R%u", 14, 31),    // scratch
  (sequence "R%u", 8, 11),     // return values
  R2, R3,                      // scratch; also addl's only base registers
  (sequence "R%u", 4, 7),      // preserved
  (sequence "R%u", 32, 127),   // stacked
  R0, R1, R12, R13)>;          // zero, gp, sp, tp (reserved)

def FR : RegisterClass<"IA64", [f64, f32], 64, (add
  (sequence "F%u", 6, 7),      // scratch
  (sequence "F%u", 8, 15),     // arguments and return values
  (sequence "F%u", 32, 127),   // scratch
  (sequence "F%u", 2, 5),      // preserved
  (sequence "F%u", 16, 31),    // preserved
  F0, F1)>;                    // 0.0 and 1.0 (reserved)

def PR : RegisterClass<"IA64", [i1], 8, (add
  (sequence "P%u", 6, 15),     // scratch
  (sequence "P%u", 1, 5),      // preserved
  (sequence "P%u", 16, 63),    // preserved (rotating)
  P0)> {                       // always 1 (reserved)
  let Size = 8;
}

def BR : RegisterClass<"IA64", [i64], 64, (add
  (sequence "B%u", 6, 7),      // scratch
  (sequence "B%u", 1, 5),      // preserved
  B0)>;                        // return address

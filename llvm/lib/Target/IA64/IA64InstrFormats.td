//===-- IA64InstrFormats.td - IA64 instruction formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An IA-64 instruction occupies one 41-bit slot of a 128-bit bundle. Every
// instruction carries a 4-bit major opcode in bits 40-37, and nearly every one
// a qualifying predicate in bits 5-0. Only the base class is provided here;
// the formats of the A, I, M, B, F and X instruction types are added as the
// instruction set is.
//
//===----------------------------------------------------------------------===//

class IA64Inst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  field bits<41> Inst;
  // A placeholder for fields an instruction does not set, so the disassembler
  // decoder tables can be generated once real encodings exist.
  field bits<41> SoftFail = 0;

  let Namespace = "IA64";

  bits<4> Opcode = 0;
  let Inst{40-37} = Opcode;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

class IA64Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : IA64Inst<outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

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

// The execution unit an instruction needs, which decides the bundle slots it
// may occupy. A-unit instructions run on either an I or an M unit; an X-unit
// instruction takes the L and X slots of an MLX bundle.
class IA64Unit<bits<3> val> {
  bits<3> Value = val;
}
def UnitNone : IA64Unit<0>;
def UnitA    : IA64Unit<1>;
def UnitI    : IA64Unit<2>;
def UnitM    : IA64Unit<3>;
def UnitF    : IA64Unit<4>;
def UnitB    : IA64Unit<5>;
def UnitX    : IA64Unit<6>;

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

// The qualifying predicate. It is the last operand of every instruction that
// has one and defaults to p0, which is always true. Assembly writes it as a
// "(pN)" prefix, so the instruction printer handles it before the mnemonic.
def QPAsmOperand : AsmOperandClass {
  let Name = "QP";
  let RenderMethod = "addRegOperands";
  let PredicateMethod = "isQP";
}
def qp : PredicateOperand<i1, (ops PR), (ops P0)> {
  let PrintMethod = "printQP";
  let ParserMatchClass = QPAsmOperand;
  let DecoderMethod = "DecodePRRegisterClass";
}

class SImmAsmOperand<int width, list<AsmOperandClass> supers = []>
    : AsmOperandClass {
  let Name = "SImm"#width;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isSImm<"#width#">";
  let SuperClasses = supers;
  let DiagnosticType = "InvalidSImm"#width;
}
def SImm22AsmOperand : SImmAsmOperand<22>;
def SImm14AsmOperand : SImmAsmOperand<14, [SImm22AsmOperand]>;
def SImm8AsmOperand  : SImmAsmOperand<8, [SImm14AsmOperand]>;

class SImm<int width, AsmOperandClass cls> : Operand<i64> {
  let ParserMatchClass = cls;
  let DecoderMethod = "decodeSImm<"#width#">";
  let OperandType = "OPERAND_IMMEDIATE";
}
def simm8  : SImm<8, SImm8AsmOperand>;
def simm14 : SImm<14, SImm14AsmOperand>;
def simm22 : SImm<22, SImm22AsmOperand>;

// A shift count of 1 to Max, encoded less one.
class CountAsmOperand<int max> : AsmOperandClass {
  let Name = "Count1To"#max;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmInRange<1, "#max#">";
  let DiagnosticType = "InvalidCount1To"#max;
}
class Count<int max> : Operand<i64> {
  let ParserMatchClass = CountAsmOperand<max>;
  let EncoderMethod = "getImmMinus1OpValue";
  let DecoderMethod = "decodeImmPlus1";
  let OperandType = "OPERAND_IMMEDIATE";
}
def count2   : Count<4>;
// Multimedia shift-and-add does not allow a count of 4.
def count2mm : Count<3>;

//===----------------------------------------------------------------------===//
// Instruction classes
//===----------------------------------------------------------------------===//

// An instruction occupies one 41-bit bundle slot. Bits 40:37 hold the major
// opcode, and bits 5:0 the qualifying predicate where there is one.
class IA64Inst<dag outs, dag ins, string mnemonic, string operands,
               list<dag> pattern>
    : Instruction {
  field bits<41> Inst;
  field bits<41> SoftFail = 0;
  let Namespace = "IA64";

  bits<4> Opcode = 0;
  let Inst{40-37} = Opcode;

  IA64Unit Unit = UnitNone;
  let TSFlags{2-0} = Unit.Value;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = mnemonic # !if(!empty(operands), "", "\t" # operands);
  let Pattern = pattern;
}

// An instruction with a qualifying predicate.
class IA64QPInst<dag oops, dag iops, string mnemonic, string operands,
                 list<dag> pattern>
    : IA64Inst<oops, !con(iops, (ins qp:$qp)), mnemonic, operands, pattern> {
  bits<6> qp;
  let Inst{5-0} = qp;
  let AsmString = mnemonic # "${qp}" #
                  !if(!empty(operands), "", "\t" # operands);
}

class IA64Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : IA64Inst<outs, ins, asmstr, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// A-unit formats (SDM volume 3, section 4.2)
//===----------------------------------------------------------------------===//

class AInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let Unit = UnitA;
  let DecoderNamespace = "A";
}

// A1: integer ALU, register-register.
class A1<bits<4> x4, bits<2> x2b, string mnemonic,
         string operands = "$r1 = $r2, $r3">
    : AInst<(outs GR:$r1), (ins GR:$r2, GR:$r3), mnemonic, operands> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  let Opcode = 8;
  let Inst{36-33} = 0;
  let Inst{32-29} = x4;
  let Inst{28-27} = x2b;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// A2: shift left and add.
class A2<bits<4> x4, string mnemonic>
    : AInst<(outs GR:$r1), (ins GR:$r2, count2:$count, GR:$r3), mnemonic,
            "$r1 = $r2, $count, $r3"> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  bits<2> count;
  let Opcode = 8;
  let Inst{36-33} = 0;
  let Inst{32-29} = x4;
  let Inst{28-27} = count;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// A3: integer ALU, immediate8-register.
class A3<bits<4> x4, bits<2> x2b, string mnemonic>
    : AInst<(outs GR:$r1), (ins simm8:$imm, GR:$r3), mnemonic,
            "$r1 = $imm, $r3"> {
  bits<7> r1;
  bits<7> r3;
  bits<8> imm;
  let Opcode = 8;
  let Inst{36} = imm{7};
  let Inst{35-33} = 0;
  let Inst{32-29} = x4;
  let Inst{28-27} = x2b;
  let Inst{26-20} = r3;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// A4: add immediate14.
class A4<bits<2> x2a, string mnemonic>
    : AInst<(outs GR:$r1), (ins simm14:$imm, GR:$r3), mnemonic,
            "$r1 = $imm, $r3"> {
  bits<7> r1;
  bits<7> r3;
  bits<14> imm;
  let Opcode = 8;
  let Inst{36} = imm{13};
  let Inst{35-34} = x2a;
  let Inst{33} = 0;
  let Inst{32-27} = imm{12-7};
  let Inst{26-20} = r3;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// A5: add immediate22. The base register field is two bits wide.
class A5<string mnemonic>
    : AInst<(outs GR:$r1), (ins simm22:$imm, GR0_3:$r3), mnemonic,
            "$r1 = $imm, $r3"> {
  bits<7> r1;
  bits<2> r3;
  bits<22> imm;
  let Opcode = 9;
  let Inst{36} = imm{21};
  let Inst{35-27} = imm{15-7};
  let Inst{26-22} = imm{20-16};
  let Inst{21-20} = r3;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// A6: integer compare, register-register.
class A6<bits<4> opcode, bits<2> x2, bit ta, bit c, string mnemonic>
    : AInst<(outs PR:$p1, PR:$p2), (ins GR:$r2, GR:$r3), mnemonic,
            "$p1, $p2 = $r2, $r3"> {
  bits<6> p1;
  bits<6> p2;
  bits<7> r2;
  bits<7> r3;
  let Opcode = opcode;
  let Inst{36} = 0;
  let Inst{35-34} = x2;
  let Inst{33} = ta;
  let Inst{32-27} = p2;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12} = c;
  let Inst{11-6} = p1;
}

// A7: integer compare to zero. The first source is always r0.
class A7<bits<4> opcode, bits<2> x2, bit ta, bit c, string mnemonic>
    : AInst<(outs PR:$p1, PR:$p2), (ins GR:$r3), mnemonic,
            "$p1, $p2 = r0, $r3"> {
  bits<6> p1;
  bits<6> p2;
  bits<7> r3;
  let Opcode = opcode;
  let Inst{36} = 1;
  let Inst{35-34} = x2;
  let Inst{33} = ta;
  let Inst{32-27} = p2;
  let Inst{26-20} = r3;
  let Inst{19-13} = 0;
  let Inst{12} = c;
  let Inst{11-6} = p1;
}

// A8: integer compare, immediate8-register.
class A8<bits<4> opcode, bits<2> x2, bit ta, bit c, string mnemonic>
    : AInst<(outs PR:$p1, PR:$p2), (ins simm8:$imm, GR:$r3), mnemonic,
            "$p1, $p2 = $imm, $r3"> {
  bits<6> p1;
  bits<6> p2;
  bits<7> r3;
  bits<8> imm;
  let Opcode = opcode;
  let Inst{36} = imm{7};
  let Inst{35-34} = x2;
  let Inst{33} = ta;
  let Inst{32-27} = p2;
  let Inst{26-20} = r3;
  let Inst{19-13} = imm{6-0};
  let Inst{12} = c;
  let Inst{11-6} = p1;
}

// A9: multimedia ALU.
class A9<bit za, bit zb, bits<4> x4, bits<2> x2b, string mnemonic>
    : AInst<(outs GR:$r1), (ins GR:$r2, GR:$r3), mnemonic,
            "$r1 = $r2, $r3"> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  let Opcode = 8;
  let Inst{36} = za;
  let Inst{35-34} = 1;
  let Inst{33} = zb;
  let Inst{32-29} = x4;
  let Inst{28-27} = x2b;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// A10: multimedia shift and add.
class A10<bits<4> x4, string mnemonic>
    : AInst<(outs GR:$r1), (ins GR:$r2, count2mm:$count, GR:$r3), mnemonic,
            "$r1 = $r2, $count, $r3"> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  bits<2> count;
  let Opcode = 8;
  let Inst{36} = 0;
  let Inst{35-34} = 1;
  let Inst{33} = 1;
  let Inst{32-29} = x4;
  let Inst{28-27} = count;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

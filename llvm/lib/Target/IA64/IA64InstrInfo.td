//===-- IA64InstrInfo.td - IA64 instruction definitions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the IA-64 instruction set.
//
//===----------------------------------------------------------------------===//

include "IA64InstrFormats.td"

//===----------------------------------------------------------------------===//
// Call-sequence markers.
//===----------------------------------------------------------------------===//

def SDT_IA64CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                            SDTCisVT<1, i64>]>;
def SDT_IA64CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                          SDTCisVT<1, i64>]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_IA64CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_IA64CallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

let Defs = [R12], Uses = [R12] in {
def ADJCALLSTACKDOWN : IA64Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   "#ADJCALLSTACKDOWN",
                                   [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : IA64Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   "#ADJCALLSTACKUP",
                                   [(callseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Integer ALU
//===----------------------------------------------------------------------===//

def ADD      : A1<0, 0, "add">;
def ADD1     : A1<0, 1, "add", "$r1 = $r2, $r3, 1">;
def SUB1     : A1<1, 0, "sub", "$r1 = $r2, $r3, 1">;
def SUB      : A1<1, 1, "sub">;
def ADDP4    : A1<2, 0, "addp4">;
def AND      : A1<3, 0, "and">;
def ANDCM    : A1<3, 1, "andcm">;
def OR       : A1<3, 2, "or">;
def XOR      : A1<3, 3, "xor">;

def SHLADD   : A2<4, "shladd">;
def SHLADDP4 : A2<6, "shladdp4">;

def SUBi     : A3<9, 1, "sub">;
def ANDi     : A3<0xb, 0, "and">;
def ANDCMi   : A3<0xb, 1, "andcm">;
def ORi      : A3<0xb, 2, "or">;
def XORi     : A3<0xb, 3, "xor">;

def ADDS     : A4<2, "adds">;
def ADDP4i   : A4<3, "addp4">;

def ADDL     : A5<"addl">;

//===----------------------------------------------------------------------===//
// Integer compare
//===----------------------------------------------------------------------===//

// The three major opcodes each hold one relation, selected by ta and c.
class CmpRel<string name, bits<4> opcode, bit ta_, bit c_> {
  string Name = name;
  bits<4> Opcode = opcode;
  bit ta = ta_;
  bit c = c_;
}

defvar CmpRels = [
  CmpRel<"lt",          0xc, 0, 0>, CmpRel<"ltu",         0xd, 0, 0>,
  CmpRel<"eq",          0xe, 0, 0>, CmpRel<"lt.unc",      0xc, 0, 1>,
  CmpRel<"ltu.unc",     0xd, 0, 1>, CmpRel<"eq.unc",      0xe, 0, 1>,
  CmpRel<"eq.and",      0xc, 1, 0>, CmpRel<"eq.or",       0xd, 1, 0>,
  CmpRel<"eq.or.andcm", 0xe, 1, 0>, CmpRel<"ne.and",      0xc, 1, 1>,
  CmpRel<"ne.or",       0xd, 1, 1>, CmpRel<"ne.or.andcm", 0xe, 1, 1>
];

// The parallel compares against zero.
defvar CmpZeroRels = [
  CmpRel<"gt.and",      0xc, 0, 0>, CmpRel<"gt.or",       0xd, 0, 0>,
  CmpRel<"gt.or.andcm", 0xe, 0, 0>, CmpRel<"le.and",      0xc, 0, 1>,
  CmpRel<"le.or",       0xd, 0, 1>, CmpRel<"le.or.andcm", 0xe, 0, 1>,
  CmpRel<"ge.and",      0xc, 1, 0>, CmpRel<"ge.or",       0xd, 1, 0>,
  CmpRel<"ge.or.andcm", 0xe, 1, 0>, CmpRel<"lt.and",      0xc, 1, 1>,
  CmpRel<"lt.or",       0xd, 1, 1>, CmpRel<"lt.or.andcm", 0xe, 1, 1>
];

multiclass Cmp<string mnemonic, bits<2> x2, bits<2> x2imm> {
  foreach R = CmpRels in {
    defvar Suffix = !toupper(!subst(".", "_", R.Name));
    def _#Suffix : A6<R.Opcode, x2, R.ta, R.c, mnemonic#"."#R.Name>;
    def _#Suffix#i : A8<R.Opcode, x2imm, R.ta, R.c, mnemonic#"."#R.Name>;
  }
  foreach R = CmpZeroRels in
    def _#!toupper(!subst(".", "_", R.Name))#z
        : A7<R.Opcode, x2, R.ta, R.c, mnemonic#"."#R.Name>;
}
defm CMP  : Cmp<"cmp", 0, 2>;
defm CMP4 : Cmp<"cmp4", 1, 3>;

//===----------------------------------------------------------------------===//
// Multimedia ALU
//===----------------------------------------------------------------------===//

// Saturation completers and their x2b values.
class MMSat<string name, bits<2> x2b> {
  string Name = name;
  bits<2> X2b = x2b;
}
foreach S = [MMSat<"", 0>, MMSat<".sss", 1>, MMSat<".uuu", 2>,
             MMSat<".uus", 3>] in {
  defvar Suffix = !toupper(!subst(".", "_", S.Name));
  def PADD1#Suffix : A9<0, 0, 0, S.X2b, "padd1"#S.Name>;
  def PADD2#Suffix : A9<0, 1, 0, S.X2b, "padd2"#S.Name>;
  def PSUB1#Suffix : A9<0, 0, 1, S.X2b, "psub1"#S.Name>;
  def PSUB2#Suffix : A9<0, 1, 1, S.X2b, "psub2"#S.Name>;
}
def PADD4     : A9<1, 0, 0, 0, "padd4">;
def PSUB4     : A9<1, 0, 1, 0, "psub4">;
def PAVG1     : A9<0, 0, 2, 2, "pavg1">;
def PAVG1_RAZ : A9<0, 0, 2, 3, "pavg1.raz">;
def PAVG2     : A9<0, 1, 2, 2, "pavg2">;
def PAVG2_RAZ : A9<0, 1, 2, 3, "pavg2.raz">;
def PAVGSUB1  : A9<0, 0, 3, 2, "pavgsub1">;
def PAVGSUB2  : A9<0, 1, 3, 2, "pavgsub2">;
def PCMP1_EQ  : A9<0, 0, 9, 0, "pcmp1.eq">;
def PCMP1_GT  : A9<0, 0, 9, 1, "pcmp1.gt">;
def PCMP2_EQ  : A9<0, 1, 9, 0, "pcmp2.eq">;
def PCMP2_GT  : A9<0, 1, 9, 1, "pcmp2.gt">;
def PCMP4_EQ  : A9<1, 0, 9, 0, "pcmp4.eq">;
def PCMP4_GT  : A9<1, 0, 9, 1, "pcmp4.gt">;

def PSHLADD2  : A10<4, "pshladd2">;
def PSHRADD2  : A10<6, "pshradd2">;

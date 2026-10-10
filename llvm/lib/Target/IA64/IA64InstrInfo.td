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

//===----------------------------------------------------------------------===//
// Multimedia multiply, mix, pack and shift
//===----------------------------------------------------------------------===//

def PMPYSHR2   : I1<3, "pmpyshr2">;
def PMPYSHR2_U : I1<1, "pmpyshr2.u">;

def MPY4      : I2<1, 0, 0, 3, 1, "mpy4">;
def MPYSHL4   : I2<1, 0, 0, 3, 3, "mpyshl4">;
def PMPY2_R   : I2<0, 2, 1, 3, 1, "pmpy2.r">;
def PMPY2_L   : I2<0, 2, 1, 3, 3, "pmpy2.l">;
def MIX1_R    : I2<0, 2, 0, 2, 0, "mix1.r">;
def MIX2_R    : I2<0, 2, 1, 2, 0, "mix2.r">;
def MIX4_R    : I2<1, 2, 0, 2, 0, "mix4.r">;
def MIX1_L    : I2<0, 2, 0, 2, 2, "mix1.l">;
def MIX2_L    : I2<0, 2, 1, 2, 2, "mix2.l">;
def MIX4_L    : I2<1, 2, 0, 2, 2, "mix4.l">;
def PACK2_USS : I2<0, 2, 1, 0, 0, "pack2.uss">;
def PACK2_SSS : I2<0, 2, 1, 0, 2, "pack2.sss">;
def PACK4_SSS : I2<1, 2, 0, 0, 2, "pack4.sss">;
def UNPACK1_H : I2<0, 2, 0, 1, 0, "unpack1.h">;
def UNPACK2_H : I2<0, 2, 1, 1, 0, "unpack2.h">;
def UNPACK4_H : I2<1, 2, 0, 1, 0, "unpack4.h">;
def UNPACK1_L : I2<0, 2, 0, 1, 2, "unpack1.l">;
def UNPACK2_L : I2<0, 2, 1, 1, 2, "unpack2.l">;
def UNPACK4_L : I2<1, 2, 0, 1, 2, "unpack4.l">;
def PMIN1_U   : I2<0, 2, 0, 0, 1, "pmin1.u">;
def PMAX1_U   : I2<0, 2, 0, 1, 1, "pmax1.u">;
def PMIN2     : I2<0, 2, 1, 0, 3, "pmin2">;
def PMAX2     : I2<0, 2, 1, 1, 3, "pmax2">;
def PSAD1     : I2<0, 2, 0, 2, 3, "psad1">;

def MUX1 : I3<"mux1">;
def MUX2 : I4<"mux2">;

def PSHR2    : I5<0, 1, 2, "pshr2">;
def PSHR4    : I5<1, 0, 2, "pshr4">;
def SHR      : I5<1, 1, 2, "shr">;
def PSHR2_U  : I5<0, 1, 0, "pshr2.u">;
def PSHR4_U  : I5<1, 0, 0, "pshr4.u">;
def SHR_U    : I5<1, 1, 0, "shr.u">;
def PSHR2i   : I6<0, 1, 3, "pshr2">;
def PSHR4i   : I6<1, 0, 3, "pshr4">;
def PSHR2_Ui : I6<0, 1, 1, "pshr2.u">;
def PSHR4_Ui : I6<1, 0, 1, "pshr4.u">;
def PSHL2    : I7<0, 1, "pshl2">;
def PSHL4    : I7<1, 0, "pshl4">;
def SHL      : I7<1, 1, "shl">;
def PSHL2i   : I8<0, 1, "pshl2">;
def PSHL4i   : I8<1, 0, "pshl4">;

def POPCNT : I9<2, "popcnt">;
def CLZ    : I9<3, "clz">;

//===----------------------------------------------------------------------===//
// Shift pair, extract and deposit
//===----------------------------------------------------------------------===//

def SHRP   : I10<"shrp">;
def EXTR_U : I11<0, "extr.u">;
def EXTR   : I11<1, "extr">;
def DEP_Z  : I12<"dep.z">;
def DEP_Zi : I13<"dep.z">;
def DEPi   : I14<"dep">;
def DEP    : I15<"dep">;

//===----------------------------------------------------------------------===//
// Bit, NaT and feature tests
//===----------------------------------------------------------------------===//

class TestRel<string name, bit tb_, bit ta_, bit c_> {
  string Name = name;
  bit tb = tb_;
  bit ta = ta_;
  bit c = c_;
}
foreach R = [TestRel<"z",          0, 0, 0>, TestRel<"z.unc",       0, 0, 1>,
             TestRel<"z.and",      1, 0, 0>, TestRel<"nz.and",      1, 0, 1>,
             TestRel<"z.or",       0, 1, 0>, TestRel<"nz.or",       0, 1, 1>,
             TestRel<"z.or.andcm", 1, 1, 0>, TestRel<"nz.or.andcm", 1, 1, 1>]
    in {
  defvar Suffix = !toupper(!subst(".", "_", R.Name));
  def TBIT_#Suffix : I16<R.tb, R.ta, R.c, "tbit."#R.Name>;
  def TNAT_#Suffix : I17<R.tb, R.ta, R.c, "tnat."#R.Name>;
  def TF_#Suffix   : I30<R.tb, R.ta, R.c, "tf."#R.Name>;
}

//===----------------------------------------------------------------------===//
// I-unit miscellany
//===----------------------------------------------------------------------===//

def NOP_I   : I18<0, "nop.i">;
def HINT_I  : I18<1, "hint.i">;
def BREAK_I : I19<0, "break.i">;
def CHK_S_I : I20<"chk.s.i">;

// The whether hint predicts the branch that uses the register; the importance
// hint (.imp) says whether to prefetch the target instruction stream now.
class MovBRHint<string name, bits<2> wh_, bit ih_> {
  string Name = name;
  bits<2> wh = wh_;
  bit ih = ih_;
}
foreach H = [MovBRHint<"",      1, 0>, MovBRHint<".imp",      1, 1>,
             MovBRHint<".sptk", 0, 0>, MovBRHint<".sptk.imp", 0, 1>,
             MovBRHint<".dptk", 2, 0>, MovBRHint<".dptk.imp", 2, 1>] in {
  defvar Suffix = !toupper(!subst(".", "_", H.Name));
  def MOV_BR#Suffix     : I21<0, H.wh, H.ih, "mov"#H.Name>;
  def MOV_RET_BR#Suffix : I21<1, H.wh, H.ih, "mov.ret"#H.Name>;
}

def MOV_FROM_BR : I22<"mov">;
def MOV_PR      : I23<"mov">;
def MOV_PR_ROT  : I24<"mov">;
def MOV_FROM_IP : I25<0x30, "mov", "ip">;
def MOV_FROM_PR : I25<0x33, "mov", "pr">;
def MOV_I_AR    : I26<"mov.i">;
def MOV_I_ARi   : I27<"mov.i">;
def MOV_I_FROM_AR : I28<"mov.i">;

def ZXT1   : I29<0x10, "zxt1">;
def ZXT2   : I29<0x11, "zxt2">;
def ZXT4   : I29<0x12, "zxt4">;
def SXT1   : I29<0x14, "sxt1">;
def SXT2   : I29<0x15, "sxt2">;
def SXT4   : I29<0x16, "sxt4">;
def CZX1_L : I29<0x18, "czx1.l">;
def CZX2_L : I29<0x19, "czx2.l">;
def CZX1_R : I29<0x1c, "czx1.r">;
def CZX2_R : I29<0x1d, "czx2.r">;

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
def SImm22AsmOperand : SImmAsmOperand<22> {
  let PredicateMethod = "isSImmOrExpr<22>";
}
def SImm14AsmOperand : SImmAsmOperand<14, [SImm22AsmOperand]> {
  let PredicateMethod = "isSImmOrExpr<14>";
}
def SImm8AsmOperand  : SImmAsmOperand<8, [SImm14AsmOperand]>;

class SImm<int width, AsmOperandClass cls> : Operand<i64> {
  let ParserMatchClass = cls;
  let DecoderMethod = "decodeSImm<"#width#">";
  let OperandType = "OPERAND_IMMEDIATE";
}
def simm8  : SImm<8, SImm8AsmOperand>;
// The add immediates may be a symbol address, possibly with a relocation
// specifier.
def simm14 : SImm<14, SImm14AsmOperand> {
  let EncoderMethod = "getExprOpValue<IA64::fixup_ia64_imm14>";
}
def simm22 : SImm<22, SImm22AsmOperand> {
  let EncoderMethod = "getExprOpValue<IA64::fixup_ia64_imm22>";
}

// The immediate of a compare pseudo-op that is assembled as "less than": the
// written value is one more than the encoded immediate. Unsigned compares
// cannot take zero, which would wrap.
class SImm8Minus1AsmOperand<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addImmMinus1Operands";
  let PredicateMethod = "is"#name;
  let DiagnosticType = "Invalid"#name;
}
def simm8m1   : SImm<8, SImm8Minus1AsmOperand<"SImm8Minus1">>;
def simm8m1nz : SImm<8, SImm8Minus1AsmOperand<"SImm8Minus1NonZero">>;

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

// An unsigned immediate, encoded as written.
class UImmAsmOperand<int width> : AsmOperandClass {
  let Name = "UImm"#width;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isUImm<"#width#">";
}
class UImm<int width> : Operand<i64> {
  let ParserMatchClass = UImmAsmOperand<width>;
  let OperandType = "OPERAND_IMMEDIATE";
}
def uimm5  : UImm<5>;
def uimm6  : UImm<6>;
def uimm8  : UImm<8>;
def uimm3  : UImm<3>;
def uimm16 : UImm<16>;
def uimm19 : UImm<19>;
def uimm21 : UImm<21>;

// A bit position, encoded as its distance from bit 63.
def cpos6 : Operand<i64> {
  let ParserMatchClass = UImmAsmOperand<6>;
  let EncoderMethod = "getReversedImmOpValue<63>";
  let DecoderMethod = "decodeReversedImm<63>";
  let OperandType = "OPERAND_IMMEDIATE";
}
// The count of a multimedia left shift, encoded as its distance from 31.
def ccount5 : Operand<i64> {
  let ParserMatchClass = UImmAsmOperand<5>;
  let EncoderMethod = "getReversedImmOpValue<31>";
  let DecoderMethod = "decodeReversedImm<31>";
  let OperandType = "OPERAND_IMMEDIATE";
}

// A field length of 1 to Max, encoded less one.
class LenAsmOperand<int max> : AsmOperandClass {
  let Name = "Len1To"#max;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmInRange<1, "#max#">";
}
class Len<int max> : Operand<i64> {
  let ParserMatchClass = LenAsmOperand<max>;
  let EncoderMethod = "getImmMinus1OpValue";
  let DecoderMethod = "decodeImmPlus1";
  let OperandType = "OPERAND_IMMEDIATE";
}
def len4 : Len<16>;
def len6 : Len<64>;

// Wider signed immediates. Bit 0 of the "mov pr = r2, mask17" mask and the
// low 16 bits of the "mov pr.rot = imm44" immediate are not encoded.
class SImmNoDiagAsmOperand<int width> : AsmOperandClass {
  let Name = "SImm"#width;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isSImm<"#width#">";
}
def simm1  : SImm<1, SImmNoDiagAsmOperand<1>>;
def simm44 : SImm<44, SImmNoDiagAsmOperand<44>>;

// The predicate mask may be written as a signed or unsigned 17-bit number.
def Mask17AsmOperand : AsmOperandClass {
  let Name = "Mask17";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isMask17";
}
def mask17 : SImm<17, Mask17AsmOperand>;

// The shift count of pmpyshr2, which can only be 0, 7, 15 or 16.
def PmpyshrCountAsmOperand : AsmOperandClass {
  let Name = "PmpyshrCount";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isPmpyshrCount";
}
def pmpyshr_count : Operand<i64> {
  let ParserMatchClass = PmpyshrCountAsmOperand;
  let EncoderMethod = "getPmpyshrCountOpValue";
  let DecoderMethod = "decodePmpyshrCount";
  let OperandType = "OPERAND_IMMEDIATE";
}

// The byte permutation of mux1: @brcst, @mix, @shuf, @alt or @rev.
def MBTypeAsmOperand : AsmOperandClass {
  let Name = "MBType";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isMBType";
}
def mbtype4 : Operand<i64> {
  let ParserMatchClass = MBTypeAsmOperand;
  let PrintMethod = "printMBType";
  let DecoderMethod = "decodeMBType";
  let OperandType = "OPERAND_IMMEDIATE";
}

// The bit number of a processor feature test, 32 to 63.
def FeatureBitAsmOperand : AsmOperandClass {
  let Name = "FeatureBit";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmInRange<32, 63>";
}
def tfimm5 : Operand<i64> {
  let ParserMatchClass = FeatureBitAsmOperand;
  let DecoderMethod = "decodeFeatureBit";
  let OperandType = "OPERAND_IMMEDIATE";
}

// An IP-relative target. Targets are bundles, so the encoding drops the low
// four bits of the displacement.
class PCRelAsmOperand<int width> : AsmOperandClass {
  let Name = "PCRel"#width;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isPCRel<"#width#">";
  let DiagnosticType = "InvalidPCRel"#width;
}
class PCRel<int width, string fixup> : Operand<OtherVT> {
  let ParserMatchClass = PCRelAsmOperand<width>;
  let PrintMethod = "printPCRel";
  let EncoderMethod = "getPCRelOpValue<IA64::fixup_ia64_"#fixup#">";
  let DecoderMethod = "decodePCRel<"#width#">";
  let OperandType = "OPERAND_PCREL";
}
// Targets are encoded in three different layouts, each with its own
// relocation: branches, chk.s and fchkf.
def target25  : PCRel<21, "pcrel21b">;
def target25m : PCRel<21, "pcrel21m">;
def target25f : PCRel<21, "pcrel21f">;
// Tags have no relocation and must be in the referencing section.
def tag13     : PCRel<9, "tag13b">;
def tag13i    : PCRel<9, "tag13i">;

// X-unit operands, which are too wide for one slot: the L slot of the bundle
// holds the bits that do not fit in the X slot, and the decoders read them
// from there.
def Imm64AsmOperand : AsmOperandClass {
  let Name = "Imm64";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImm";
}
def imm64 : Operand<i64> {
  let ParserMatchClass = Imm64AsmOperand;
  let EncoderMethod = "getExprOpValue<IA64::fixup_ia64_imm64>";
  let DecoderMethod = "decodeImm64";
  let OperandType = "OPERAND_IMMEDIATE";
}
def uimm62 : Operand<i64> {
  let ParserMatchClass = UImmAsmOperand<62>;
  let DecoderMethod = "decodeUImm62";
  let OperandType = "OPERAND_IMMEDIATE";
}
def target64 : PCRel<60, "pcrel60b"> {
  let DecoderMethod = "decodeLongPCRel";
}

// The locality hint of a memory access is an optional mnemonic completer,
// "ld8.nta", which the parser splits off. Loads with base update cannot use
// .nt2, and stores with base update can only use .nta. All other accesses,
// and all line prefetches, also accept the data access hints .d1 to .d7 of
// later processors: .d1-.d3 are aliases for the locality hints, and .d4-.d7
// are the same with one extra bit set.
class HintAsmOperand<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addHintOperands";
  let PredicateMethod = "is"#name;
  let IsOptional = 1;
  let DefaultMethod = "defaultHintOperand";
}
class Hint<string name> : Operand<i64> {
  let ParserMatchClass = HintAsmOperand<name>;
  let PrintMethod = "printHint";
  let DecoderMethod = "decode"#name;
  let OperandType = "OPERAND_IMMEDIATE";
}
def ldhint : Hint<"LoadHint">;
def sthint : Hint<"StoreHint">;
def lfhint : Hint<"FetchHint">;
def dahint : Hint<"DataHint">;

// The stride of a counted prefetch: a multiple of 64, encoded as a 5-bit
// signed multiplier.
def StrideAsmOperand : AsmOperandClass {
  let Name = "Stride";
  let RenderMethod = "addStrideOperands";
  let PredicateMethod = "isImmInRange<-1024, 960>";
  let DiagnosticType = "InvalidStride";
}
def stride5 : Operand<i64> {
  let ParserMatchClass = StrideAsmOperand;
  let EncoderMethod = "getStrideOpValue";
  let DecoderMethod = "decodeStride";
  let OperandType = "OPERAND_IMMEDIATE";
}

// The FP status field is also a completer, "fma.s1". It defaults to .s0, the
// main status field.
def StatusFieldAsmOperand : AsmOperandClass {
  let Name = "StatusField";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isStatusField";
  let IsOptional = 1;
  let DefaultMethod = "defaultStatusFieldOperand";
}
def sfield : Operand<i64> {
  let ParserMatchClass = StatusFieldAsmOperand;
  let PrintMethod = "printStatusField";
  let OperandType = "OPERAND_IMMEDIATE";
}

// A branch takes up to three completers: the whether hint, ".sptk"; the
// prefetch hint, ".few" or ".many"; and the deallocation hint, ".clr". Only
// the whether hint is required.
class BranchCompleterAsmOperand<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "is"#name;
}
class BranchCompleter<string name, AsmOperandClass opclass> : Operand<i64> {
  let ParserMatchClass = opclass;
  let PrintMethod = "print"#name;
  let OperandType = "OPERAND_IMMEDIATE";
}
def BranchWhetherAsmOperand : BranchCompleterAsmOperand<"BranchWhether">;
def BranchPrefetchAsmOperand : BranchCompleterAsmOperand<"BranchPrefetch"> {
  let IsOptional = 1;
  let DefaultMethod = "defaultBranchPrefetchOperand";
}
def BranchDeallocAsmOperand : BranchCompleterAsmOperand<"BranchDealloc"> {
  let IsOptional = 1;
  let DefaultMethod = "defaultBranchDeallocOperand";
}
def bwh : BranchCompleter<"BranchWhether", BranchWhetherAsmOperand>;
def bph : BranchCompleter<"BranchPrefetch", BranchPrefetchAsmOperand>;
def bdh : BranchCompleter<"BranchDealloc", BranchDeallocAsmOperand>;

def simm9  : SImm<9, SImmNoDiagAsmOperand<9>>;
def uimm2  : UImm<2>;
def uimm7  : UImm<7>;
def uimm9  : UImm<9>;
def uimm24 : UImm<24>;

// The fetchadd increment: +/-1, 4, 8 or 16.
def Inc3AsmOperand : AsmOperandClass {
  let Name = "Inc3";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isInc3";
}
def inc3 : Operand<i64> {
  let ParserMatchClass = Inc3AsmOperand;
  let EncoderMethod = "getInc3OpValue";
  let DecoderMethod = "decodeInc3";
  let OperandType = "OPERAND_IMMEDIATE";
}

//===----------------------------------------------------------------------===//
// Instruction classes
//===----------------------------------------------------------------------===//

// An instruction occupies one 41-bit bundle slot. Bits 40:37 hold the major
// opcode, and bits 5:0 the qualifying predicate, if any. TableGen requires a
// whole number of bytes, so the slot is the low 41 bits of a 6-byte encoding.
class IA64Inst<dag outs, dag ins, string mnemonic, string operands,
               list<dag> pattern>
    : Instruction {
  let SchedRW = [WriteSys];
  field bits<48> Inst;
  field bits<48> SoftFail = 0;
  let Namespace = "IA64";
  let Size = 6;
  let Inst{47-41} = 0;

  bits<4> Opcode = 0;
  let Inst{40-37} = Opcode;

  IA64Unit Unit = UnitNone;
  let TSFlags{2-0} = Unit.Value;

  // Set if the instruction faults in any bundle slot other than the last.
  bit LastSlot = 0;
  let TSFlags{3} = LastSlot;

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

// A pseudo-op whose unit is chosen by the slot it is bundled into: "nop" is
// nop.m in an M slot and nop.i in an I slot. The operands match those of the
// real instructions, which IA64::getOpcodeForUnit returns.
class IA64AnyUnit<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteNop];
  let isPseudo = 1;
}

class IA64Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : IA64Inst<outs, ins, asmstr, "", pattern> {
  let SchedRW = [WriteALU];
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// A-unit formats (SDM volume 3, section 4.2)
//===----------------------------------------------------------------------===//

class AInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let hasSideEffects = 0;
  let SchedRW = [WriteALU];
  let Unit = UnitA;
  let DecoderNamespace = "A";
}

// A1: integer ALU, register-register. Bit 36 is ignored.
class A1<bits<4> x4, bits<2> x2b, string mnemonic,
         string operands = "$r1 = $r2, $r3">
    : AInst<(outs GR:$r1), (ins GR:$r2, GR:$r3), mnemonic, operands> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  let Opcode = 8;
  let Inst{35-33} = 0;
  let Inst{32-29} = x4;
  let Inst{28-27} = x2b;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// A2: shift left and add. Bit 36 is ignored.
class A2<bits<4> x4, string mnemonic>
    : AInst<(outs GR:$r1), (ins GR:$r2, count2:$count, GR:$r3), mnemonic,
            "$r1 = $r2, $count, $r3"> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  bits<2> count;
  let Opcode = 8;
  let Inst{35-33} = 0;
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
  let SchedRW = [WriteICmp, WritePred2];
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
  let SchedRW = [WriteICmp, WritePred2];
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
  let SchedRW = [WriteICmp, WritePred2];
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
  let SchedRW = [WriteMMALU, ReadMM, ReadMM];
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
  let SchedRW = [WriteMMALU, ReadMM, ReadMM];
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

//===----------------------------------------------------------------------===//
// I-unit formats (SDM volume 3, section 4.3)
//===----------------------------------------------------------------------===//

class IInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteShift];
  let Unit = UnitI;
  let DecoderNamespace = "I";
}

// The multimedia and variable shift instructions share major opcode 7 and
// these extension fields. Most add a third, x2c, in bits 31:30.
class IMMInst<bit za, bits<2> x2a, bit zb, bits<2> x2b, dag outs, dag ins,
              string mnemonic, string operands>
    : IInst<outs, ins, mnemonic, operands> {
  let hasSideEffects = 0;
  let SchedRW = [WriteMMShift, ReadMM, ReadMM];
  bits<7> r1;
  let Opcode = 7;
  let Inst{36} = za;
  let Inst{35-34} = x2a;
  let Inst{33} = zb;
  let Inst{32} = 0;
  let Inst{29-28} = x2b;
  let Inst{12-6} = r1;
}

// I1: multimedia multiply and shift.
class I1<bits<2> x2b, string mnemonic>
    : IMMInst<0, 0, 1, x2b, (outs GR:$r1),
              (ins GR:$r2, GR:$r3, pmpyshr_count:$count), mnemonic,
              "$r1 = $r2, $r3, $count"> {
  let SchedRW = [WriteMMMul, ReadMM, ReadMM];
  bits<7> r2;
  bits<7> r3;
  bits<2> count;
  let Inst{31-30} = count;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
}

// I2: multimedia multiply, mix and pack; I7: shift left, variable.
class I2<bit za, bits<2> x2a, bit zb, bits<2> x2c, bits<2> x2b,
         string mnemonic>
    : IMMInst<za, x2a, zb, x2b, (outs GR:$r1), (ins GR:$r2, GR:$r3), mnemonic,
              "$r1 = $r2, $r3"> {
  let hasSideEffects = 0;
  bits<7> r2;
  bits<7> r3;
  let Inst{31-30} = x2c;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
}
class I7<bit za, bit zb, string mnemonic> : I2<za, 0, zb, 1, 0, mnemonic>;

// I3: multimedia mux1.
class I3<string mnemonic>
    : IMMInst<0, 3, 0, 2, (outs GR:$r1), (ins GR:$r2, mbtype4:$mbtype),
              mnemonic, "$r1 = $r2, $mbtype"> {
  let SchedRW = [WriteMMShiftI, ReadMM];
  bits<7> r2;
  bits<4> mbtype;
  let Inst{31-30} = 2;
  let Inst{23-20} = mbtype;
  let Inst{19-13} = r2;
}

// I4: multimedia mux2.
class I4<string mnemonic>
    : IMMInst<0, 3, 1, 2, (outs GR:$r1), (ins GR:$r2, uimm8:$mhtype),
              mnemonic, "$r1 = $r2, $mhtype"> {
  let SchedRW = [WriteMMShiftI, ReadMM];
  bits<7> r2;
  bits<8> mhtype;
  let Inst{31-30} = 2;
  let Inst{27-20} = mhtype;
  let Inst{19-13} = r2;
}

// I5: shift right, variable. The value is in r3 and the count in r2.
class I5<bit za, bit zb, bits<2> x2b, string mnemonic>
    : IMMInst<za, 0, zb, x2b, (outs GR:$r1), (ins GR:$r3, GR:$r2), mnemonic,
              "$r1 = $r3, $r2"> {
  bits<7> r2;
  bits<7> r3;
  let Inst{31-30} = 0;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
}

// I6: multimedia shift right, fixed.
class I6<bit za, bit zb, bits<2> x2b, string mnemonic>
    : IMMInst<za, 1, zb, x2b, (outs GR:$r1), (ins GR:$r3, uimm5:$count),
              mnemonic, "$r1 = $r3, $count"> {
  let SchedRW = [WriteMMShiftI, ReadMM];
  bits<7> r3;
  bits<5> count;
  let Inst{31-30} = 0;
  let Inst{26-20} = r3;
  let Inst{18-14} = count;
}

// I8: multimedia shift left, fixed.
class I8<bit za, bit zb, string mnemonic>
    : IMMInst<za, 3, zb, 1, (outs GR:$r1), (ins GR:$r2, ccount5:$count),
              mnemonic, "$r1 = $r2, $count"> {
  let SchedRW = [WriteMMShiftI, ReadMM];
  bits<7> r2;
  bits<5> count;
  let Inst{31-30} = 1;
  let Inst{24-20} = count;
  let Inst{19-13} = r2;
}

// I9: bit count.
class I9<bits<2> x2c, string mnemonic>
    : IMMInst<0, 1, 1, 1, (outs GR:$r1), (ins GR:$r3), mnemonic,
              "$r1 = $r3"> {
  let SchedRW = [WriteMMShift, ReadMM];
  bits<7> r3;
  let Inst{31-30} = x2c;
  let Inst{26-20} = r3;
  let Inst{19-13} = 0;
}

// I10: shift right pair.
class I10<string mnemonic>
    : IInst<(outs GR:$r1), (ins GR:$r2, GR:$r3, uimm6:$count), mnemonic,
            "$r1 = $r2, $r3, $count"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  bits<6> count;
  let Opcode = 5;
  let Inst{35-34} = 3;
  let Inst{33} = 0;
  let Inst{32-27} = count;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// I11: extract.
class I11<bit y, string mnemonic>
    : IInst<(outs GR:$r1), (ins GR:$r3, uimm6:$pos, len6:$len), mnemonic,
            "$r1 = $r3, $pos, $len"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<7> r3;
  bits<6> pos;
  bits<6> len;
  let Opcode = 5;
  let Inst{35-34} = 1;
  let Inst{33} = 0;
  let Inst{32-27} = len;
  let Inst{26-20} = r3;
  let Inst{19-14} = pos;
  let Inst{13} = y;
  let Inst{12-6} = r1;
}

// I12: zero and deposit.
class I12<string mnemonic>
    : IInst<(outs GR:$r1), (ins GR:$r2, cpos6:$pos, len6:$len), mnemonic,
            "$r1 = $r2, $pos, $len"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<7> r2;
  bits<6> pos;
  bits<6> len;
  let Opcode = 5;
  let Inst{35-34} = 1;
  let Inst{33} = 1;
  let Inst{32-27} = len;
  let Inst{26} = 0;
  let Inst{25-20} = pos;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// I13: zero and deposit immediate8.
class I13<string mnemonic>
    : IInst<(outs GR:$r1), (ins simm8:$imm, cpos6:$pos, len6:$len), mnemonic,
            "$r1 = $imm, $pos, $len"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<8> imm;
  bits<6> pos;
  bits<6> len;
  let Opcode = 5;
  let Inst{36} = imm{7};
  let Inst{35-34} = 1;
  let Inst{33} = 1;
  let Inst{32-27} = len;
  let Inst{26} = 1;
  let Inst{25-20} = pos;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// I14: deposit immediate1.
class I14<string mnemonic>
    : IInst<(outs GR:$r1), (ins simm1:$imm, GR:$r3, cpos6:$pos, len6:$len),
            mnemonic, "$r1 = $imm, $r3, $pos, $len"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<7> r3;
  bit imm;
  bits<6> pos;
  bits<6> len;
  let Opcode = 5;
  let Inst{36} = imm;
  let Inst{35-34} = 3;
  let Inst{33} = 1;
  let Inst{32-27} = len;
  let Inst{26-20} = r3;
  let Inst{19-14} = pos;
  let Inst{12-6} = r1;
}

// I15: deposit.
class I15<string mnemonic>
    : IInst<(outs GR:$r1), (ins GR:$r2, GR:$r3, cpos6:$pos, len4:$len),
            mnemonic, "$r1 = $r2, $r3, $pos, $len"> {
  let hasSideEffects = 0;
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  bits<6> pos;
  bits<4> len;
  let Opcode = 4;
  let Inst{36-31} = pos;
  let Inst{30-27} = len;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// The bit, NaT and feature tests write two predicates.
class ITestInst<bit tb, bit ta, bit c, dag ins, string mnemonic,
                string operands>
    : IInst<(outs PR:$p1, PR:$p2), ins, mnemonic, operands> {
  let SchedRW = [WriteTBit, WritePred2];
  bits<6> p1;
  bits<6> p2;
  let Opcode = 5;
  let Inst{36} = tb;
  let Inst{35-34} = 0;
  let Inst{33} = ta;
  let Inst{32-27} = p2;
  let Inst{12} = c;
  let Inst{11-6} = p1;
}

// I16: test bit.
class I16<bit tb, bit ta, bit c, string mnemonic>
    : ITestInst<tb, ta, c, (ins GR:$r3, uimm6:$pos), mnemonic,
                "$p1, $p2 = $r3, $pos"> {
  let hasSideEffects = 0;
  bits<7> r3;
  bits<6> pos;
  let Inst{26-20} = r3;
  let Inst{19-14} = pos;
  let Inst{13} = 0;
}

// I17: test NaT.
class I17<bit tb, bit ta, bit c, string mnemonic>
    : ITestInst<tb, ta, c, (ins GR:$r3), mnemonic, "$p1, $p2 = $r3"> {
  let hasSideEffects = 0;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{19} = 0;
  let Inst{13} = 1;
}

// I30: test feature.
class I30<bit tb, bit ta, bit c, string mnemonic>
    : ITestInst<tb, ta, c, (ins tfimm5:$imm), mnemonic, "$p1, $p2 = $imm"> {
  bits<6> imm;
  let Inst{26-20} = 0;
  let Inst{19} = 1;
  let Inst{18-14} = imm{4-0};
  let Inst{13} = 1;
}

// The remaining I-unit instructions share major opcode 0 and the extension
// field x3. Those with x3 = 0 have a second extension field, x6.
class IMiscInst<bits<3> x3, dag outs, dag ins, string mnemonic,
                string operands>
    : IInst<outs, ins, mnemonic, operands> {
  let SchedRW = [WriteSys];
  let Opcode = 0;
  let Inst{35-33} = x3;
}
class IMisc6Inst<bits<6> x6, dag outs, dag ins, string mnemonic,
                 string operands>
    : IMiscInst<0, outs, ins, mnemonic, operands> {
  let Inst{32-27} = x6;
}

// I18: nop and hint; I19: break.
class I19<bits<6> x6, string mnemonic>
    : IMisc6Inst<x6, (outs), (ins uimm21:$imm), mnemonic, "$imm"> {
  bits<21> imm;
  let Inst{36} = imm{20};
  let Inst{25-6} = imm{19-0};
}
class I18<bit y, string mnemonic> : I19<1, mnemonic> {
  let SchedRW = [WriteNop];
  let Inst{26} = y;
}

// I20: integer speculation check.
class I20<string mnemonic>
    : IMiscInst<1, (outs), (ins GR:$r2, target25m:$target), mnemonic,
                "$r2, $target"> {
  let SchedRW = [WriteChk];
  bits<7> r2;
  bits<21> target;
  let Inst{36} = target{20};
  let Inst{32-20} = target{19-7};
  let Inst{19-13} = r2;
  let Inst{12-6} = target{6-0};
}

// I21: move to a branch register, with a hint about the branch that will use
// it and that branch's address.
class I21<bit x, bits<2> wh, bit ih, string mnemonic>
    : IMiscInst<7, (outs BR:$b1), (ins GR:$r2, tag13i:$tag), mnemonic,
                "$b1 = $r2, $tag"> {
  let SchedRW = [WriteToBR];
  bits<3> b1;
  bits<7> r2;
  bits<9> tag;
  let Inst{32-24} = tag;
  let Inst{23} = ih;
  let Inst{22} = x;
  let Inst{21-20} = wh;
  let Inst{19-13} = r2;
  let Inst{8-6} = b1;
}

// I22: move from a branch register.
class I22<string mnemonic>
    : IMisc6Inst<0x31, (outs GR:$r1), (ins BR:$b2), mnemonic, "$r1 = $b2"> {
  let SchedRW = [WriteFromBR];
  bits<7> r1;
  bits<3> b2;
  let Inst{15-13} = b2;
  let Inst{12-6} = r1;
}

// I23: move to the predicates under a mask.
class I23<string mnemonic>
    : IMiscInst<3, (outs), (ins GR:$r2, mask17:$mask), mnemonic,
                "pr = $r2, $mask"> {
  let SchedRW = [WriteToPR];
  bits<7> r2;
  bits<17> mask;
  let Inst{36} = mask{16};
  let Inst{31-24} = mask{15-8};
  let Inst{19-13} = r2;
  let Inst{12-6} = mask{7-1};
}

// I24: move an immediate to the rotating predicates.
class I24<string mnemonic>
    : IMiscInst<2, (outs), (ins simm44:$imm), mnemonic, "pr.rot = $imm"> {
  let SchedRW = [WriteToPR];
  bits<44> imm;
  let Inst{36} = imm{43};
  let Inst{32-6} = imm{42-16};
}

// I25: move from the predicates or the instruction pointer.
class I25<bits<6> x6, string mnemonic, string source>
    : IMisc6Inst<x6, (outs GR:$r1), (ins), mnemonic, "$r1 = "#source> {
  let SchedRW = [WriteFromPR];
  bits<7> r1;
  let Inst{12-6} = r1;
}

// I26: move to an application register.
class I26<string mnemonic>
    : IMisc6Inst<0x2a, (outs AR:$ar3), (ins GR:$r2), mnemonic,
                 "$ar3 = $r2"> {
  let SchedRW = [WriteToAR];
  bits<7> ar3;
  bits<7> r2;
  let Inst{26-20} = ar3;
  let Inst{19-13} = r2;
}

// I27: move immediate8 to an application register.
class I27<string mnemonic>
    : IMisc6Inst<0x0a, (outs AR:$ar3), (ins simm8:$imm), mnemonic,
                 "$ar3 = $imm"> {
  let SchedRW = [WriteToAR];
  bits<7> ar3;
  bits<8> imm;
  let Inst{36} = imm{7};
  let Inst{26-20} = ar3;
  let Inst{19-13} = imm{6-0};
}

// I28: move from an application register.
class I28<string mnemonic>
    : IMisc6Inst<0x32, (outs GR:$r1), (ins AR:$ar3), mnemonic,
                 "$r1 = $ar3"> {
  let SchedRW = [WriteFromAR];
  bits<7> r1;
  bits<7> ar3;
  let Inst{26-20} = ar3;
  let Inst{12-6} = r1;
}

// I29: sign and zero extension, and compute zero index.
class I29<bits<6> x6, string mnemonic>
    : IMisc6Inst<x6, (outs GR:$r1), (ins GR:$r3), mnemonic, "$r1 = $r3"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteExt];
  bits<7> r1;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{12-6} = r1;
}

//===----------------------------------------------------------------------===//
// M-unit formats (SDM volume 3, section 4.4)
//===----------------------------------------------------------------------===//

class MInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteSys];
  let Unit = UnitM;
  let DecoderNamespace = "M";
}

// Memory accesses carry a locality hint, written as the last mnemonic
// completer. The forms with an immediate base update use the next major
// opcode and spread the immediate over the bits that distinguish the other
// forms.
class MMemInst<bits<4> op, bits<6> x6, dag outs, dag ins, string mnemonic,
               string operands>
    : MInst<outs, ins, mnemonic, operands> {
  bits<3> hint;
  bits<7> r3;
  let Opcode = op;
  let Inst{35-30} = x6;
  let Inst{29-28} = hint{1-0};
  let Inst{26-20} = r3;
  let AsmString = mnemonic#"${qp}${hint}\t"#operands;
}
class MMemRegInst<bits<4> op, bit m, bits<6> x6, bit x, dag outs, dag ins,
                  string mnemonic, string operands>
    : MMemInst<op, x6, outs, ins, mnemonic, operands> {
  let Inst{36} = m;
  let Inst{27} = x;
}
class MMemImmInst<bits<4> op, bits<6> x6, dag outs, dag ins, string mnemonic,
                  string operands>
    : MMemInst<op, x6, outs, ins, mnemonic, operands> {
  bits<9> imm;
  let Inst{36} = imm{8};
  let Inst{27} = imm{7};
  let Constraints = "$wb = $r3";
}

// M1: integer load.
class M1<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 0, (outs GR:$r1), (ins GR:$r3, dahint:$hint),
                  mnemonic, "$r1 = [$r3]"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteLoad, ReadAddr];
  bits<7> r1;
  let Inst{19} = hint{2};
  let Inst{12-6} = r1;
}
// A linkage table load that names the symbol whose address it loads. If the
// address is at a constant offset from gp, the linker may relax the load to
// a move. The symbol is only a relocation, so the encoding is that of the
// plain load, which is what a disassembler sees.
def SymbolAsmOperand : AsmOperandClass {
  let Name = "Symbol";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImm";
}
def ldxmov : Operand<i64> {
  let ParserMatchClass = SymbolAsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}
class M1Mov<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 0, (outs GR:$r1), (ins GR:$r3, ldxmov:$sym),
                  mnemonic, "$r1 = [$r3], $sym"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteLoad, ReadAddr];
  bits<7> r1;
  let hint = 0;
  let Inst{12-6} = r1;
  let AsmString = mnemonic#"${qp}\t$r1 = [$r3], $sym";
  let isAsmParserOnly = 1;
}
// The 16-byte load puts the high half in ar.csd.
class M1_16<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 1, (outs GR:$r1), (ins GR:$r3, dahint:$hint),
                  mnemonic, "$r1, ar.csd = [$r3]"> {
  let SchedRW = [WriteLoad, ReadAddr];
  bits<7> r1;
  let Inst{19} = hint{2};
  let Inst{12-6} = r1;
}

// M2: integer load, incrementing the base by a register.
class M2<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 1, x6, 0, (outs GR:$r1, GR:$wb),
                  (ins GR:$r3, GR:$r2, ldhint:$hint), mnemonic,
                  "$r1 = [$r3], $r2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteLoad, WritePostInc, ReadAddr];
  bits<7> r1;
  bits<7> r2;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
  let Constraints = "$wb = $r3";
}

// M3: integer load, incrementing the base by an immediate.
class M3<bits<6> x6, string mnemonic>
    : MMemImmInst<5, x6, (outs GR:$r1, GR:$wb),
                  (ins GR:$r3, simm9:$imm, ldhint:$hint), mnemonic,
                  "$r1 = [$r3], $imm"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteLoad, WritePostInc, ReadAddr];
  bits<7> r1;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// M4: integer store.
class M4<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 0, (outs), (ins GR:$r3, GR:$r2, dahint:$hint),
                  mnemonic, "[$r3] = $r2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteStore, ReadAddr];
  bits<7> r2;
  let Inst{12} = hint{2};
  let Inst{19-13} = r2;
}
class M4_16<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 1, (outs), (ins GR:$r3, GR:$r2, dahint:$hint),
                  mnemonic, "[$r3] = $r2, ar.csd"> {
  let SchedRW = [WriteStore, ReadAddr];
  bits<7> r2;
  let Inst{12} = hint{2};
  let Inst{19-13} = r2;
}

// M5: integer store, incrementing the base by an immediate.
class M5<bits<6> x6, string mnemonic>
    : MMemImmInst<5, x6, (outs GR:$wb),
                  (ins GR:$r3, GR:$r2, simm9:$imm, sthint:$hint), mnemonic,
                  "[$r3] = $r2, $imm"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteStore, ReadAddr];
  bits<7> r2;
  let Inst{19-13} = r2;
  let Inst{12-6} = imm{6-0};
}

// M6: floating-point load.
class M6<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 0, x6, 0, (outs FR:$f1), (ins GR:$r3, dahint:$hint),
                  mnemonic, "$f1 = [$r3]"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFLoad, ReadAddr];
  bits<7> f1;
  let Inst{19} = hint{2};
  let Inst{12-6} = f1;
}

// M7: floating-point load, incrementing the base by a register.
class M7<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 1, x6, 0, (outs FR:$f1, GR:$wb),
                  (ins GR:$r3, GR:$r2, ldhint:$hint), mnemonic,
                  "$f1 = [$r3], $r2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFLoad, WritePostInc, ReadAddr];
  bits<7> f1;
  bits<7> r2;
  let Inst{19-13} = r2;
  let Inst{12-6} = f1;
  let Constraints = "$wb = $r3";
}

// M8: floating-point load, incrementing the base by an immediate.
class M8<bits<6> x6, string mnemonic>
    : MMemImmInst<7, x6, (outs FR:$f1, GR:$wb),
                  (ins GR:$r3, simm9:$imm, ldhint:$hint), mnemonic,
                  "$f1 = [$r3], $imm"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFLoad, WritePostInc, ReadAddr];
  bits<7> f1;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = f1;
}

// M9: floating-point store.
class M9<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 0, x6, 0, (outs), (ins GR:$r3, FR:$f2, dahint:$hint),
                  mnemonic, "[$r3] = $f2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFStore, ReadAddr];
  bits<7> f2;
  let Inst{12} = hint{2};
  let Inst{19-13} = f2;
}

// M10: floating-point store, incrementing the base by an immediate.
class M10<bits<6> x6, string mnemonic>
    : MMemImmInst<7, x6, (outs GR:$wb),
                  (ins GR:$r3, FR:$f2, simm9:$imm, sthint:$hint), mnemonic,
                  "[$r3] = $f2, $imm"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFStore, ReadAddr];
  bits<7> f2;
  let Inst{19-13} = f2;
  let Inst{12-6} = imm{6-0};
}

// M11: floating-point load pair.
class M11<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 0, x6, 1, (outs FR:$f1, FR:$f2),
                  (ins GR:$r3, ldhint:$hint), mnemonic, "$f1, $f2 = [$r3]"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFLoad, WriteFLoad2, ReadAddr];
  bits<7> f1;
  bits<7> f2;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}

// M12: floating-point load pair, incrementing the base by the size loaded.
class M12<bits<6> x6, string mnemonic, int size>
    : MMemRegInst<6, 1, x6, 1, (outs FR:$f1, FR:$f2, GR:$wb),
                  (ins GR:$r3, ldhint:$hint), mnemonic,
                  "$f1, $f2 = [$r3], "#size> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFLoad, WriteFLoad2, WritePostInc, ReadAddr];
  bits<7> f1;
  bits<7> f2;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
  let Constraints = "$wb = $r3";
}

// M13: line prefetch. Bit 19 is ignored, except when \p hasCount is set,
// where it distinguishes M13Count from this form.
class M13<bits<6> x6, string mnemonic, bit hasCount>
    : MMemRegInst<6, 0, x6, 0, (outs), (ins GR:$r3, lfhint:$hint), mnemonic,
                  "[$r3]"> {
  let SchedRW = [WritePrefetch, ReadAddr];
  let Inst{19} = !if(hasCount, 0, ?);
  let Inst{12} = hint{2};
}
// Counted prefetch of lines a stride apart, distinguished from the plain
// prefetch by bit 19.
class M13Count<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 0, x6, 0, (outs),
                  (ins GR:$r3, Count<64>:$count, stride5:$stride,
                       lfhint:$hint),
                  mnemonic, "[$r3], $count, $stride"> {
  let SchedRW = [WritePrefetch, ReadAddr];
  bits<6> count;
  bits<5> stride;
  let Inst{19} = 1;
  let Inst{18} = 0;
  let Inst{17-13} = stride;
  let Inst{12} = hint{2};
  let Inst{11-6} = count;
}

// M14: line prefetch, incrementing the base by a register.
class M14<bits<6> x6, string mnemonic>
    : MMemRegInst<6, 1, x6, 0, (outs GR:$wb),
                  (ins GR:$r3, GR:$r2, lfhint:$hint), mnemonic,
                  "[$r3], $r2"> {
  let SchedRW = [WritePrefetch, ReadAddr];
  bits<7> r2;
  let Inst{19-13} = r2;
  let Inst{12} = hint{2};
  let Constraints = "$wb = $r3";
}

// M15: line prefetch, incrementing the base by an immediate.
class M15<bits<6> x6, string mnemonic>
    : MMemImmInst<7, x6, (outs GR:$wb),
                  (ins GR:$r3, simm9:$imm, lfhint:$hint), mnemonic,
                  "[$r3], $imm"> {
  let SchedRW = [WritePrefetch, ReadAddr];
  let Inst{19-13} = imm{6-0};
  let Inst{12} = hint{2};
}

// M16: exchange and compare-and-exchange. The compare value is in ar.ccv,
// and the high half of the 16-byte form is in ar.csd.
class M16<bits<6> x6, string mnemonic, string implicit = "">
    : MMemRegInst<4, 0, x6, 1, (outs GR:$r1),
                  (ins GR:$r3, GR:$r2, ldhint:$hint), mnemonic,
                  "$r1 = [$r3], $r2"#implicit> {
  let SchedRW = [WriteSem, ReadAddr];
  bits<7> r1;
  bits<7> r2;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}

// M17: fetch and add immediate.
class M17<bits<6> x6, string mnemonic>
    : MMemRegInst<4, 0, x6, 1, (outs GR:$r1),
                  (ins GR:$r3, inc3:$inc, ldhint:$hint), mnemonic,
                  "$r1 = [$r3], $inc"> {
  let SchedRW = [WriteSem, ReadAddr];
  bits<7> r1;
  bits<3> inc;
  let Inst{15-13} = inc;
  let Inst{12-6} = r1;
}

// M18: move a GR to an FR (setf); M19: move an FR to a GR (getf).
class M18<bits<6> x6, string mnemonic>
    : MInst<(outs FR:$f1), (ins GR:$r2), mnemonic, "$f1 = $r2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteSetF];
  bits<7> f1;
  bits<7> r2;
  let Opcode = 6;
  let Inst{36} = 0;
  let Inst{35-30} = x6;
  let Inst{27} = 1;
  let Inst{19-13} = r2;
  let Inst{12-6} = f1;
}
class M19<bits<6> x6, string mnemonic>
    : MInst<(outs GR:$r1), (ins FR:$f2), mnemonic, "$r1 = $f2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteGetF];
  bits<7> r1;
  bits<7> f2;
  let Opcode = 4;
  let Inst{36} = 0;
  let Inst{35-30} = x6;
  let Inst{27} = 1;
  let Inst{19-13} = f2;
  let Inst{12-6} = r1;
}

// The speculation checks have major opcodes 0 and 1 and are distinguished
// by x3. M20 and M21: control speculation check.
class MChkSInst<bits<3> x3, RegisterClass rc, string mnemonic>
    : MInst<(outs), (ins rc:$r2, target25m:$target), mnemonic,
            "$r2, $target"> {
  let SchedRW = [WriteChk];
  bits<7> r2;
  bits<21> target;
  let Opcode = 1;
  let Inst{36} = target{20};
  let Inst{35-33} = x3;
  let Inst{32-20} = target{19-7};
  let Inst{19-13} = r2;
  let Inst{12-6} = target{6-0};
}
class M20<string mnemonic> : MChkSInst<1, GR, mnemonic>;
class M21<string mnemonic> : MChkSInst<3, FR, mnemonic>;

// M22 and M23: check a data-speculative load.
class MChkAInst<bits<3> x3, RegisterClass rc, string mnemonic>
    : MInst<(outs), (ins rc:$r1, target25:$target), mnemonic,
            "$r1, $target"> {
  let SchedRW = [WriteChk];
  bits<7> r1;
  bits<21> target;
  let Opcode = 0;
  let Inst{36} = target{20};
  let Inst{35-33} = x3;
  let Inst{32-13} = target{19-0};
  let Inst{12-6} = r1;
}
class M22<bits<3> x3, string mnemonic> : MChkAInst<x3, GR, mnemonic>;
class M23<bits<3> x3, string mnemonic> : MChkAInst<x3, FR, mnemonic>;

// The rest of major opcode 0 has x3 = 0 and is distinguished by x4 and x2.
class MSys0Inst<bits<4> x4, bits<2> x2, dag outs, dag ins, string mnemonic,
                string operands>
    : MInst<outs, ins, mnemonic, operands> {
  let Opcode = 0;
  let Inst{35-33} = 0;
  let Inst{32-31} = x2;
  let Inst{30-27} = x4;
}
// The rest of major opcode 1 also has x3 = 0 and is distinguished by x6.
class MSys1Inst<bits<6> x6, dag outs, dag ins, string mnemonic,
                string operands>
    : MInst<outs, ins, mnemonic, operands> {
  let Opcode = 1;
  let Inst{35-33} = 0;
  let Inst{32-27} = x6;
}

// M24: memory management and serialization, without operands.
class M24<bits<4> x4, bits<2> x2, string mnemonic>
    : MSys0Inst<x4, x2, (outs), (ins), mnemonic, "">;

// M25: register stack flush and load. These have no qualifying predicate.
class M25<bits<4> x4, string mnemonic>
    : IA64Inst<(outs), (ins), mnemonic, "", []> {
  let Unit = UnitM;
  let DecoderNamespace = "M";
  let Opcode = 0;
  let Inst{35-33} = 0;
  let Inst{32-31} = 0;
  let Inst{30-27} = x4;
  let Inst{5-0} = 0;
}

// M26 and M27: invalidate one entry of the advanced load table.
class MInvalaInst<bits<4> x4, RegisterClass rc, string mnemonic>
    : MSys0Inst<x4, 1, (outs), (ins rc:$r1), mnemonic, "$r1"> {
  bits<7> r1;
  let Inst{12-6} = r1;
}
class M26<string mnemonic> : MInvalaInst<2, GR, mnemonic>;
class M27<string mnemonic> : MInvalaInst<3, FR, mnemonic>;

// M28: flush cache.
class M28<bit x, string mnemonic>
    : MSys1Inst<0x30, (outs), (ins GR:$r3), mnemonic, "$r3"> {
  bits<7> r3;
  let Inst{36} = x;
  let Inst{26-20} = r3;
}

// M29: move to an application register.
class M29<string mnemonic>
    : MSys1Inst<0x2a, (outs AR:$ar3), (ins GR:$r2), mnemonic, "$ar3 = $r2"> {
  let SchedRW = [WriteToAR];
  bits<7> ar3;
  bits<7> r2;
  let Inst{26-20} = ar3;
  let Inst{19-13} = r2;
}

// M30: move immediate8 to an application register.
class M30<string mnemonic>
    : MSys0Inst<8, 2, (outs AR:$ar3), (ins simm8:$imm), mnemonic,
                "$ar3 = $imm"> {
  let SchedRW = [WriteToAR];
  bits<7> ar3;
  bits<8> imm;
  let Inst{36} = imm{7};
  let Inst{26-20} = ar3;
  let Inst{19-13} = imm{6-0};
}

// M31: move from an application register.
class M31<string mnemonic>
    : MSys1Inst<0x22, (outs GR:$r1), (ins AR:$ar3), mnemonic, "$r1 = $ar3"> {
  let SchedRW = [WriteFromAR];
  bits<7> r1;
  bits<7> ar3;
  let Inst{26-20} = ar3;
  let Inst{12-6} = r1;
}

// M32: move to a control register; M33: move from one.
class M32<string mnemonic>
    : MSys1Inst<0x2c, (outs CR:$cr3), (ins GR:$r2), mnemonic, "$cr3 = $r2"> {
  bits<7> cr3;
  bits<7> r2;
  let Inst{26-20} = cr3;
  let Inst{19-13} = r2;
}
class M33<string mnemonic>
    : MSys1Inst<0x24, (outs GR:$r1), (ins CR:$cr3), mnemonic, "$r1 = $cr3"> {
  bits<7> r1;
  bits<7> cr3;
  let Inst{26-20} = cr3;
  let Inst{12-6} = r1;
}

// M34: allocate a register stack frame. It is written with the input, local,
// output and rotating register counts, and encoded as the frame size, the
// size of locals (inputs plus locals) and the rotating count divided by 8.
// Inputs and locals cannot be separated when decoding, so a decoded alloc
// has no inputs.
class M34<string mnemonic>
    : MInst<(outs GR:$r1),
            (ins uimm7:$inputs, uimm7:$locals, uimm7:$outputs, uimm7:$rotating),
            mnemonic, "$r1 = ar.pfs, $inputs, $locals, $outputs, $rotating"> {
  let SchedRW = [WriteAlloc];
  bits<7> r1;
  let Opcode = 1;
  let Inst{35-33} = 6;
  let Inst{12-6} = r1;
  let PostEncoderMethod = "encodeAllocFrame";
  let DecoderMethod = "decodeAlloc";
  let hasCompleteDecoder = 0;
}

// M35: move to the processor status register; M36: move from it.
class M35<bits<6> x6, string mnemonic, string dest>
    : MSys1Inst<x6, (outs), (ins GR:$r2), mnemonic, dest#" = $r2"> {
  bits<7> r2;
  let Inst{19-13} = r2;
}
class M36<bits<6> x6, string mnemonic, string source>
    : MSys1Inst<x6, (outs GR:$r1), (ins), mnemonic, "$r1 = "#source> {
  bits<7> r1;
  let Inst{12-6} = r1;
}

// M37: break; M48: nop and hint.
class M37<bits<4> x4, string mnemonic>
    : MSys0Inst<x4, 0, (outs), (ins uimm21:$imm), mnemonic, "$imm"> {
  bits<21> imm;
  let Inst{36} = imm{20};
  let Inst{25-6} = imm{19-0};
}
class M48<bit y, string mnemonic> : M37<1, mnemonic> {
  let SchedRW = [WriteNop];
  let Inst{26} = y;
}
// hint.m has a 19-bit immediate, two bits fewer than the other units. The
// SDM originally gave it all 21 bits; bits 11:10 were later taken to
// distinguish it from the moves to data access hint registers, which is what
// GNU as implements.
class M48Hint<string mnemonic>
    : MSys0Inst<1, 0, (outs), (ins uimm19:$imm), mnemonic, "$imm"> {
  let SchedRW = [WriteNop];
  bits<19> imm;
  let Inst{36} = imm{18};
  let Inst{26} = 1;
  let Inst{25-12} = imm{17-4};
  let Inst{11-10} = 0;
  let Inst{9-6} = imm{3-0};
}

// Move an immediate to one of the eight data access hint registers. Bits
// 11:10 distinguish it from hint.m.
class M50<string mnemonic>
    : MSys0Inst<1, 0, (outs), (ins uimm3:$dahr, uimm16:$imm), mnemonic,
                "dahr$dahr = $imm"> {
  bits<3> dahr;
  bits<16> imm;
  let Inst{36} = imm{15};
  let Inst{26} = 1;
  let Inst{25-23} = dahr;
  let Inst{22-12} = imm{14-4};
  let Inst{11-10} = 1;
  let Inst{9-6} = imm{3-0};
}

// M38: probe access; M39: the same with an immediate privilege level.
class M38<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs GR:$r1), (ins GR:$r3, GR:$r2), mnemonic,
                "$r1 = $r3, $r2"> {
  bits<7> r1;
  bits<7> r2;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
  let Inst{12-6} = r1;
}
class M39<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs GR:$r1), (ins GR:$r3, uimm2:$imm), mnemonic,
                "$r1 = $r3, $imm"> {
  bits<7> r1;
  bits<7> r3;
  bits<2> imm;
  let Inst{26-20} = r3;
  let Inst{14-13} = imm;
  let Inst{12-6} = r1;
}

// M40: probe access and fault.
class M40<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs), (ins GR:$r3, uimm2:$imm), mnemonic,
                "$r3, $imm"> {
  bits<7> r3;
  bits<2> imm;
  let Inst{26-20} = r3;
  let Inst{14-13} = imm;
}

// M41: insert a translation cache entry.
class M41<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs), (ins GR:$r2), mnemonic, "$r2"> {
  bits<7> r2;
  let Inst{19-13} = r2;
}

// M42: move to an indirect register file, or insert a translation register;
// M43: move from an indirect register file.
class M42<bits<6> x6, string mnemonic, string file>
    : MSys1Inst<x6, (outs), (ins GR:$r3, GR:$r2), mnemonic,
                file#"[$r3] = $r2"> {
  bits<7> r2;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
}
class M43<bits<6> x6, string mnemonic, string file>
    : MSys1Inst<x6, (outs GR:$r1), (ins GR:$r3), mnemonic,
                "$r1 = "#file#"[$r3]"> {
  bits<7> r1;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{12-6} = r1;
}

// M44: set and reset the user or system mask.
class M44<bits<4> x4, string mnemonic>
    : MInst<(outs), (ins uimm24:$imm), mnemonic, "$imm"> {
  bits<24> imm;
  let Opcode = 0;
  let Inst{36} = imm{23};
  let Inst{35-33} = 0;
  let Inst{32-31} = imm{22-21};
  let Inst{30-27} = x4;
  let Inst{26-6} = imm{20-0};
}

// M45: purge translation entries.
class M45<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs), (ins GR:$r3, GR:$r2), mnemonic, "$r3, $r2"> {
  bits<7> r2;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{19-13} = r2;
}

// M46: translation access.
class M46<bits<6> x6, string mnemonic>
    : MSys1Inst<x6, (outs GR:$r1), (ins GR:$r3), mnemonic, "$r1 = $r3"> {
  bits<7> r1;
  bits<7> r3;
  let Inst{26-20} = r3;
  let Inst{12-6} = r1;
}

// M47: purge a translation cache entry.
class M47<string mnemonic>
    : MSys1Inst<0x34, (outs), (ins GR:$r3), mnemonic, "$r3"> {
  bits<7> r3;
  let Inst{26-20} = r3;
}

//===----------------------------------------------------------------------===//
// F-unit formats (SDM volume 3, section 4.6)
//===----------------------------------------------------------------------===//

class FInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteFMisc];
  let Unit = UnitF;
  let DecoderNamespace = "F";
}

// An instruction that names its status field as the last mnemonic
// completer. The field is in ar.fpsr and selects the rounding mode and which
// FP exceptions trap.
class FSFInst<dag oops, dag iops, string mnemonic, string operands>
    : FInst<oops, !con(iops, (ins sfield:$sf)), mnemonic, operands> {
  bits<2> sf;
  let Uses = [AR40];
  let mayRaiseFPException = 1;
  let Inst{35-34} = sf;
  let AsmString = mnemonic#"${qp}${sf}"#
                  !if(!empty(operands), "", "\t"#operands);
}

// F1: floating-point multiply and add, f1 = f3 * f4 + f2.
class F1<bits<4> op, bit x, string mnemonic, RegisterClass Addend = FR>
    : FSFInst<(outs FR:$f1), (ins FR:$f3, FR:$f4, Addend:$f2), mnemonic,
              "$f1 = $f3, $f4, $f2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFMA];
  bits<7> f1;
  bits<7> f2;
  bits<7> f3;
  bits<7> f4;
  let Opcode = op;
  let Inst{36} = x;
  let Inst{33-27} = f4;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}

// F2: fixed-point multiply and add; F3: parallel select. They share major
// opcode 0xE and have no status field.
class FOpEInst<bit x, string mnemonic>
    : FInst<(outs FR:$f1), (ins FR:$f3, FR:$f4, FR:$f2), mnemonic,
            "$f1 = $f3, $f4, $f2"> {
  let hasSideEffects = 0;
  bits<7> f1;
  bits<7> f2;
  bits<7> f3;
  bits<7> f4;
  let Opcode = 0xe;
  let Inst{36} = x;
  let Inst{33-27} = f4;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}
class F2<bits<2> x2, string mnemonic> : FOpEInst<1, mnemonic> {
  let SchedRW = [WriteXMA];
  let Inst{35-34} = x2;
}
class F3<string mnemonic> : FOpEInst<0, mnemonic>;

// F4: floating-point compare.
class F4<bit rb, bit ra, bit ta, string mnemonic>
    : FSFInst<(outs PR:$p1, PR:$p2), (ins FR:$f2, FR:$f3), mnemonic,
              "$p1, $p2 = $f2, $f3"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFCmp, WriteFPred2];
  bits<6> p1;
  bits<6> p2;
  bits<7> f2;
  bits<7> f3;
  let Opcode = 4;
  let Inst{36} = rb;
  let Inst{33} = ra;
  let Inst{32-27} = p2;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12} = ta;
  let Inst{11-6} = p1;
}

// F5: floating-point class. The low two bits of the 9-bit class mask are
// stored separately from the rest.
class F5<bit ta, string mnemonic>
    : FInst<(outs PR:$p1, PR:$p2), (ins FR:$f2, uimm9:$fclass), mnemonic,
            "$p1, $p2 = $f2, $fclass"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFCmp, WriteFPred2];
  bits<6> p1;
  bits<6> p2;
  bits<7> f2;
  bits<9> fclass;
  let Opcode = 5;
  let Inst{34-33} = fclass{1-0};
  let Inst{32-27} = p2;
  let Inst{26-20} = fclass{8-2};
  let Inst{19-13} = f2;
  let Inst{12} = ta;
  let Inst{11-6} = p1;
}

// F6: reciprocal approximation; F7: reciprocal square root approximation.
// The major opcode is 0 for the scalar form and 1 for the parallel form, as
// in all following formats.
class F6<bits<4> op, string mnemonic>
    : FSFInst<(outs FR:$f1, PR:$p2), (ins FR:$f2, FR:$f3), mnemonic,
              "$f1, $p2 = $f2, $f3"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFRecip, WriteFRecipPred];
  bits<7> f1;
  bits<6> p2;
  bits<7> f2;
  bits<7> f3;
  let Opcode = op;
  let Inst{36} = 0;
  let Inst{33} = 1;
  let Inst{32-27} = p2;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}
class F7<bits<4> op, string mnemonic>
    : FSFInst<(outs FR:$f1, PR:$p2), (ins FR:$f3), mnemonic,
              "$f1, $p2 = $f3"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFRecip, WriteFRecipPred];
  bits<7> f1;
  bits<6> p2;
  bits<7> f3;
  let Opcode = op;
  let Inst{36} = 1;
  let Inst{33} = 1;
  let Inst{32-27} = p2;
  let Inst{26-20} = f3;
  let Inst{12-6} = f1;
}

// F8: minimum, maximum and parallel compare.
class F8<bits<4> op, bits<6> x6, string mnemonic>
    : FSFInst<(outs FR:$f1), (ins FR:$f2, FR:$f3), mnemonic,
              "$f1 = $f2, $f3"> {
  let hasSideEffects = 0;
  bits<7> f1;
  bits<7> f2;
  bits<7> f3;
  let Opcode = op;
  let Inst{33} = 0;
  let Inst{32-27} = x6;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}

// F9: merge, mix, sign extend, pack, swap and the logical operations.
class F9<bits<4> op, bits<6> x6, string mnemonic>
    : FInst<(outs FR:$f1), (ins FR:$f2, FR:$f3), mnemonic,
            "$f1 = $f2, $f3"> {
  let hasSideEffects = 0;
  bits<7> f1;
  bits<7> f2;
  bits<7> f3;
  let Opcode = op;
  let Inst{33} = 0;
  let Inst{32-27} = x6;
  let Inst{26-20} = f3;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}

// F10: convert floating-point to fixed-point; F11: convert a signed integer
// to floating-point, which raises no exceptions.
class F10<bits<4> op, bits<6> x6, string mnemonic>
    : FSFInst<(outs FR:$f1), (ins FR:$f2), mnemonic, "$f1 = $f2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFCvt];
  bits<7> f1;
  bits<7> f2;
  let Opcode = op;
  let Inst{33} = 0;
  let Inst{32-27} = x6;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}
class F11<string mnemonic>
    : FInst<(outs FR:$f1), (ins FR:$f2), mnemonic, "$f1 = $f2"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteFCvt];
  bits<7> f1;
  bits<7> f2;
  let Opcode = 0;
  let Inst{33} = 0;
  let Inst{32-27} = 0x1c;
  let Inst{19-13} = f2;
  let Inst{12-6} = f1;
}

// F12: set the controls of a status field; F13: clear its flags.
class F12<string mnemonic>
    : FSFInst<(outs), (ins uimm7:$amask, uimm7:$omask), mnemonic,
              "$amask, $omask"> {
  let SchedRW = [WriteSys];
  bits<7> amask;
  bits<7> omask;
  let Opcode = 0;
  let Inst{33} = 0;
  let Inst{32-27} = 0x04;
  let Inst{26-20} = omask;
  let Inst{19-13} = amask;
}
class F13<string mnemonic> : FSFInst<(outs), (ins), mnemonic, ""> {
  let SchedRW = [WriteSys];
  let Opcode = 0;
  let Inst{33} = 0;
  let Inst{32-27} = 0x05;
}

// F14: check the flags of a status field and branch to recovery code.
class F14<string mnemonic>
    : FSFInst<(outs), (ins target25f:$target), mnemonic, "$target"> {
  let SchedRW = [WriteChk];
  bits<21> target;
  let Opcode = 0;
  let Inst{36} = target{20};
  let Inst{33} = 0;
  let Inst{32-27} = 0x08;
  let Inst{25-6} = target{19-0};
}

// F15: break; F16: nop and hint.
class F15<bits<6> x6, string mnemonic>
    : FInst<(outs), (ins uimm21:$imm), mnemonic, "$imm"> {
  let SchedRW = [WriteSys];
  bits<21> imm;
  let Opcode = 0;
  let Inst{36} = imm{20};
  let Inst{33} = 0;
  let Inst{32-27} = x6;
  let Inst{25-6} = imm{19-0};
}
class F16<bit y, string mnemonic> : F15<1, mnemonic> {
  let SchedRW = [WriteNop];
  let Inst{26} = y;
}

//===----------------------------------------------------------------------===//
// B-unit formats (SDM volume 3, section 4.5)
//===----------------------------------------------------------------------===//

// Counted loop branches, branch predicts and the B8 instructions have no
// qualifying predicate, so the base class has none.
class BInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64Inst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteBr];
  let Unit = UnitB;
  let DecoderNamespace = "B";
}
class BQPInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteBr];
  let Unit = UnitB;
  let DecoderNamespace = "B";
}

// Fields common to all branches. The formats add the operands, ending with
// the three completers.
class BranchFields {
  bits<2> wh;
  bit ph;
  bit dh;
}
defvar BranchCompleters = (ins bwh:$wh, bph:$ph, bdh:$dh);

// B1: IP-relative branch.
class B1<bits<3> btype, string mnemonic>
    : BQPInst<(outs), !con((ins target25:$target), BranchCompleters),
              mnemonic, "$target">, BranchFields {
  bits<21> target;
  let Opcode = 4;
  let Inst{36} = target{20};
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-13} = target{19-0};
  let Inst{12} = ph;
  let Inst{8-6} = btype;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$target";
  let isBranch = 1;
  let isTerminator = 1;
}

// B2: IP-relative counted branch, which tests ar.lc or ar.ec instead of a
// predicate.
class B2<bits<3> btype, string mnemonic>
    : BInst<(outs), !con((ins target25:$target), BranchCompleters), mnemonic,
            "$target">, BranchFields {
  bits<21> target;
  let Opcode = 4;
  let Inst{36} = target{20};
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-13} = target{19-0};
  let Inst{12} = ph;
  let Inst{8-6} = btype;
  let Inst{5-0} = 0;
  let AsmString = mnemonic#"${wh}${ph}${dh}\t$target";
  let isBranch = 1;
  let isTerminator = 1;
  let LastSlot = 1;
}

// B3: IP-relative call.
class B3<string mnemonic>
    : BQPInst<(outs BR:$b1), !con((ins target25:$target), BranchCompleters),
              mnemonic, "$b1 = $target">, BranchFields {
  bits<3> b1;
  bits<21> target;
  let Opcode = 5;
  let Inst{36} = target{20};
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-13} = target{19-0};
  let Inst{12} = ph;
  let Inst{8-6} = b1;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$b1 = $target";
  let isCall = 1;
}

// B4: indirect branch and return.
class B4<bits<6> x6, bits<3> btype, string mnemonic>
    : BQPInst<(outs), !con((ins BR:$b2), BranchCompleters), mnemonic, "$b2">,
      BranchFields {
  bits<3> b2;
  let Opcode = 0;
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-27} = x6;
  let Inst{15-13} = b2;
  let Inst{12} = ph;
  let Inst{8-6} = btype;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$b2";
  let isTerminator = 1;
}

// B5: indirect call. Its hint field is three bits, with the low bit set in
// all four defined encodings.
class B5<string mnemonic>
    : BQPInst<(outs BR:$b1), !con((ins BR:$b2), BranchCompleters), mnemonic,
              "$b1 = $b2">, BranchFields {
  bits<3> b1;
  bits<3> b2;
  let Opcode = 1;
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32} = 1;
  let Inst{15-13} = b2;
  let Inst{12} = ph;
  let Inst{8-6} = b1;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$b1 = $b2";
  let isCall = 1;
}

// B6: IP-relative branch predict at a tag; B7: indirect branch predict.
// There are few enough hints to make them part of the mnemonic.
class B6<bits<2> wh, bit ih, string mnemonic>
    : BInst<(outs), (ins target25:$target, tag13:$tag), mnemonic,
            "$target, $tag"> {
  bits<21> target;
  bits<9> tag;
  let Opcode = 7;
  let Inst{36} = target{20};
  let Inst{35} = ih;
  let Inst{34-33} = tag{8-7};
  let Inst{32-13} = target{19-0};
  let Inst{12-6} = tag{6-0};
  let Inst{4-3} = wh;
}
class B7<bits<6> x6, bits<2> wh, bit ih, string mnemonic>
    : BInst<(outs), (ins BR:$b2, tag13:$tag), mnemonic, "$b2, $tag"> {
  bits<3> b2;
  bits<9> tag;
  let Opcode = 2;
  let Inst{35} = ih;
  let Inst{34-33} = tag{8-7};
  let Inst{32-27} = x6;
  let Inst{15-13} = b2;
  let Inst{12-6} = tag{6-0};
  let Inst{4-3} = wh;
}

// B8: the instructions without operands.
class B8<bits<6> x6, string mnemonic> : BInst<(outs), (ins), mnemonic, ""> {
  let Opcode = 0;
  let Inst{32-27} = x6;
  let Inst{5-0} = 0;
}

// B9: break, nop and hint.
class B9<bits<4> op, bits<6> x6, string mnemonic>
    : BQPInst<(outs), (ins uimm21:$imm), mnemonic, "$imm"> {
  bits<21> imm;
  let Opcode = op;
  let Inst{36} = imm{20};
  let Inst{32-27} = x6;
  let Inst{25-6} = imm{19-0};
}

//===----------------------------------------------------------------------===//
// X-unit formats
//===----------------------------------------------------------------------===//

// An X-unit instruction occupies the last two slots of an MLX bundle. Inst
// is the X slot, the second of them; the code emitter and the disassembler
// handle the L slot, which holds only immediate bits. Size must agree with
// Inst, so it covers only the X slot; the unit implies the second slot.
class XInst<dag outs, dag ins, string mnemonic, string operands>
    : IA64QPInst<outs, ins, mnemonic, operands, []> {
  let SchedRW = [WriteSys];
  let Unit = UnitX;
  let DecoderNamespace = "X";
}

// X1 and X5: break.x, nop.x and hint.x. The L slot holds imm{61-21}.
class XMiscInst<bits<6> x6, string mnemonic>
    : XInst<(outs), (ins uimm62:$imm), mnemonic, "$imm"> {
  bits<62> imm;
  let Opcode = 0;
  let Inst{36} = imm{20};
  let Inst{35-33} = 0;
  let Inst{32-27} = x6;
  let Inst{25-6} = imm{19-0};
}
class X1<string mnemonic> : XMiscInst<0, mnemonic> {
  let Inst{26} = 0;
}
class X5<bit y, string mnemonic> : XMiscInst<1, mnemonic> {
  let SchedRW = [WriteNop];
  let Inst{26} = y;
}

// X2: move long immediate. The L slot holds imm{62-22}.
class X2<string mnemonic>
    : XInst<(outs GR:$r1), (ins imm64:$imm), mnemonic, "$r1 = $imm"> {
  let hasSideEffects = 0;
  let SchedRW = [WriteMovL];
  bits<7> r1;
  bits<64> imm;
  let Opcode = 6;
  let Inst{36} = imm{63};
  let Inst{35-27} = imm{15-7};
  let Inst{26-22} = imm{20-16};
  let Inst{21} = imm{21};
  let Inst{20} = 0;
  let Inst{19-13} = imm{6-0};
  let Inst{12-6} = r1;
}

// X3: long branch. The displacement is 60 bits in units of bundles, and
// bits 40:2 of the L slot hold target{58-20}.
class X3<bits<3> btype, string mnemonic>
    : XInst<(outs), !con((ins target64:$target), BranchCompleters), mnemonic,
            "$target">, BranchFields {
  let SchedRW = [WriteBr];
  bits<60> target;
  let Opcode = 12;
  let Inst{36} = target{59};
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-13} = target{19-0};
  let Inst{12} = ph;
  let Inst{8-6} = btype;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$target";
  let isBranch = 1;
  let isTerminator = 1;
}

// X4: long call.
class X4<string mnemonic>
    : XInst<(outs BR:$b1), !con((ins target64:$target), BranchCompleters),
            mnemonic, "$b1 = $target">, BranchFields {
  let SchedRW = [WriteBr];
  bits<3> b1;
  bits<60> target;
  let Opcode = 13;
  let Inst{36} = target{59};
  let Inst{35} = dh;
  let Inst{34-33} = wh;
  let Inst{32-13} = target{19-0};
  let Inst{12} = ph;
  let Inst{8-6} = b1;
  let AsmString = mnemonic#"${qp}${wh}${ph}${dh}\t$b1 = $target";
  let isCall = 1;
}

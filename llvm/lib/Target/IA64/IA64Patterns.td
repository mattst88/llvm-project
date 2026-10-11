//===-- IA64Patterns.td - IA-64 selection patterns ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SelectionDAG selection patterns. The instruction definitions describe the
// ISA and carry no patterns. Every instruction's last operand is its
// qualifying predicate, which patterns omit to get p0 (always true).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target nodes
//===----------------------------------------------------------------------===//

// A return, glued to the copies into the return value registers.
def IA64retglue : SDNode<"IA64ISD::RET_GLUE", SDTypeProfile<0, 0, []>,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// The address of a symbol's linkage table entry: @ltoff(sym) + gp.
def IA64ltoff : SDNode<"IA64ISD::LTOFF", SDTIntUnaryOp>;

// Move the bits of an f32 to/from the low half of a GR.
def IA64getfs
    : SDNode<"IA64ISD::GETF_S",
             SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, f32>]>>;
def IA64setfs
    : SDNode<"IA64ISD::SETF_S",
             SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisVT<1, i64>]>>;

// A call: the callee, the register holding the GP to restore, and the
// argument registers.
def IA64call : SDNode<"IA64ISD::CALL",
                      SDTypeProfile<0, -1, [SDTCisVT<0, i64>, SDTCisVT<1, i64>]>,
                      [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def IA64call_ind
    : SDNode<"IA64ISD::CALL_IND",
             SDTypeProfile<0, -1, [SDTCisVT<0, i64>, SDTCisVT<1, i64>,
                                   SDTCisVT<2, i64>]>,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Immediates
//===----------------------------------------------------------------------===//

def imm8  : ImmLeaf<i64, [{ return isInt<8>(Imm); }]>;
def imm14 : ImmLeaf<i64, [{ return isInt<14>(Imm); }]>;
def imm22 : ImmLeaf<i64, [{ return isInt<22>(Imm); }]>;

// A shift count, and 64 minus the count.
def shamt : ImmLeaf<i64, [{ return isUInt<6>(Imm); }]>;
def RestOf64 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(64 - N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;
// The shladd shift count: 1 to 4.
def shladdamt : ImmLeaf<i64, [{ return Imm >= 1 && Imm <= 4; }]>;

//===----------------------------------------------------------------------===//
// Branches and returns
//===----------------------------------------------------------------------===//

// br.ret.sptk.many b0
def : Pat<(IA64retglue), (BR_RET B0, 0, 1, 0)>;
def : Pat<(br bb:$target), (BRUNCOND bb:$target)>;
def : Pat<(brind GR:$target), (BRIND GR:$target)>;

def : Pat<(IA64call tglobaladdr:$target, GRSave:$gp),
          (CALL tglobaladdr:$target, GRSave:$gp)>;
def : Pat<(IA64call texternalsym:$target, GRSave:$gp),
          (CALL texternalsym:$target, GRSave:$gp)>;
def : Pat<(IA64call_ind GR:$entry, GR:$calleegp, GRSave:$gp),
          (CALL_IND GR:$entry, GR:$calleegp, GRSave:$gp)>;

//===----------------------------------------------------------------------===//
// Integer arithmetic
//===----------------------------------------------------------------------===//

def : Pat<(add GR:$r2, GR:$r3), (ADD GR:$r2, GR:$r3)>;
def : Pat<(sub GR:$r2, GR:$r3), (SUB GR:$r2, GR:$r3)>;
def : Pat<(and GR:$r2, GR:$r3), (AND GR:$r2, GR:$r3)>;
def : Pat<(or GR:$r2, GR:$r3), (OR GR:$r2, GR:$r3)>;
def : Pat<(xor GR:$r2, GR:$r3), (XOR GR:$r2, GR:$r3)>;

// The integer multiplier is in the FP unit and operates on the significands
// of FP registers, so a product costs a round trip through setf.sig/getf.sig.
// The addend is f0.
def : Pat<(mul GR:$a, GR:$b),
          (GETF_SIG (f64 (XMA_L (f64 (SETF_SIG GR:$a)),
                                (f64 (SETF_SIG GR:$b)), (f64 F0))))>;
def : Pat<(mulhu GR:$a, GR:$b),
          (GETF_SIG (f64 (XMA_HU (f64 (SETF_SIG GR:$a)),
                                 (f64 (SETF_SIG GR:$b)), (f64 F0))))>;
def : Pat<(mulhs GR:$a, GR:$b),
          (GETF_SIG (f64 (XMA_H (f64 (SETF_SIG GR:$a)),
                                (f64 (SETF_SIG GR:$b)), (f64 F0))))>;

// andcm computes r2 & ~r3.
def : Pat<(and GR:$r2, (not GR:$r3)), (ANDCM GR:$r2, GR:$r3)>;

// add can add an extra 1, and sub can subtract an extra 1, which is the same
// as adding the complement.
def : Pat<(add (add GR:$r2, GR:$r3), 1), (ADD1 GR:$r2, GR:$r3)>;
def : Pat<(add GR:$r2, (not GR:$r3)), (SUB1 GR:$r2, GR:$r3)>;

// Immediate forms. The immediate is always the first instruction operand,
// whichever side of the operation it is on. Only add takes more than 8 bits,
// and it takes more than 14 only with an r0-r3 base, which is of no use for
// a value in an arbitrary register.
def : Pat<(add GR:$r3, imm14:$imm), (ADDS imm14:$imm, GR:$r3)>;
def : Pat<(sub imm8:$imm, GR:$r3), (SUBi imm8:$imm, GR:$r3)>;
def : Pat<(and GR:$r3, imm8:$imm), (ANDi imm8:$imm, GR:$r3)>;
def : Pat<(or GR:$r3, imm8:$imm), (ORi imm8:$imm, GR:$r3)>;
def : Pat<(xor GR:$r3, imm8:$imm), (XORi imm8:$imm, GR:$r3)>;
def : Pat<(and (not GR:$r3), imm8:$imm), (ANDCMi imm8:$imm, GR:$r3)>;

//===----------------------------------------------------------------------===//
// Floating-point arithmetic
//===----------------------------------------------------------------------===//

// FP arithmetic is all fma: an add multiplies by f1 (1.0) and a multiply
// adds f0 (0.0). It rounds to the result's precision using status field 0.
foreach T = [["f32", "_S"], ["f64", "_D"]] in {
  defvar VT = !cast<ValueType>(T[0]);
  defvar FMA = !cast<Instruction>("FMA"#T[1]);
  defvar FMS = !cast<Instruction>("FMS"#T[1]);
  defvar FNMA = !cast<Instruction>("FNMA"#T[1]);
  def : Pat<(VT (fadd FR:$a, FR:$b)), (FMA FR:$a, (VT F1), FR:$b, 0)>;
  def : Pat<(VT (fsub FR:$a, FR:$b)), (FMS FR:$a, (VT F1), FR:$b, 0)>;
  def : Pat<(VT (fmul FR:$a, FR:$b)), (FMA FR:$a, FR:$b, (VT F0), 0)>;
  def : Pat<(VT (fneg (fmul FR:$a, FR:$b))),
            (FNMA FR:$a, FR:$b, (VT F0), 0)>;
  def : Pat<(VT (fma FR:$a, FR:$b, FR:$c)), (FMA FR:$a, FR:$b, FR:$c, 0)>;
  def : Pat<(VT (fma FR:$a, FR:$b, (fneg FR:$c))),
            (FMS FR:$a, FR:$b, FR:$c, 0)>;
  def : Pat<(VT (fma (fneg FR:$a), FR:$b, FR:$c)),
            (FNMA FR:$a, FR:$b, FR:$c, 0)>;

  // fmerge.s takes the sign from the first operand and the exponent and
  // significand from the second; fmerge.ns inverts the sign. f0 has a clear
  // sign bit.
  def : Pat<(VT (fneg FR:$f)), (FMERGE_NS FR:$f, FR:$f)>;
  def : Pat<(VT (fabs FR:$f)), (FMERGE_S (VT F0), FR:$f)>;
  def : Pat<(VT (fneg (fabs FR:$f))), (FMERGE_NS (VT F0), FR:$f)>;
  foreach ST = [f32, f64] in {
    def : Pat<(VT (fcopysign FR:$mag, (ST FR:$sign))),
              (FMERGE_S FR:$sign, FR:$mag)>;
    def : Pat<(VT (fcopysign FR:$mag, (fneg (ST FR:$sign)))),
              (FMERGE_NS FR:$sign, FR:$mag)>;
  }
}

//===----------------------------------------------------------------------===//
// Floating-point conversions
//===----------------------------------------------------------------------===//

// Integer conversions happen in an FP register, with the integer as the
// significand, moved to and from GRs with setf.sig/getf.sig.
foreach VT = [f32, f64] in {
  def : Pat<(i64 (fp_to_sint (VT FR:$f))),
            (GETF_SIG (f64 (FCVT_FX_TRUNC FR:$f, 0)))>;
  def : Pat<(i64 (fp_to_uint (VT FR:$f))),
            (GETF_SIG (f64 (FCVT_FXU_TRUNC FR:$f, 0)))>;
}

// In the other direction the 64-bit significand holds the integer exactly,
// and is then rounded to the target precision. An unsigned integer is
// already a valid, if unnormalized, FP value.
foreach T = [["f32", "_S"], ["f64", "_D"]] in {
  defvar VT = !cast<ValueType>(T[0]);
  defvar FMA = !cast<Instruction>("FMA"#T[1]);
  def : Pat<(VT (sint_to_fp GR:$r)),
            (FMA (f64 (FCVT_XF (f64 (SETF_SIG GR:$r)))), (f64 F1), (f64 F0),
             0)>;
  def : Pat<(VT (uint_to_fp GR:$r)),
            (FMA (f64 (SETF_SIG GR:$r)), (f64 F1), (f64 F0), 0)>;
}

// A narrower value is a wider one already, and a wider one is rounded to
// make a narrower.
def : Pat<(f64 (fpextend (f32 FR:$f))), (COPY_TO_REGCLASS FR:$f, FR)>;
def : Pat<(f32 (fpround (f64 FR:$f))), (FMA_S FR:$f, (f64 F1), (f64 F0), 0)>;

// Bitcasts use the memory format.
def : Pat<(i64 (bitconvert (f64 FR:$f))), (GETF_D FR:$f)>;
def : Pat<(f64 (bitconvert GR:$r)), (SETF_D GR:$r)>;
def : Pat<(IA64getfs FR:$f), (GETF_S FR:$f)>;
def : Pat<(IA64setfs GR:$r), (SETF_S GR:$r)>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

// A register count above 63 shifts everything out, which is stronger than
// what ISD requires.
def : Pat<(shl GR:$r2, GR:$r3), (SHL GR:$r2, GR:$r3)>;
def : Pat<(srl GR:$r3, GR:$r2), (SHR_U GR:$r3, GR:$r2)>;
def : Pat<(sra GR:$r3, GR:$r2), (SHR GR:$r3, GR:$r2)>;

// Constant shifts: shl is dep.z of the surviving bits at the count, and
// shifts right are extr/extr.u from the count.
def : Pat<(shl GR:$r2, shamt:$count),
          (DEP_Z GR:$r2, shamt:$count, (RestOf64 shamt:$count))>;
def : Pat<(srl GR:$r3, shamt:$count),
          (EXTR_U GR:$r3, shamt:$count, (RestOf64 shamt:$count))>;
def : Pat<(sra GR:$r3, shamt:$count),
          (EXTR GR:$r3, shamt:$count, (RestOf64 shamt:$count))>;

// shladd folds a left shift of 1 to 4 into an add, for scaled indexing.
def : Pat<(add (shl GR:$r2, shladdamt:$count), GR:$r3),
          (SHLADD GR:$r2, shladdamt:$count, GR:$r3)>;

//===----------------------------------------------------------------------===//
// Bits and bytes
//===----------------------------------------------------------------------===//

def : Pat<(ctpop GR:$r3), (POPCNT GR:$r3)>;

// mux1 @rev reverses the 8 bytes of a register.
def : Pat<(bswap GR:$r2), (MUX1 GR:$r2, 0xb)>;

//===----------------------------------------------------------------------===//
// Extension
//===----------------------------------------------------------------------===//

def : Pat<(sext_inreg GR:$r3, i8), (SXT1 GR:$r3)>;
def : Pat<(sext_inreg GR:$r3, i16), (SXT2 GR:$r3)>;
def : Pat<(sext_inreg GR:$r3, i32), (SXT4 GR:$r3)>;
def : Pat<(and GR:$r3, 0xff), (ZXT1 GR:$r3)>;
def : Pat<(and GR:$r3, 0xffff), (ZXT2 GR:$r3)>;
def : Pat<(and GR:$r3, 0xffffffff), (ZXT4 GR:$r3)>;

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

// A 22-bit constant is addl with r0, the "mov" pseudo-op. Anything else
// needs movl, which takes two slots.
def : Pat<(i64 imm22:$imm), (ADDL imm22:$imm, R0)>;
def : Pat<(i64 imm:$imm), (MOVL imm:$imm)>;

//===----------------------------------------------------------------------===//
// Loads and stores
//===----------------------------------------------------------------------===//

// Addresses are a bare register: no displacement and no index. Narrow loads
// zero-extend, so they serve both zextload and extload.
def : Pat<(i64 (load GR:$r3)), (LD8 GR:$r3, 0)>;
foreach S = ["1", "2", "4"] in {
  defvar Bits = !cond(!eq(S, "1") : "8", !eq(S, "2") : "16", true : "32");
  def : Pat<(i64 (!cast<PatFrag>("zextloadi"#Bits) GR:$r3)),
            (!cast<Instruction>("LD"#S) GR:$r3, 0)>;
  def : Pat<(i64 (!cast<PatFrag>("extloadi"#Bits) GR:$r3)),
            (!cast<Instruction>("LD"#S) GR:$r3, 0)>;
  def : Pat<(!cast<PatFrag>("truncstorei"#Bits) GR:$r2, GR:$r3),
            (!cast<Instruction>("ST"#S) GR:$r3, GR:$r2, 0)>;
}
def : Pat<(store GR:$r2, GR:$r3), (ST8 GR:$r3, GR:$r2, 0)>;

// A floating-point load widens what it reads to the format of the register,
// so a single is as good as a double once it is in one.
def : Pat<(f32 (load GR:$r3)), (LDFS GR:$r3, 0)>;
def : Pat<(f64 (load GR:$r3)), (LDFD GR:$r3, 0)>;
def : Pat<(f64 (extloadf32 GR:$r3)), (LDFS GR:$r3, 0)>;
def : Pat<(store (f32 FR:$f2), GR:$r3), (STFS GR:$r3, FR:$f2, 0)>;
def : Pat<(store (f64 FR:$f2), GR:$r3), (STFD GR:$r3, FR:$f2, 0)>;

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

// addl r1 = @ltoff(sym), gp
def : Pat<(IA64ltoff tglobaladdr:$sym), (ADDL tglobaladdr:$sym, R1)>;
def : Pat<(IA64ltoff tjumptable:$sym), (ADDL tjumptable:$sym, R1)>;
def : Pat<(IA64ltoff tblockaddress:$sym), (ADDL tblockaddress:$sym, R1)>;

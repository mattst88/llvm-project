//===-- IA64InstrAliases.td - IA-64 pseudo-ops -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The pseudo-ops from the architecture manual: alternate spellings of
// instructions defined elsewhere. Each is a fixed operand rearrangement, so
// the assembler accepts them. The instruction printer emits only the moves,
// which are what a reader expects to see.
//
// Three kinds are not here. "br" and "brl" without a whether hint, and
// shifts by an immediate, are rewritten by the assembly parser. Pseudo-ops
// that select a unit ("nop", "break", "hint", "chk.s", "mov" to and from an
// application register) depend on the slot the instruction lands in.
//
//===----------------------------------------------------------------------===//

class IA64Alias<string mnemonic, string operands, dag result, int emit = 0>
    : InstAlias<mnemonic#"${qp}\t"#operands, result, emit>;
class IA64SFAlias<string mnemonic, string operands, dag result>
    : InstAlias<mnemonic#"${qp}${sf}\t"#operands, result, 0>;

//===----------------------------------------------------------------------===//
// Moves and additions
//===----------------------------------------------------------------------===//

def : IA64Alias<"mov", "$r1 = $r3", (ADDS GR:$r1, 0, GR:$r3, qp:$qp), 1>;
def : IA64Alias<"mov", "$r1 = $imm",
                (ADDL GR:$r1, simm22:$imm, R0, qp:$qp), 1>;
def : IA64Alias<"mov", "$f1 = $f3", (FMERGE_S FR:$f1, FR:$f3, FR:$f3, qp:$qp), 1>;

// "add" with an immediate is addl when the register allows it and adds
// otherwise. The assembly parser picks between them when both fit.
def : IA64Alias<"add", "$r1 = $imm, $r3",
                (ADDL GR:$r1, simm22:$imm, GR0_3:$r3, qp:$qp)>;
def : IA64Alias<"add", "$r1 = $imm, $r3",
                (ADDS GR:$r1, simm14:$imm, GR:$r3, qp:$qp)>;

// A move to a BR may omit the branch target address hint.
def : IA64Alias<"mov", "$b1 = $r2", (MOV_BR BR:$b1, GR:$r2, 0, qp:$qp), 1>;

// A move to pr with no mask writes all predicates.
def : IA64Alias<"mov", "pr = $r2", (MOV_PR GR:$r2, -2, qp:$qp)>;

//===----------------------------------------------------------------------===//
// Memory accesses with implicit register operands omitted
//===----------------------------------------------------------------------===//

class IA64HintAlias<string mnemonic, string operands, dag result>
    : InstAlias<mnemonic#"${qp}${hint}\t"#operands, result, 0>;

// 16-byte accesses keep the other half in ar.csd, and cmpxchg compares
// against ar.ccv. GNU as accepts them without either operand.
def : IA64HintAlias<"ld16", "$r1 = [$r3]",
                    (LD16 GR:$r1, GR:$r3, dahint:$hint, qp:$qp)>;
def : IA64HintAlias<"ld16.acq", "$r1 = [$r3]",
                    (LD16_ACQ GR:$r1, GR:$r3, dahint:$hint, qp:$qp)>;
def : IA64HintAlias<"st16", "[$r3] = $r2",
                    (ST16 GR:$r3, GR:$r2, dahint:$hint, qp:$qp)>;
def : IA64HintAlias<"st16.rel", "[$r3] = $r2",
                    (ST16_REL GR:$r3, GR:$r2, dahint:$hint, qp:$qp)>;
foreach S = IntSizes in
  foreach K = ["acq", "rel"] in
    def : IA64HintAlias<"cmpxchg"#S.Name#"."#K, "$r1 = [$r3], $r2",
                        (!cast<Instruction>("CMPXCHG"#S.Name#"_"#!toupper(K))
                           GR:$r1, GR:$r3, GR:$r2, ldhint:$hint, qp:$qp)>;
foreach K = ["acq", "rel"] in
  def : IA64HintAlias<"cmp8xchg16."#K, "$r1 = [$r3], $r2",
                      (!cast<Instruction>("CMP8XCHG16_"#!toupper(K))
                         GR:$r1, GR:$r3, GR:$r2, ldhint:$hint, qp:$qp)>;

//===----------------------------------------------------------------------===//
// Integer compare
//===----------------------------------------------------------------------===//

// A relation's equivalent with its operands swapped, with its sense
// inverted, and with both.
class CmpPseudo<string name, string swapped, string inverted,
                string swappedinverted> {
  string Name = name;
  string Swapped = swapped;
  string Inverted = inverted;
  string SwappedInverted = swappedinverted;
}

class CmpComplemented<string name, string real, bit swap> {
  string Name = name;
  string Real = real;
  bit Swap = swap;
}
defvar ComplementedTypes = [CmpComplemented<"orcm", "or", 0>,
                            CmpComplemented<"andcm", "and", 0>,
                            CmpComplemented<"and.orcm", "or.andcm", 1>];

multiclass CmpAliases<string mnemonic, string prefix> {
  // Relations with no encoding of their own swap the operands, the
  // predicates, or both. Immediate operands cannot be swapped, so
  // "imm <= r" is encoded as "imm - 1 < r".
  foreach C = ["", ".unc"] in {
    defvar LT = !cast<Instruction>(prefix#"_LT"#!subst(".", "_", !toupper(C)));
    defvar LTU =
        !cast<Instruction>(prefix#"_LTU"#!subst(".", "_", !toupper(C)));
    defvar EQ = !cast<Instruction>(prefix#"_EQ"#!subst(".", "_", !toupper(C)));
    defvar LTi =
        !cast<Instruction>(prefix#"_LT"#!subst(".", "_", !toupper(C))#"i");
    defvar LTUi =
        !cast<Instruction>(prefix#"_LTU"#!subst(".", "_", !toupper(C))#"i");
    defvar EQi =
        !cast<Instruction>(prefix#"_EQ"#!subst(".", "_", !toupper(C))#"i");

    def : IA64Alias<mnemonic#".ne"#C, "$p1, $p2 = $r2, $r3",
                    (EQ PR:$p2, PR:$p1, GR:$r2, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".ne"#C, "$p1, $p2 = $imm, $r3",
                    (EQi PR:$p2, PR:$p1, simm8:$imm, GR:$r3, qp:$qp)>;

    def : IA64Alias<mnemonic#".le"#C, "$p1, $p2 = $r2, $r3",
                    (LT PR:$p2, PR:$p1, GR:$r3, GR:$r2, qp:$qp)>;
    def : IA64Alias<mnemonic#".gt"#C, "$p1, $p2 = $r2, $r3",
                    (LT PR:$p1, PR:$p2, GR:$r3, GR:$r2, qp:$qp)>;
    def : IA64Alias<mnemonic#".ge"#C, "$p1, $p2 = $r2, $r3",
                    (LT PR:$p2, PR:$p1, GR:$r2, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".le"#C, "$p1, $p2 = $imm, $r3",
                    (LTi PR:$p1, PR:$p2, simm8m1:$imm, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".gt"#C, "$p1, $p2 = $imm, $r3",
                    (LTi PR:$p2, PR:$p1, simm8m1:$imm, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".ge"#C, "$p1, $p2 = $imm, $r3",
                    (LTi PR:$p2, PR:$p1, simm8:$imm, GR:$r3, qp:$qp)>;

    def : IA64Alias<mnemonic#".leu"#C, "$p1, $p2 = $r2, $r3",
                    (LTU PR:$p2, PR:$p1, GR:$r3, GR:$r2, qp:$qp)>;
    def : IA64Alias<mnemonic#".gtu"#C, "$p1, $p2 = $r2, $r3",
                    (LTU PR:$p1, PR:$p2, GR:$r3, GR:$r2, qp:$qp)>;
    def : IA64Alias<mnemonic#".geu"#C, "$p1, $p2 = $r2, $r3",
                    (LTU PR:$p2, PR:$p1, GR:$r2, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".leu"#C, "$p1, $p2 = $imm, $r3",
                    (LTUi PR:$p1, PR:$p2, simm8m1nz:$imm, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".gtu"#C, "$p1, $p2 = $imm, $r3",
                    (LTUi PR:$p2, PR:$p1, simm8m1nz:$imm, GR:$r3, qp:$qp)>;
    def : IA64Alias<mnemonic#".geu"#C, "$p1, $p2 = $imm, $r3",
                    (LTUi PR:$p2, PR:$p1, simm8:$imm, GR:$r3, qp:$qp)>;
  }

  // The complemented parallel compares (orcm, andcm, and.orcm) invert the
  // relation; and.orcm also swaps the predicates.
  foreach T = ComplementedTypes in {
    foreach R = [["eq", "ne"], ["ne", "eq"]] in {
      defvar Suffix = !toupper(!subst(".", "_", R[1]#"."#T.Real));
      defvar I = !cast<Instruction>(prefix#"_"#Suffix);
      defvar Ii = !cast<Instruction>(prefix#"_"#Suffix#"i");
      def : IA64Alias<mnemonic#"."#R[0]#"."#T.Name, "$p1, $p2 = $r2, $r3",
                      !con(!if(T.Swap, (I PR:$p2, PR:$p1), (I PR:$p1, PR:$p2)),
                           (I GR:$r2, GR:$r3, qp:$qp))>;
      def : IA64Alias<mnemonic#"."#R[0]#"."#T.Name, "$p1, $p2 = $imm, $r3",
                      !con(!if(T.Swap, (Ii PR:$p2, PR:$p1),
                               (Ii PR:$p1, PR:$p2)),
                           (Ii simm8:$imm, GR:$r3, qp:$qp))>;
    }
  }

  // The relational parallel compares only compare against zero, which may
  // be written as either operand.
  foreach R = [CmpPseudo<"lt", "gt", "ge", "le">,
               CmpPseudo<"le", "ge", "gt", "lt">,
               CmpPseudo<"gt", "lt", "le", "ge">,
               CmpPseudo<"ge", "le", "lt", "gt">] in {
    foreach T = ["and", "or", "or.andcm"] in {
      defvar Suffix = !toupper(!subst(".", "_", R.Swapped#"."#T))#"z";
      def : IA64Alias<mnemonic#"."#R.Name#"."#T, "$p1, $p2 = $r3, r0",
                      (!cast<Instruction>(prefix#"_"#Suffix)
                           PR:$p1, PR:$p2, GR:$r3, qp:$qp)>;
    }
    foreach T = ComplementedTypes in {
      defvar I = !cast<Instruction>(
          prefix#"_"#!toupper(!subst(".", "_", R.Inverted#"."#T.Real))#"z");
      defvar SwappedI = !cast<Instruction>(
          prefix#"_"#!toupper(!subst(".", "_", R.SwappedInverted#"."#T.Real))#
          "z");
      def : IA64Alias<mnemonic#"."#R.Name#"."#T.Name, "$p1, $p2 = r0, $r3",
                      !con(!if(T.Swap, (I PR:$p2, PR:$p1), (I PR:$p1, PR:$p2)),
                           (I GR:$r3, qp:$qp))>;
      def : IA64Alias<mnemonic#"."#R.Name#"."#T.Name, "$p1, $p2 = $r3, r0",
                      !con(!if(T.Swap, (SwappedI PR:$p2, PR:$p1),
                               (SwappedI PR:$p1, PR:$p2)),
                           (SwappedI GR:$r3, qp:$qp))>;
    }
  }
}
defm : CmpAliases<"cmp", "CMP">;
defm : CmpAliases<"cmp4", "CMP4">;

//===----------------------------------------------------------------------===//
// Bit, NaT and feature tests
//===----------------------------------------------------------------------===//

// Testing for a one is testing for a zero with the predicates swapped, and a
// complemented parallel test is the opposite test.
class TestPseudo<string name, string real, bit swap> {
  string Name = name;
  string Real = real;
  bit Swap = swap;
}
foreach R = [TestPseudo<"nz",          "z",           1>,
             TestPseudo<"nz.unc",      "z.unc",       1>,
             TestPseudo<"z.orcm",      "nz.or",       1>,
             TestPseudo<"z.andcm",     "nz.and",      1>,
             TestPseudo<"z.and.orcm",  "nz.or.andcm", 1>,
             TestPseudo<"nz.orcm",     "z.or",        1>,
             TestPseudo<"nz.andcm",    "z.and",       1>,
             TestPseudo<"nz.and.orcm", "z.or.andcm",  1>] in {
  defvar Suffix = !toupper(!subst(".", "_", R.Real));
  defvar TBIT = !cast<Instruction>("TBIT_"#Suffix);
  defvar TNAT = !cast<Instruction>("TNAT_"#Suffix);
  defvar TF = !cast<Instruction>("TF_"#Suffix);
  def : IA64Alias<"tbit."#R.Name, "$p1, $p2 = $r3, $pos",
                  !con(!if(R.Swap, (TBIT PR:$p2, PR:$p1),
                           (TBIT PR:$p1, PR:$p2)),
                       (TBIT GR:$r3, uimm6:$pos, qp:$qp))>;
  def : IA64Alias<"tnat."#R.Name, "$p1, $p2 = $r3",
                  !con(!if(R.Swap, (TNAT PR:$p2, PR:$p1),
                           (TNAT PR:$p1, PR:$p2)),
                       (TNAT GR:$r3, qp:$qp))>;
  def : IA64Alias<"tf."#R.Name, "$p1, $p2 = $imm",
                  !con(!if(R.Swap, (TF PR:$p2, PR:$p1), (TF PR:$p1, PR:$p2)),
                       (TF tfimm5:$imm, qp:$qp))>;
}

//===----------------------------------------------------------------------===//
// Floating point
//===----------------------------------------------------------------------===//

// Addition and multiplication are a multiply-add with one operand fixed: f1
// holds 1.0 and f0 holds 0.0.
foreach P = ["", "_S", "_D"] in {
  defvar C = !tolower(!subst("_", ".", P));
  def : IA64SFAlias<"fadd"#C, "$f1 = $f3, $f2",
                    (!cast<Instruction>("FMA"#P)
                         FR:$f1, FR:$f3, F1, FR:$f2, sfield:$sf, qp:$qp)>;
  def : IA64SFAlias<"fsub"#C, "$f1 = $f3, $f2",
                    (!cast<Instruction>("FMS"#P)
                         FR:$f1, FR:$f3, F1, FR:$f2, sfield:$sf, qp:$qp)>;
  def : IA64SFAlias<"fmpy"#C, "$f1 = $f3, $f4",
                    (!cast<Instruction>("FMA"#P)
                         FR:$f1, FR:$f3, FR:$f4, F0, sfield:$sf, qp:$qp)>;
  def : IA64SFAlias<"fnmpy"#C, "$f1 = $f3, $f4",
                    (!cast<Instruction>("FNMA"#P)
                         FR:$f1, FR:$f3, FR:$f4, F0, sfield:$sf, qp:$qp)>;
  def : IA64SFAlias<"fnorm"#C, "$f1 = $f3",
                    (!cast<Instruction>("FMA"#P)
                         FR:$f1, FR:$f3, F1, F0, sfield:$sf, qp:$qp)>;
  def : IA64SFAlias<"fcvt.xuf"#C, "$f1 = $f3",
                    (!cast<Instruction>("FMA"#P)
                         FR:$f1, FR:$f3, F1, F0, sfield:$sf, qp:$qp)>;
}
def : IA64SFAlias<"fpmpy", "$f1 = $f3, $f4",
                  (FPMA FR:$f1, FR:$f3, FR:$f4, F0, sfield:$sf, qp:$qp)>;
def : IA64SFAlias<"fpnmpy", "$f1 = $f3, $f4",
                  (FPNMA FR:$f1, FR:$f3, FR:$f4, F0, sfield:$sf, qp:$qp)>;

def : IA64Alias<"xma.lu", "$f1 = $f3, $f4, $f2",
                (XMA_L FR:$f1, FR:$f3, FR:$f4, FR:$f2, qp:$qp)>;
def : IA64Alias<"xmpy.l", "$f1 = $f3, $f4",
                (XMA_L FR:$f1, FR:$f3, FR:$f4, F0, qp:$qp)>;
def : IA64Alias<"xmpy.lu", "$f1 = $f3, $f4",
                (XMA_L FR:$f1, FR:$f3, FR:$f4, F0, qp:$qp)>;
def : IA64Alias<"xmpy.h", "$f1 = $f3, $f4",
                (XMA_H FR:$f1, FR:$f3, FR:$f4, F0, qp:$qp)>;
def : IA64Alias<"xmpy.hu", "$f1 = $f3, $f4",
                (XMA_HU FR:$f1, FR:$f3, FR:$f4, F0, qp:$qp)>;

// The sign is merged from f0, which is positive, or from the operand itself.
def : IA64Alias<"fabs", "$f1 = $f3", (FMERGE_S FR:$f1, F0, FR:$f3, qp:$qp)>;
def : IA64Alias<"fneg", "$f1 = $f3",
                (FMERGE_NS FR:$f1, FR:$f3, FR:$f3, qp:$qp)>;
def : IA64Alias<"fnegabs", "$f1 = $f3",
                (FMERGE_NS FR:$f1, F0, FR:$f3, qp:$qp)>;
def : IA64Alias<"fpabs", "$f1 = $f3", (FPMERGE_S FR:$f1, F0, FR:$f3, qp:$qp)>;
def : IA64Alias<"fpneg", "$f1 = $f3",
                (FPMERGE_NS FR:$f1, FR:$f3, FR:$f3, qp:$qp)>;
def : IA64Alias<"fpnegabs", "$f1 = $f3",
                (FPMERGE_NS FR:$f1, F0, FR:$f3, qp:$qp)>;

// Relations with no encoding swap the operands, the predicates, or both.
class FCmpPseudo<string name, string real, bit swappreds, bit swapops> {
  string Name = name;
  string Real = real;
  bit SwapPreds = swappreds;
  bit SwapOps = swapops;
}
foreach R = [FCmpPseudo<"gt",  "LT",    0, 1>, FCmpPseudo<"ge",  "LE",    0, 1>,
             FCmpPseudo<"neq", "EQ",    1, 0>, FCmpPseudo<"nlt", "LT",    1, 0>,
             FCmpPseudo<"nle", "LE",    1, 0>, FCmpPseudo<"ngt", "LT",    1, 1>,
             FCmpPseudo<"nge", "LE",    1, 1>,
             FCmpPseudo<"ord", "UNORD", 1, 0>] in {
  foreach C = ["", ".unc"] in {
    defvar I = !cast<Instruction>(
        "FCMP_"#R.Real#!toupper(!subst(".", "_", C)));
    def : IA64SFAlias<"fcmp."#R.Name#C, "$p1, $p2 = $f2, $f3",
                      !con(!if(R.SwapPreds, (I PR:$p2, PR:$p1),
                               (I PR:$p1, PR:$p2)),
                           !if(R.SwapOps, (I FR:$f3, FR:$f2),
                               (I FR:$f2, FR:$f3)),
                           (I sfield:$sf, qp:$qp))>;
  }
}
def : IA64Alias<"fclass.nm", "$p1, $p2 = $f2, $fclass",
                (FCLASS_M PR:$p2, PR:$p1, FR:$f2, uimm9:$fclass, qp:$qp)>;
def : IA64Alias<"fclass.nm.unc", "$p1, $p2 = $f2, $fclass",
                (FCLASS_M_UNC PR:$p2, PR:$p1, FR:$f2, uimm9:$fclass, qp:$qp)>;

foreach R = [["gt", "LT"], ["ge", "LE"], ["ngt", "NLT"], ["nge", "NLE"]] in
  def : IA64SFAlias<"fpcmp."#R[0], "$f1 = $f2, $f3",
                    (!cast<Instruction>("FPCMP_"#R[1])
                         FR:$f1, FR:$f3, FR:$f2, sfield:$sf, qp:$qp)>;

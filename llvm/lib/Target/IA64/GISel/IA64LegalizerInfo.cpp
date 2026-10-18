//===-- IA64LegalizerInfo.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the LegalizerInfo class for IA64.
//
// The general registers operate only on 64-bit integers. float is s32 and
// double is s64, so s32 is also legal, but only for moves: loads, stores,
// selects and extensions. Extended precision is s80, and s80 is never anything
// else.
//
//===----------------------------------------------------------------------===//

#include "IA64LegalizerInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;
using namespace TargetOpcode;
using namespace LegalityPredicates;

IA64LegalizerInfo::IA64LegalizerInfo(const IA64Subtarget &ST) {
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  const LLT s32 = LLT::scalar(32);
  const LLT s64 = LLT::scalar(64);
  const LLT s80 = LLT::scalar(80);
  const LLT s128 = LLT::scalar(128);
  const LLT p0 = LLT::pointer(0, 64);

  // Values wider than a register are widened to a multiple of 64 bits before
  // being split into registers: clamping first would leave a leftover piece
  // that no rule covers.
  getActionDefinitionsBuilder({G_ADD, G_SUB, G_AND, G_OR, G_XOR})
      .legalFor({s64})
      .scalarize(0)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);

  // Integer multiply is xma on the significands of two FP registers, which
  // can produce either half of the product.
  getActionDefinitionsBuilder(G_MUL)
      .legalFor({s64})
      .scalarize(0)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);
  // The high half of a narrower product comes from the widened product.
  getActionDefinitionsBuilder({G_UMULH, G_SMULH})
      .legalFor({s64})
      .scalarize(0)
      .lower();
  getActionDefinitionsBuilder({G_SMULO, G_UMULO}).lower();

  // There is no integer divide instruction.
  getActionDefinitionsBuilder({G_SDIV, G_UDIV, G_SREM, G_UREM})
      .scalarize(0)
      .minScalar(0, s64)
      .widenScalarToNextPow2(0)
      .libcallFor({s64, s128})
      .unsupported();
  getActionDefinitionsBuilder({G_SDIVREM, G_UDIVREM}).lower();

  getActionDefinitionsBuilder({G_SHL, G_LSHR, G_ASHR})
      .legalFor({{s64, s64}})
      .scalarize(0)
      .clampScalar(1, s64, s64)
      // Shifts are narrowed by halving, which reaches 64 bits only from a
      // power-of-two width; a 192-bit shift would not.
      .widenScalarToNextPow2(0, 64)
      .clampScalar(0, s64, s64);
  getActionDefinitionsBuilder({G_FSHL, G_FSHR, G_ROTL, G_ROTR}).lower();

  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({s64, p0})
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);

  getActionDefinitionsBuilder({G_FRAME_INDEX, G_BLOCK_ADDR, G_CONSTANT_POOL})
      .legalFor({p0});
  // A TLS address may require a call.
  getActionDefinitionsBuilder(G_GLOBAL_VALUE).customFor({p0});

  getActionDefinitionsBuilder(G_PTR_ADD).legalFor({{p0, s64}});
  getActionDefinitionsBuilder(G_PTRMASK).customFor({{p0, s64}});
  getActionDefinitionsBuilder(G_INTTOPTR)
      .legalFor({{p0, s64}})
      .clampScalar(1, s64, s64);
  getActionDefinitionsBuilder(G_PTRTOINT)
      .legalFor({{s64, p0}})
      .clampScalar(0, s64, s64);

  // The MMO alignment is a hard requirement, not a preference: misaligned
  // accesses fault and are emulated by the kernel at a high cost. Misaligned
  // accesses are split into aligned ones.
  //
  // Extending loads zero-extend, and truncating stores store the low bits.
  //
  // Values wider than a register whose size is not a power-of-two number of
  // bytes are split by lowering before any narrowing: narrowing keeps the
  // original access size and loses the bytes past the first piece.
  auto WiderThanMemory = [=](const LegalityQuery &Q) {
    unsigned Mem = Q.MMODescrs[0].MemoryTy.getSizeInBits();
    return Q.Types[0].getScalarSizeInBits() > std::max(Mem, 64u);
  };
  auto ToMemoryWidth = [=](const LegalityQuery &Q) {
    return std::make_pair(0,
                          LLT::scalar(Q.MMODescrs[0].MemoryTy.getSizeInBits()));
  };
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalForTypesWithMemDesc({{s64, p0, s8, 8},
                                 {s64, p0, s16, 16},
                                 {s64, p0, s32, 32},
                                 {s64, p0, s64, 64},
                                 {s32, p0, s32, 32},
                                 {p0, p0, s64, 64},
                                 {s80, p0, s80, 128}})
      // s80 accesses that may be under-aligned go through an aligned stack
      // slot.
      .customIf([=](const LegalityQuery &Q) { return Q.Types[0] == s80; })
      // Vectors with non-byte-sized elements are bit-packed in memory and
      // are accessed as a single integer.
      .bitcastIf(
          [=](const LegalityQuery &Q) {
            return Q.Types[0].isFixedVector() &&
                   !Q.Types[0].getElementType().isByteSized();
          },
          [=](const LegalityQuery &Q) {
            return std::make_pair(0,
                                  LLT::scalar(Q.Types[0].getSizeInBits()));
          })
      .scalarize(0)
      .lowerIf([=](const LegalityQuery &Q) {
        return Q.Types[0].getScalarSizeInBits() > 64 && Q.Types[0] != s80 &&
               memSizeNotByteSizePow2(0)(Q);
      })
      .narrowScalarIf(WiderThanMemory, ToMemoryWidth)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64)
      .lower();

  // There is no sign-extending load; G_SEXTLOAD is selected as a load and an
  // extension.
  getActionDefinitionsBuilder({G_ZEXTLOAD, G_SEXTLOAD})
      .legalForTypesWithMemDesc(
          {{s64, p0, s8, 8}, {s64, p0, s16, 16}, {s64, p0, s32, 32}})
      .narrowScalarIf(WiderThanMemory, ToMemoryWidth)
      .clampScalar(0, s64, s64)
      .lower();

  // An s80 register value is always extended-precision FP. Conversions
  // between it and integers go through memory (stfe/ldfe).
  getActionDefinitionsBuilder({G_SEXT, G_ZEXT, G_ANYEXT})
      .customIf([=](const LegalityQuery &Query) {
        return Query.Types[1] == s80 && Query.Types[0].isScalar() &&
               Query.Types[0].getSizeInBits() % 64 == 0;
      })
      .legalIf([=](const LegalityQuery &Query) {
        const LLT Dst = Query.Types[0];
        const LLT Src = Query.Types[1];
        return Dst.isScalar() && Src.isScalar() && (Dst == s32 || Dst == s64) &&
               Src.getSizeInBits() < Dst.getSizeInBits();
      })
      .scalarize(0)
      .maxScalar(0, s64);
  getActionDefinitionsBuilder(G_TRUNC)
      .customIf([=](const LegalityQuery &Query) {
        return Query.Types[0] == s80 && Query.Types[1].isScalar() &&
               Query.Types[1].getSizeInBits() % 64 == 0;
      })
      .customIf([=](const LegalityQuery &Query) {
        return Query.Types[1] == s80 && Query.Types[0].isScalar() &&
               Query.Types[0].getSizeInBits() <= 64;
      })
      .legalIf(
          [=](const LegalityQuery &Query) { return Query.Types[0].isScalar(); })
      .scalarize(0);

  // extr extracts and sign-extends a field of any width.
  getActionDefinitionsBuilder(G_SEXT_INREG)
      .legalFor({s64})
      .widenScalarToNextMultipleOf(0, 64)
      .lower();

  // Compares produce 0 or 1 in a GR.
  getActionDefinitionsBuilder(G_ICMP)
      .legalFor({{s64, s64}, {s64, p0}})
      .scalarize(1)
      .scalarize(0)
      .widenScalarToNextMultipleOf(1, 64)
      .clampScalar(0, s64, s64)
      .clampScalar(1, s64, s64);
  getActionDefinitionsBuilder({G_SCMP, G_UCMP}).lower();

  getActionDefinitionsBuilder(G_BRCOND).legalFor({s64}).minScalar(0, s64);
  getActionDefinitionsBuilder(G_BR).alwaysLegal();
  getActionDefinitionsBuilder(G_BRINDIRECT).legalFor({p0});

  // Jump table entries are 32-bit offsets of the block from the table.
  getActionDefinitionsBuilder(G_JUMP_TABLE).legalFor({p0});
  getActionDefinitionsBuilder(G_BRJT).customFor({{p0, s64}});

  getActionDefinitionsBuilder(G_PHI)
      .legalFor({s32, s64, s80, p0})
      .scalarize(0)
      .minScalar(0, s32)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s32, s64);

  getActionDefinitionsBuilder(G_SELECT)
      .legalFor({{s32, s64}, {s64, s64}, {s80, s64}, {p0, s64}})
      .scalarize(1)
      .scalarize(0)
      .clampScalar(1, s64, s64)
      .minScalar(0, s32)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s32, s64);

  getActionDefinitionsBuilder(
      {G_IMPLICIT_DEF, G_FREEZE, G_CONSTANT_FOLD_BARRIER})
      .legalFor({s32, s64, s80, p0})
      .scalarize(0)
      .minScalar(0, s32)
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s32, s64);

  for (unsigned Op : {G_MERGE_VALUES, G_UNMERGE_VALUES}) {
    unsigned BigTy = Op == G_MERGE_VALUES ? 0 : 1;
    unsigned LitTy = Op == G_MERGE_VALUES ? 1 : 0;
    getActionDefinitionsBuilder(Op)
        // Vectors are only ever unmerged into the elements they were built
        // from.
        .legalIf([=](const LegalityQuery &Query) {
          return Query.Types[BigTy].isVector();
        })
        // An s80 value is always extended-precision FP; its parts are
        // accessed through memory.
        .customIf([=](const LegalityQuery &Query) {
          return Query.Types[BigTy] == s80;
        })
        // Non-byte-sized parts are lowered to shifts. Widening them would
        // produce an unmerge into wider parts plus truncates, which fold
        // back into the original. Nor can a non-power-of-two part size,
        // e.g. 3 bytes, be widened to a matching whole.
        .lowerIf([=](const LegalityQuery &Query) {
          unsigned LitBits = Query.Types[LitTy].getSizeInBits();
          unsigned BigBits = Query.Types[BigTy].getSizeInBits();
          return BigBits <= 64 && (LitBits < 8 || !isPowerOf2_32(LitBits) ||
                                   !isPowerOf2_32(BigBits));
        })
        .widenScalarToNextPow2(LitTy, 8)
        .widenScalarToNextPow2(BigTy, 32)
        .clampScalar(LitTy, s8, s64)
        .clampScalar(BigTy, s16, s128)
        // Values wider than a register stay as their halves and are only
        // combined where an unmerge reads the halves again. Lowering them to
        // shifts and ors would just produce another wide merge.
        .lowerIf([=](const LegalityQuery &Query) {
          return Query.Types[BigTy].getSizeInBits() <= 64;
        });
  }
  getActionDefinitionsBuilder({G_EXTRACT, G_INSERT}).lower();

  // There are no vector registers. Every vector operation is scalarized, so
  // each G_BUILD_VECTOR meets the unmerge of its elements and both fold away.
  getActionDefinitionsBuilder({G_BUILD_VECTOR, G_CONCAT_VECTORS})
      .legalIf([=](const LegalityQuery &Query) {
        return Query.Types[0].isVector();
      });
  getActionDefinitionsBuilder(G_BUILD_VECTOR_TRUNC).lower();
  getActionDefinitionsBuilder(
      {G_EXTRACT_VECTOR_ELT, G_INSERT_VECTOR_ELT, G_SHUFFLE_VECTOR, G_BITCAST})
      .lower();

  // Arithmetic is fma at the result's precision, and sign operations are
  // fmerge. Extended-precision division needs more than frcpa and a few
  // Newton-Raphson steps, so it is a libcall.
  getActionDefinitionsBuilder({G_FADD, G_FSUB, G_FMUL, G_FMA, G_FNEG, G_FABS})
      .legalFor({s32, s64, s80})
      .libcallFor({s128})
      .scalarize(0);
  getActionDefinitionsBuilder(G_FDIV)
      .legalFor({s32, s64})
      .libcallFor({s80, s128})
      .scalarize(0);
  getActionDefinitionsBuilder(G_FCOPYSIGN)
      .legalIf([=](const LegalityQuery &Query) {
        auto IsFP = [=](LLT Ty) { return Ty == s32 || Ty == s64 || Ty == s80; };
        return IsFP(Query.Types[0]) && IsFP(Query.Types[1]);
      })
      .lower();

  // f0 and f1 are the only constant registers (0.0 and 1.0). Other constants
  // are built in a GR and moved with setf, which cannot hold the bits of an
  // extended-precision value.
  getActionDefinitionsBuilder(G_FCONSTANT)
      .legalFor({s32, s64})
      .lowerFor({s80, s128});

  getActionDefinitionsBuilder(G_FCMP)
      .legalFor({{s64, s32}, {s64, s64}, {s64, s80}})
      .scalarize(1)
      .scalarize(0)
      .clampScalar(0, s64, s64)
      .libcallFor({{s64, s128}});

  // Truncation rounds; extension is exact.
  getActionDefinitionsBuilder(G_FPEXT)
      .legalFor({{s64, s32}, {s80, s32}, {s80, s64}})
      .scalarize(0)
      .libcallFor({{s128, s32}, {s128, s64}, {s128, s80}});
  getActionDefinitionsBuilder(G_FPTRUNC)
      .legalFor({{s32, s64}, {s32, s80}, {s64, s80}})
      .scalarize(0)
      .libcallFor({{s32, s128}, {s64, s128}, {s80, s128}});

  // Integer/FP conversions go through the significand of an FP register,
  // signed or unsigned.
  getActionDefinitionsBuilder({G_SITOFP, G_UITOFP})
      .legalFor({{s32, s64}, {s64, s64}, {s80, s64}})
      .scalarize(0)
      .libcallFor(
          {{s32, s128}, {s64, s128}, {s80, s128}, {s128, s64}, {s128, s128}})
      .widenScalarToNextPow2(1)
      .clampScalar(1, s64, s128);
  getActionDefinitionsBuilder({G_FPTOSI, G_FPTOUI})
      .legalFor({{s64, s32}, {s64, s64}, {s64, s80}})
      .scalarize(0)
      .libcallFor(
          {{s128, s32}, {s128, s64}, {s128, s80}, {s64, s128}, {s128, s128}})
      .widenScalarToNextPow2(0)
      .clampScalar(0, s64, s128);
  getActionDefinitionsBuilder({G_FPTOSI_SAT, G_FPTOUI_SAT})
      .scalarize(0)
      .lower();

  getActionDefinitionsBuilder({G_FSQRT,
                               G_FREM,
                               G_FPOW,
                               G_FEXP,
                               G_FEXP2,
                               G_FEXP10,
                               G_FLOG,
                               G_FLOG2,
                               G_FLOG10,
                               G_FSIN,
                               G_FCOS,
                               G_FTAN,
                               G_FASIN,
                               G_FACOS,
                               G_FATAN,
                               G_FATAN2,
                               G_FSINH,
                               G_FCOSH,
                               G_FTANH,
                               G_FCEIL,
                               G_FFLOOR,
                               G_FRINT,
                               G_FNEARBYINT,
                               G_INTRINSIC_TRUNC,
                               G_INTRINSIC_ROUND,
                               G_INTRINSIC_ROUNDEVEN,
                               G_FMINNUM,
                               G_FMAXNUM,
                               G_FMINIMUM,
                               G_FMAXIMUM})
      .libcallFor({s32, s64, s80, s128});
  getActionDefinitionsBuilder({G_FPOWI, G_FLDEXP})
      .libcallFor({{s32, s32}, {s64, s32}, {s80, s32}, {s128, s32}});
  getActionDefinitionsBuilder(
      {G_LROUND, G_LLROUND, G_INTRINSIC_LRINT, G_INTRINSIC_LLRINT})
      .libcallFor({{s64, s32}, {s64, s64}, {s64, s80}, {s64, s128}})
      .clampScalar(0, s64, s64);
  // float and double are classified from their bit patterns by the generic
  // lowering. Extended precision has a different encoding and uses fclass.
  getActionDefinitionsBuilder(G_IS_FPCLASS)
      .customIf(
          [=](const LegalityQuery &Query) { return Query.Types[1] == s80; })
      .lower();

  getActionDefinitionsBuilder({G_MEMCPY, G_MEMMOVE, G_MEMSET}).libcall();
  getActionDefinitionsBuilder({G_MEMCPY_INLINE, G_MEMSET_INLINE}).lower();

  // popcnt counts set bits; cttz lowers to popcnt of the mask of zeros below
  // the lowest set bit.
  getActionDefinitionsBuilder(G_CTPOP)
      .legalFor({{s64, s64}})
      .widenScalarToNextMultipleOf(1, 64)
      .clampScalar(0, s64, s64)
      .clampScalar(1, s64, s64);
  getActionDefinitionsBuilder({G_CTLZ, G_CTTZ})
      .widenScalarToNextMultipleOf(1, 64)
      .clampScalar(0, s64, s64)
      .clampScalar(1, s64, s64)
      .lower();
  getActionDefinitionsBuilder({G_CTLZ_ZERO_POISON, G_CTTZ_ZERO_POISON}).lower();

  // mux1 @rev reverses the bytes of a register.
  getActionDefinitionsBuilder(G_BSWAP)
      .legalFor({s64})
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);
  // The lowering byte-swaps and then reverses the bits within each byte,
  // which only works for whole bytes.
  getActionDefinitionsBuilder(G_BITREVERSE)
      .widenScalarToNextMultipleOf(0, 8)
      .lower();
  getActionDefinitionsBuilder(G_ABS).minScalar(0, s64).lower();
  getActionDefinitionsBuilder({G_SMIN, G_SMAX, G_UMIN, G_UMAX})
      .minScalar(0, s64)
      .lower();
  getActionDefinitionsBuilder(
      {G_UADDO, G_USUBO, G_UADDE, G_USUBE, G_SADDO, G_SSUBO, G_SADDE, G_SSUBE})
      .lower();
  getActionDefinitionsBuilder(
      {G_SADDSAT, G_SSUBSAT, G_UADDSAT, G_USUBSAT, G_SSHLSAT, G_USHLSAT})
      .lower();

  getActionDefinitionsBuilder({G_STACKSAVE, G_STACKRESTORE}).lower();

  // The outgoing argument area is at the stack pointer, so dynamic
  // allocations start above it.
  getActionDefinitionsBuilder(G_DYN_STACKALLOC).customFor({{p0, s64}});

  // va_list is a pointer to the next variadic argument.
  getActionDefinitionsBuilder(G_VASTART).customFor({p0});
  // va_arg loads from the va_list pointer and advances it past the slots
  // used. Arguments narrower than a slot occupy a full 8-byte slot.
  getActionDefinitionsBuilder(G_VAARG)
      .customIf([=](const LegalityQuery &Q) {
        return Q.Types[0].isScalar() && Q.Types[0].getSizeInBits() < 64;
      })
      .lower();
  getActionDefinitionsBuilder(G_FENCE).alwaysLegal();
  getActionDefinitionsBuilder({G_TRAP, G_DEBUGTRAP}).alwaysLegal();
  getActionDefinitionsBuilder(G_PREFETCH).legalIf(typeIs(0, p0));
  getActionDefinitionsBuilder(G_INVOKE_REGION_START).alwaysLegal();
  getActionDefinitionsBuilder({G_INTRINSIC, G_INTRINSIC_W_SIDE_EFFECTS,
                               G_INTRINSIC_CONVERGENT,
                               G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS})
      .alwaysLegal();

  // cmpxchg compares against ar.ccv and stores 1, 2, 4 or 8 bytes.
  getActionDefinitionsBuilder(G_ATOMIC_CMPXCHG)
      .legalForTypesWithMemDesc({{s64, p0, s8, 8},
                                 {s64, p0, s16, 16},
                                 {s64, p0, s32, 32},
                                 {s64, p0, s64, 64}})
      .clampScalar(0, s64, s64);
  getActionDefinitionsBuilder(G_ATOMIC_CMPXCHG_WITH_SUCCESS).lower();

  // xchg handles the same sizes, and fetchadd adds one of a few immediates to
  // 4 or 8 bytes. Other atomicrmw operations were already expanded to
  // cmpxchg loops by AtomicExpand.
  getActionDefinitionsBuilder(G_ATOMICRMW_XCHG)
      .legalForTypesWithMemDesc({{s64, p0, s8, 8},
                                 {s64, p0, s16, 16},
                                 {s64, p0, s32, 32},
                                 {s64, p0, s64, 64}})
      .clampScalar(0, s64, s64);
  getActionDefinitionsBuilder({G_ATOMICRMW_ADD, G_ATOMICRMW_SUB})
      .legalForTypesWithMemDesc({{s64, p0, s32, 32}, {s64, p0, s64, 64}})
      .clampScalar(0, s64, s64);

  verify(*ST.getInstrInfo());
}

bool IA64LegalizerInfo::legalizeCustom(
    LegalizerHelper &Helper, MachineInstr &MI,
    LostDebugLocObserver &LocObserver) const {
  MachineIRBuilder &MIRBuilder = Helper.MIRBuilder;
  MachineFunction &MF = MIRBuilder.getMF();
  switch (MI.getOpcode()) {
  default:
    return false;
  case G_GLOBAL_VALUE: {
    // General-dynamic TLS: call __tls_get_addr with the module ID and DTP
    // offset, both loaded from the linkage table. Local-dynamic: call it with
    // a zero offset and add the DTP offset to the result. All other addresses
    // are left to the instruction selector.
    const MachineOperand &Symbol = MI.getOperand(1);
    const GlobalValue *GV = Symbol.getGlobal();
    const TargetMachine &TM = MF.getTarget();
    if (!GV->isThreadLocal())
      return true;
    if (TM.useEmulatedTLS()) {
      // Emulated TLS: call __emutls_get_address on the __emutls_v. control
      // variable.
      const auto *Var = cast<GlobalValue>(GV->stripPointerCastsAndAliases());
      const GlobalVariable *Control = Var->getParent()->getNamedGlobal(
          ("__emutls_v." + Var->getName()).str());
      if (!Control)
        return false;
      MachineRegisterInfo &MRI = MF.getRegInfo();
      LLT P0 = LLT::pointer(0, 64);
      Type *Ptr = PointerType::getUnqual(MF.getFunction().getContext());
      Register Dst = MI.getOperand(0).getReg();
      int64_t Offset = Symbol.getOffset();
      Register Base = Offset ? MRI.createGenericVirtualRegister(P0) : Dst;
      Register Arg = MIRBuilder.buildGlobalValue(P0, Control).getReg(0);
      if (Helper.createLibcall("__emutls_get_address", {Base, Ptr, 0},
                               {{Arg, Ptr, 0}}, CallingConv::C,
                               LocObserver) != LegalizerHelper::Legalized)
        return false;
      if (Offset)
        MIRBuilder.buildPtrAdd(
            Dst, Base, MIRBuilder.buildConstant(LLT::scalar(64), Offset));
      MI.eraseFromParent();
      return true;
    }
    TLSModel::Model Model = TM.getTLSModel(GV);
    if (Model != TLSModel::GeneralDynamic && Model != TLSModel::LocalDynamic)
      return true;
    if (Symbol.getOffset() != 0)
      return false;

    MachineRegisterInfo &MRI = MF.getRegInfo();
    LLT S64 = LLT::scalar(64);
    LLT P0 = LLT::pointer(0, 64);
    auto Flagged = [&](unsigned Flags) {
      MachineOperand Op = Symbol;
      Op.setTargetFlags(Flags);
      return Op;
    };
    auto LinkageTableEntry = [&](unsigned Flags) {
      Register Entry = MRI.createVirtualRegister(&IA64::GRRegClass);
      MIRBuilder.buildInstr(IA64::ADDL)
          .addDef(Entry)
          .add(Flagged(Flags))
          .addReg(IA64::R1)
          .addReg(IA64::P0);
      Register Value = MRI.createVirtualRegister(&IA64::GRRegClass);
      MRI.setType(Value, S64);
      MIRBuilder.buildInstr(IA64::LD8)
          .addDef(Value)
          .addReg(Entry)
          .addImm(0)
          .addReg(IA64::P0)
          .addMemOperand(MF.getMachineMemOperand(
              MachinePointerInfo::getGOT(MF),
              MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
                  MachineMemOperand::MOInvariant,
              S64, Align(8)));
      return Value;
    };

    unsigned Size = TM.Options.TLSSize;
    Size = Size == 0 ? 22 : Size <= 14 ? 14 : Size <= 22 ? 22 : 64;
    bool General = Model == TLSModel::GeneralDynamic;
    Register Dst = MI.getOperand(0).getReg();
    Register Module = LinkageTableEntry(IA64::S_LTOFF_DTPMOD);
    Register Offset = General ? LinkageTableEntry(IA64::S_LTOFF_DTPREL)
                              : MIRBuilder.buildConstant(S64, 0).getReg(0);
    Register Base = General ? Dst : MRI.createGenericVirtualRegister(P0);
    LLVMContext &Ctx = MF.getFunction().getContext();
    Type *Int64 = Type::getInt64Ty(Ctx);
    if (Helper.createLibcall(
            "__tls_get_addr", {Base, PointerType::getUnqual(Ctx), 0},
            {{Module, Int64, 0}, {Offset, Int64, 1}}, CallingConv::C,
            LocObserver) != LegalizerHelper::Legalized)
      return false;
    if (!General) {
      if (Size == 64) {
        MIRBuilder.buildPtrAdd(Dst, Base,
                               LinkageTableEntry(IA64::S_LTOFF_DTPREL));
      } else if (Size == 14) {
        Register From = MRI.createVirtualRegister(&IA64::GRRegClass);
        MIRBuilder.buildCopy(From, Base);
        MRI.setRegClass(Dst, &IA64::GRRegClass);
        MIRBuilder.buildInstr(IA64::ADDS)
            .addDef(Dst)
            .add(Flagged(IA64::S_DTPREL))
            .addReg(From)
            .addReg(IA64::P0);
      } else {
        // addl's 22-bit form only accepts r0-r3 as the base.
        Register From = MRI.createVirtualRegister(&IA64::GR0_3RegClass);
        MIRBuilder.buildCopy(From, Base);
        MRI.setRegClass(Dst, &IA64::GRRegClass);
        MIRBuilder.buildInstr(IA64::ADDL)
            .addDef(Dst)
            .add(Flagged(IA64::S_DTPREL))
            .addReg(From)
            .addReg(IA64::P0);
      }
    }
    MI.eraseFromParent();
    return true;
  }
  case G_IS_FPCLASS: {
    // fclass classifies an extended-precision value without raising an
    // exception on a signaling NaN, unlike a compare. Emit one fclass per
    // test needed, testing either the requested classes or their complement,
    // whichever needs fewer.
    LLT S64 = LLT::scalar(64);
    MachineRegisterInfo &MRI = MF.getRegInfo();
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    unsigned Mask = MI.getOperand(2).getImm() & fcAllFlags;
    if (Mask == 0 || Mask == fcAllFlags) {
      MIRBuilder.buildConstant(Dst, Mask != 0);
      MI.eraseFromParent();
      return true;
    }
    SmallVector<unsigned, 2> Tests, Others;
    IA64TargetLowering::getClassTests(Mask, Tests);
    IA64TargetLowering::getClassTests(~Mask & fcAllFlags, Others);
    bool None = Others.size() < Tests.size();
    Register Value = MRI.createVirtualRegister(&IA64::FRRegClass);
    MRI.setType(Value, MRI.getType(Src));
    MIRBuilder.buildCopy(Value, Src);
    Register Res;
    for (unsigned Test : None ? Others : Tests) {
      Register Is = MRI.createVirtualRegister(&IA64::GRRegClass);
      MRI.setType(Is, S64);
      MIRBuilder.buildInstr(IA64::SETFCLASS)
          .addDef(Is)
          .addUse(Value)
          .addImm(Test)
          .addImm(None);
      if (!Res)
        Res = Is;
      else if (None)
        Res = MIRBuilder.buildAnd(S64, Res, Is).getReg(0);
      else
        Res = MIRBuilder.buildOr(S64, Res, Is).getReg(0);
    }
    MIRBuilder.buildTrunc(Dst, Res);
    MI.eraseFromParent();
    return true;
  }
  case G_SEXT:
  case G_ZEXT:
  case G_ANYEXT:
  case G_TRUNC: {
    // stfe stores an extended value as 10 bytes: a doubleword plus 2 bytes,
    // which are sign- or zero-extended when converting to a wider integer.
    // ldfe reads the low 10 bytes of an integer stored there.
    MachineRegisterInfo &MRI = MF.getRegInfo();
    LLT P0 = LLT::pointer(0, 64);
    LLT S16 = LLT::scalar(16);
    LLT S64 = LLT::scalar(64);
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
    auto Slot = MIRBuilder.buildFrameIndex(P0, FI);
    auto SlotHigh =
        MIRBuilder.buildPtrAdd(P0, Slot, MIRBuilder.buildConstant(S64, 8));
    MachinePointerInfo SlotInfo = MachinePointerInfo::getFixedStack(MF, FI);
    MachineMemOperand *High = MF.getMachineMemOperand(
        SlotInfo.getWithOffset(8),
        MI.getOpcode() == G_TRUNC ? MachineMemOperand::MOStore
                                  : MachineMemOperand::MOLoad,
        S16, Align(8));
    if (MI.getOpcode() == G_TRUNC && MRI.getType(Src) == LLT::scalar(80)) {
      // stfe stores the low bits in the first doubleword.
      MIRBuilder.buildStore(Src, Slot, SlotInfo, Align(16));
      auto Low = MIRBuilder.buildLoad(S64, Slot, SlotInfo, Align(16));
      if (MRI.getType(Dst) == S64)
        MIRBuilder.buildCopy(Dst, Low);
      else
        MIRBuilder.buildTrunc(Dst, Low);
    } else if (MI.getOpcode() == G_TRUNC) {
      auto Parts = MIRBuilder.buildUnmerge(S64, Src);
      MIRBuilder.buildStore(Parts.getReg(0), Slot, SlotInfo, Align(16));
      MIRBuilder.buildStore(Parts.getReg(1), SlotHigh, *High);
      MIRBuilder.buildLoad(Dst, Slot, SlotInfo, Align(16));
    } else {
      bool Signed = MI.getOpcode() == G_SEXT;
      MIRBuilder.buildStore(Src, Slot, SlotInfo, Align(16));
      SmallVector<Register, 4> Parts;
      Parts.push_back(
          MIRBuilder.buildLoad(S64, Slot, SlotInfo, Align(16)).getReg(0));
      Parts.push_back(MIRBuilder
                          .buildLoadInstr(Signed ? G_SEXTLOAD : G_ZEXTLOAD, S64,
                                          SlotHigh, *High)
                          .getReg(0));
      Register Rest;
      while (Parts.size() != MRI.getType(Dst).getSizeInBits() / 64) {
        if (!Rest)
          Rest = Signed ? MIRBuilder
                              .buildAShr(S64, Parts[1],
                                         MIRBuilder.buildConstant(S64, 63))
                              .getReg(0)
                        : MIRBuilder.buildConstant(S64, 0).getReg(0);
        Parts.push_back(Rest);
      }
      MIRBuilder.buildMergeLikeInstr(Dst, Parts);
    }
    MI.eraseFromParent();
    return true;
  }
  case G_MERGE_VALUES:
  case G_UNMERGE_VALUES: {
    // Unmerge: stfe to a stack slot and load the parts. Merge: store the
    // parts and ldfe the whole.
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);
    bool IsMerge = MI.getOpcode() == G_MERGE_VALUES;
    unsigned NumParts = MI.getNumOperands() - 1;
    Register Whole = MI.getOperand(IsMerge ? 0 : NumParts).getReg();
    LLT PartTy =
        MF.getRegInfo().getType(MI.getOperand(IsMerge ? 1 : 0).getReg());
    unsigned PartBytes = PartTy.getSizeInBytes();
    if (PartTy.getSizeInBits() != PartBytes * 8 || !isPowerOf2_32(PartBytes))
      return false;
    int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
    auto Slot = MIRBuilder.buildFrameIndex(P0, FI);
    MachinePointerInfo SlotInfo = MachinePointerInfo::getFixedStack(MF, FI);
    if (!IsMerge)
      MIRBuilder.buildStore(Whole, Slot, SlotInfo, Align(16));
    for (unsigned Part = 0; Part != NumParts; ++Part) {
      unsigned Offset = Part * PartBytes;
      Register PartReg = MI.getOperand(IsMerge ? Part + 1 : Part).getReg();
      auto Address = MIRBuilder.buildPtrAdd(
          P0, Slot, MIRBuilder.buildConstant(S64, Offset));
      MachinePointerInfo Info = SlotInfo.getWithOffset(Offset);
      Align Alignment = commonAlignment(Align(16), Offset);
      if (IsMerge)
        MIRBuilder.buildStore(PartReg, Address, Info, Alignment);
      else
        MIRBuilder.buildLoad(PartReg, Address, Info, Alignment);
    }
    if (IsMerge)
      MIRBuilder.buildLoad(Whole, Slot, SlotInfo, Align(16));
    MI.eraseFromParent();
    return true;
  }
  case G_LOAD:
  case G_STORE: {
    // Copy the 10 bytes as an 8-byte and a 2-byte access between the
    // original address and a 16-byte-aligned stack slot, where ldfe/stfe get
    // the alignment they require.
    auto &Access = cast<GLoadStore>(MI);
    LLT P0 = LLT::pointer(0, 64);
    LLT S16 = LLT::scalar(16);
    LLT S64 = LLT::scalar(64);
    LLT S80 = LLT::scalar(80);
    Register Value = MI.getOperand(0).getReg();
    Register Pointer = Access.getPointerReg();
    const MachineMemOperand &MMO = Access.getMMO();
    if (MMO.getMemoryType() != S80 || MMO.isAtomic())
      return false;
    int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
    auto Slot = MIRBuilder.buildFrameIndex(P0, FI);
    MachinePointerInfo SlotInfo = MachinePointerInfo::getFixedStack(MF, FI);
    auto Eight = MIRBuilder.buildConstant(S64, 8);
    auto SlotHigh = MIRBuilder.buildPtrAdd(P0, Slot, Eight);
    auto PointerHigh = MIRBuilder.buildPtrAdd(P0, Pointer, Eight);
    MachineMemOperand *Low = MF.getMachineMemOperand(&MMO, 0, S64);
    MachineMemOperand *High = MF.getMachineMemOperand(&MMO, 8, S16);
    bool IsLoad = MI.getOpcode() == G_LOAD;
    if (!IsLoad)
      MIRBuilder.buildStore(Value, Slot, SlotInfo, Align(16));
    Register From = IsLoad ? Pointer : Slot.getReg(0);
    Register FromHigh = IsLoad ? PointerHigh.getReg(0) : SlotHigh.getReg(0);
    Register To = IsLoad ? Slot.getReg(0) : Pointer;
    Register ToHigh = IsLoad ? SlotHigh.getReg(0) : PointerHigh.getReg(0);
    auto LowValue = IsLoad
                        ? MIRBuilder.buildLoad(S64, From, *Low)
                        : MIRBuilder.buildLoad(S64, From, SlotInfo, Align(16));
    auto HighValue =
        IsLoad ? MIRBuilder.buildLoad(S16, FromHigh, *High)
               : MIRBuilder.buildLoad(S16, FromHigh, SlotInfo.getWithOffset(8),
                                      Align(8));
    if (IsLoad) {
      MIRBuilder.buildStore(LowValue, To, SlotInfo, Align(16));
      MIRBuilder.buildStore(HighValue, ToHigh, SlotInfo.getWithOffset(8),
                            Align(8));
      MIRBuilder.buildLoad(Value, Slot, SlotInfo, Align(16));
    } else {
      MIRBuilder.buildStore(LowValue, To, *Low);
      MIRBuilder.buildStore(HighValue, ToHigh, *High);
    }
    MI.eraseFromParent();
    return true;
  }
  case G_BRJT: {
    // Load the entry, add it to the table address and branch there.
    const MachineJumpTableInfo *MJTI = MF.getJumpTableInfo();
    if (MJTI->getEntryKind() != MachineJumpTableInfo::EK_LabelDifference64)
      return false;
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);
    Register Table = MI.getOperand(0).getReg();
    auto Offset = MIRBuilder.buildShl(S64, MI.getOperand(2).getReg(),
                                      MIRBuilder.buildConstant(S64, 3));
    auto Entry = MIRBuilder.buildLoad(
        S64, MIRBuilder.buildPtrAdd(P0, Table, Offset),
        *MF.getMachineMemOperand(MachinePointerInfo::getJumpTable(MF),
                                 MachineMemOperand::MOLoad, S64, Align(8)));
    MIRBuilder.buildBrIndirect(
        MIRBuilder.buildPtrAdd(P0, Table, Entry).getReg(0));
    MI.eraseFromParent();
    return true;
  }
  case G_DYN_STACKALLOC: {
    // Decrement sp by the size, which is already a multiple of 16. Larger
    // alignments over-allocate and round the address up.
    MachineRegisterInfo &MRI = *MIRBuilder.getMRI();
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);
    Register Dst = MI.getOperand(0).getReg();
    Register Size = MI.getOperand(1).getReg();
    uint64_t Alignment = MI.getOperand(2).getImm();
    if (Alignment > 16)
      Size = MIRBuilder
                 .buildAdd(S64, Size,
                           MIRBuilder.buildConstant(S64, Alignment - 16))
                 .getReg(0);
    auto SP = MIRBuilder.buildCopy(P0, Register(IA64::R12));
    auto Lower =
        MIRBuilder.buildSub(S64, MIRBuilder.buildPtrToInt(S64, SP), Size);
    MIRBuilder.buildCopy(Register(IA64::R12),
                         MIRBuilder.buildIntToPtr(P0, Lower));
    Register Addr = Dst;
    if (Alignment > 16)
      Addr = MRI.createGenericVirtualRegister(P0);
    MRI.setRegClass(Addr, &IA64::GRRegClass);
    MIRBuilder.buildInstr(IA64::ADJDYNALLOC, {Addr}, {});
    if (Alignment > 16) {
      auto Mask = MIRBuilder.buildConstant(S64, Alignment - 1);
      auto Up =
          MIRBuilder.buildAdd(S64, MIRBuilder.buildPtrToInt(S64, Addr), Mask);
      MIRBuilder.buildIntToPtr(
          Dst,
          MIRBuilder.buildAnd(
              S64, Up, MIRBuilder.buildConstant(S64, -int64_t(Alignment))));
    }
    MI.eraseFromParent();
    return true;
  }
  case G_PTRMASK: {
    // Mask the pointer as an integer.
    LLT S64 = LLT::scalar(64);
    MIRBuilder.buildIntToPtr(
        MI.getOperand(0).getReg(),
        MIRBuilder.buildAnd(
            S64, MIRBuilder.buildPtrToInt(S64, MI.getOperand(1).getReg()),
            MI.getOperand(2).getReg()));
    MI.eraseFromParent();
    return true;
  }
  case G_VAARG: {
    Register Slot = MF.getRegInfo().createGenericVirtualRegister(
        LLT::scalar(64));
    MIRBuilder.buildInstr(G_VAARG, {Slot}, {MI.getOperand(1).getReg()})
        .addImm(8)
        .cloneMemRefs(MI);
    MIRBuilder.buildTrunc(MI.getOperand(0).getReg(), Slot);
    MI.eraseFromParent();
    return true;
  }
  case G_VASTART: {
    // The va_list starts at the first variadic argument, just past the named
    // ones.
    int FI = MF.getInfo<IA64MachineFunctionInfo>()->getVarArgsFrameIndex();
    MachineMemOperand *MMO = *MI.memoperands_begin();
    LLT P0 = LLT::pointer(0, 64);
    MIRBuilder.buildStore(MIRBuilder.buildFrameIndex(P0, FI),
                          MI.getOperand(0).getReg(),
                          *MF.getMachineMemOperand(MMO->getPointerInfo(),
                                                   MachineMemOperand::MOStore,
                                                   P0, MMO->getAlign()));
    MI.eraseFromParent();
    return true;
  }
  }
}

bool IA64LegalizerInfo::legalizeIntrinsic(LegalizerHelper &Helper,
                                          MachineInstr &MI) const {
  MachineIRBuilder &MIRBuilder = Helper.MIRBuilder;
  switch (cast<GIntrinsic>(MI).getIntrinsicID()) {
  default:
    return true;
  case Intrinsic::returnaddress: {
    // The return address is the incoming b0. Nonzero depths return 0.
    MachineFunction &MF = MIRBuilder.getMF();
    MF.getFrameInfo().setReturnAddressIsTaken(true);
    Register Dst = MI.getOperand(0).getReg();
    LLT P0 = LLT::pointer(0, 64);
    if (MI.getOperand(2).getImm() != 0) {
      MIRBuilder.buildConstant(Dst, 0);
    } else {
      Register B0 = getFunctionLiveInPhysReg(
          MF, *MF.getSubtarget().getInstrInfo(), IA64::B0, IA64::GRRegClass,
          MI.getDebugLoc(), P0);
      MIRBuilder.buildCopy(Dst, B0);
    }
    MI.eraseFromParent();
    return true;
  }
  case Intrinsic::frameaddress: {
    // The frame address is the stack pointer on entry.
    MachineFunction &MF = MIRBuilder.getMF();
    Register Dst = MI.getOperand(0).getReg();
    if (MI.getOperand(2).getImm() != 0) {
      MIRBuilder.buildConstant(Dst, 0);
    } else {
      MF.getFrameInfo().setFrameAddressIsTaken(true);
      MF.getRegInfo().setRegClass(Dst, &IA64::GRRegClass);
      MIRBuilder.buildInstr(IA64::FRAMEADDR, {Dst}, {});
    }
    MI.eraseFromParent();
    return true;
  }
  case Intrinsic::init_trampoline: {
    // A trampoline is a function descriptor followed by the data its GP
    // points to: the entry point of __ia64_trampoline and the address of
    // word 2 as the GP, then the target's function descriptor and the static
    // chain, which __ia64_trampoline loads through its GP.
    MachineFunction &MF = MIRBuilder.getMF();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    LLT S64 = LLT::scalar(64);
    LLT P0 = LLT::pointer(0, 64);
    Register Tramp = MI.getOperand(1).getReg();
    Register At = MRI.createVirtualRegister(&IA64::GRRegClass);
    MIRBuilder.buildInstr(IA64::ADDL)
        .addDef(At)
        .addExternalSymbol("__ia64_trampoline", IA64::S_LTOFF)
        .addReg(IA64::R1)
        .addReg(IA64::P0);
    Register Entry = MRI.createVirtualRegister(&IA64::GRRegClass);
    MRI.setType(Entry, S64);
    MIRBuilder.buildInstr(IA64::LD8)
        .addDef(Entry)
        .addReg(At)
        .addImm(0)
        .addReg(IA64::P0)
        .addMemOperand(MF.getMachineMemOperand(
            MachinePointerInfo::getGOT(MF),
            MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
                MachineMemOperand::MOInvariant,
            S64, Align(8)));
    auto Word = [&](unsigned I) {
      return MIRBuilder.buildPtrAdd(P0, Tramp,
                                    MIRBuilder.buildConstant(S64, 8 * I));
    };
    Register Words[] = {Entry, Word(2).getReg(0), MI.getOperand(2).getReg(),
                        MI.getOperand(3).getReg()};
    for (unsigned I = 0; I != 4; ++I)
      MIRBuilder.buildStore(Words[I], I ? Word(I).getReg(0) : Tramp,
                            MachinePointerInfo(), Align(8));
    MI.eraseFromParent();
    return true;
  }
  case Intrinsic::adjust_trampoline:
    // The trampoline is called through its own address.
    MIRBuilder.buildCopy(MI.getOperand(0).getReg(), MI.getOperand(2).getReg());
    MI.eraseFromParent();
    return true;
  case Intrinsic::thread_pointer:
    MIRBuilder.buildCopy(MI.getOperand(0).getReg(), Register(IA64::R13));
    MI.eraseFromParent();
    return true;
  case Intrinsic::vacopy: {
    // va_list is a single pointer.
    LLT P0 = LLT::pointer(0, 64);
    auto Next = MIRBuilder.buildLoad(P0, MI.getOperand(2).getReg(),
                                     MachinePointerInfo(), Align(8));
    MIRBuilder.buildStore(Next, MI.getOperand(1).getReg(), MachinePointerInfo(),
                          Align(8));
    MI.eraseFromParent();
    return true;
  }
  }
}

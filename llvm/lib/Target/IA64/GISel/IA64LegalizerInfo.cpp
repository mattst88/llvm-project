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
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Intrinsics.h"

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
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);
  getActionDefinitionsBuilder({G_FSHL, G_FSHR, G_ROTL, G_ROTR}).lower();

  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({s64, p0})
      .widenScalarToNextMultipleOf(0, 64)
      .clampScalar(0, s64, s64);

  getActionDefinitionsBuilder(
      {G_FRAME_INDEX, G_GLOBAL_VALUE, G_BLOCK_ADDR, G_CONSTANT_POOL})
      .legalFor({p0});

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

  getActionDefinitionsBuilder({G_SEXT, G_ZEXT, G_ANYEXT})
      .legalIf([=](const LegalityQuery &Query) {
        const LLT Dst = Query.Types[0];
        const LLT Src = Query.Types[1];
        return Dst.isScalar() && Src.isScalar() && (Dst == s32 || Dst == s64) &&
               Src.getSizeInBits() < Dst.getSizeInBits();
      })
      .scalarize(0)
      .maxScalar(0, s64);
  getActionDefinitionsBuilder(G_TRUNC)
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
      .libcallFor({{s64, s32}, {s64, s64}, {s64, s80}, {s64, s128}});
  getActionDefinitionsBuilder(G_IS_FPCLASS).lower();

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
  case G_BRJT: {
    // Load the entry, add it to the table address and branch there.
    const MachineJumpTableInfo *MJTI = MF.getJumpTableInfo();
    if (MJTI->getEntryKind() != MachineJumpTableInfo::EK_LabelDifference32)
      return false;
    LLT P0 = LLT::pointer(0, 64);
    LLT S32 = LLT::scalar(32);
    LLT S64 = LLT::scalar(64);
    Register Table = MI.getOperand(0).getReg();
    auto Offset = MIRBuilder.buildShl(S64, MI.getOperand(2).getReg(),
                                      MIRBuilder.buildConstant(S64, 2));
    auto Entry = MIRBuilder.buildLoad(
        S32, MIRBuilder.buildPtrAdd(P0, Table, Offset),
        *MF.getMachineMemOperand(MachinePointerInfo::getJumpTable(MF),
                                 MachineMemOperand::MOLoad, S32, Align(4)));
    MIRBuilder.buildBrIndirect(
        MIRBuilder.buildPtrAdd(P0, Table, MIRBuilder.buildSExt(S64, Entry))
            .getReg(0));
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

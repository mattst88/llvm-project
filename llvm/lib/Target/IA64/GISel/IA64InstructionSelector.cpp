//===-- IA64InstructionSelector.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GlobalISel instruction selection for legal, register-bank-assigned generic
// instructions.
//
// The patterns are written for i64, which is what a GR holds in
// SelectionDAG. Here a GR may also hold a pointer or a narrower value with
// the same bits, so patterns are matched as if every GR operand were s64.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64RegisterBankInfo.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "IA64TargetObjectFile.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IntrinsicsIA64.h"

#define DEBUG_TYPE "ia64-isel"

using namespace llvm;

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

namespace {

// The condition of a branch or select: a compare of two registers or of a
// register and an immediate.
struct Condition {
  enum KindTy { TwoRegisters, RegisterAndConstant, FloatingPoint };
  KindTy Kind;
  IA64CC::CondCode CC;
  Register LHS;
  Register RHS;
  int64_t Imm;
};

class IA64InstructionSelector : public InstructionSelector {
public:
  IA64InstructionSelector(const IA64TargetMachine &TM, const IA64Subtarget &STI,
                          const IA64RegisterBankInfo &RBI);

  void setupMF(MachineFunction &MF, GISelValueTracking *VT,
               CodeGenCoverage *CoverageInfo, ProfileSummaryInfo *PSI,
               BlockFrequencyInfo *BFI) override;
  bool select(MachineInstr &I) override;
  static const char *getName() { return DEBUG_TYPE; }

private:
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  bool selectInstruction(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectByPattern(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectCopy(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectPHI(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectLoadStore(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectAtomic(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectExtension(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectShift(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectICmp(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectFCmp(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectSelect(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectFConstant(MachineInstr &I, MachineRegisterInfo &MRI) const;
  bool selectGlobalValue(MachineInstr &I, MachineRegisterInfo &MRI) const;

  // Build an instruction before I. Its registers are constrained to its
  // operand classes after I has been selected.
  MachineInstrBuilder emit(MachineInstr &I, unsigned Opcode,
                           Register Def = Register()) const;
  MachineInstrBuilder emitAt(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator It, const DebugLoc &DL,
                             unsigned Opcode, Register Def = Register()) const;

  void materialize(MachineInstr &I, Register Dst, int64_t Imm) const;
  Register loadLinkageTableEntry(MachineInstr &I, Register Dst,
                                 const MachineOperand &Symbol,
                                 unsigned Flags) const;
  Condition getCondition(Register Reg, const MachineRegisterInfo &MRI) const;
  bool isFloatingPoint(Register Reg, const MachineRegisterInfo &MRI) const;
  bool constrainToBank(Register Reg, MachineRegisterInfo &MRI) const;

  void renderFrameIndex(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx) const;

  mutable SmallVector<MachineInstr *, 8> Emitted;

  const IA64TargetMachine &TM;
  const IA64InstrInfo &TII;
  const IA64RegisterInfo &TRI;
  const IA64RegisterBankInfo &RBI;

#define GET_GLOBALISEL_PREDICATES_DECL
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};

} // end anonymous namespace

#define GET_GLOBALISEL_IMPL
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

IA64InstructionSelector::IA64InstructionSelector(
    const IA64TargetMachine &TM, const IA64Subtarget &STI,
    const IA64RegisterBankInfo &RBI)
    : TM(TM), TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()), RBI(RBI),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "IA64GenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

void IA64InstructionSelector::renderFrameIndex(MachineInstrBuilder &MIB,
                                               const MachineInstr &MI,
                                               int OpIdx) const {
  MIB.add(MI.getOperand(1));
}

MachineInstrBuilder IA64InstructionSelector::emitAt(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator It, const DebugLoc &DL,
    unsigned Opcode, Register Def) const {
  MachineInstrBuilder MIB = Def ? BuildMI(MBB, It, DL, TII.get(Opcode), Def)
                                : BuildMI(MBB, It, DL, TII.get(Opcode));
  Emitted.push_back(MIB.getInstr());
  return MIB;
}

MachineInstrBuilder IA64InstructionSelector::emit(MachineInstr &I,
                                                  unsigned Opcode,
                                                  Register Def) const {
  return emitAt(*I.getParent(), I, I.getDebugLoc(), Opcode, Def);
}

bool IA64InstructionSelector::isFloatingPoint(
    Register Reg, const MachineRegisterInfo &MRI) const {
  if (Reg.isPhysical())
    return IA64::FRRegClass.contains(Reg);
  if (const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg))
    return IA64::FRRegClass.hasSubClassEq(RC);
  const RegisterBank *RB = MRI.getRegBankOrNull(Reg);
  return RB && RB->getID() == IA64::FPRRegBankID;
}

// Constrain a register with a bank but no class to the bank's class.
bool IA64InstructionSelector::constrainToBank(Register Reg,
                                              MachineRegisterInfo &MRI) const {
  if (Reg.isPhysical() || MRI.getRegClassOrNull(Reg))
    return true;
  if (!MRI.getRegBankOrNull(Reg))
    return false;
  const TargetRegisterClass &RC =
      isFloatingPoint(Reg, MRI) ? IA64::FRRegClass : IA64::GRRegClass;
  return RBI.constrainGenericRegister(Reg, RC, MRI) != nullptr;
}

// A copy between a GR and an FR copies the bits, which getf/setf move in the
// memory format of a single or double.
bool IA64InstructionSelector::selectCopy(MachineInstr &I,
                                         MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  Register Src = I.getOperand(1).getReg();
  auto IsGeneral = [&](Register Reg) {
    return Reg.isPhysical() ? IA64::GRRegClass.contains(Reg)
                            : !isFloatingPoint(Reg, MRI);
  };
  bool ToFP = isFloatingPoint(Dst, MRI) && IsGeneral(Src);
  bool FromFP = isFloatingPoint(Src, MRI) && IsGeneral(Dst);
  if (!ToFP && !FromFP)
    return constrainToBank(Dst, MRI) && constrainToBank(Src, MRI);

  // Take the width from whichever register still has a type, trying the FPR
  // one first, since nothing has had reason to change its type.
  Register FP = ToFP ? Dst : Src, General = ToFP ? Src : Dst;
  LLT Ty = FP.isVirtual() ? MRI.getType(FP) : LLT();
  if (!Ty.isValid() && General.isVirtual())
    Ty = MRI.getType(General);
  if (!Ty.isValid())
    return false;
  unsigned Opcode;
  switch (Ty.getSizeInBits()) {
  case 32:
    Opcode = ToFP ? IA64::SETF_S : IA64::GETF_S;
    break;
  case 64:
    Opcode = ToFP ? IA64::SETF_D : IA64::GETF_D;
    break;
  default:
    return false;
  }
  I.setDesc(TII.get(Opcode));
  I.addOperand(MachineOperand::CreateReg(IA64::P0, /*isDef=*/false));
  constrainSelectedInstRegOperands(I, TII, TRI, RBI);
  return true;
}

// Every phi input must be in the result's bank. An input that is not is moved
// across at the end of its predecessor block.
bool IA64InstructionSelector::selectPHI(MachineInstr &I,
                                        MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  bool IsFP = isFloatingPoint(Dst, MRI);
  LLT DstTy = MRI.getType(Dst);
  if (!constrainToBank(Dst, MRI))
    return false;
  for (unsigned Idx = 1, E = I.getNumOperands(); Idx < E; Idx += 2) {
    Register Reg = I.getOperand(Idx).getReg();
    if (isFloatingPoint(Reg, MRI) == IsFP) {
      if (!constrainToBank(Reg, MRI))
        return false;
      continue;
    }
    LLT Ty = IsFP ? DstTy : MRI.getType(Reg);
    if (!Ty.isValid() || (Ty.getSizeInBits() != 32 && Ty.getSizeInBits() != 64))
      return false;
    bool Is32 = Ty.getSizeInBits() == 32;
    unsigned Opcode = IsFP ? (Is32 ? IA64::SETF_S : IA64::SETF_D)
                           : (Is32 ? IA64::GETF_S : IA64::GETF_D);
    MachineBasicBlock &Pred = *I.getOperand(Idx + 1).getMBB();
    Register Moved =
        MRI.createVirtualRegister(IsFP ? &IA64::FRRegClass : &IA64::GRRegClass);
    emitAt(Pred, Pred.getFirstTerminator(), I.getDebugLoc(), Opcode, Moved)
        .addReg(Reg)
        .addReg(IA64::P0);
    I.getOperand(Idx).setReg(Moved);
  }
  return true;
}

// A 22-bit constant is addl with r0, and anything else needs movl, which
// takes two slots.
void IA64InstructionSelector::materialize(MachineInstr &I, Register Dst,
                                          int64_t Imm) const {
  if (Imm == 0)
    emit(I, TargetOpcode::COPY, Dst).addReg(IA64::R0);
  else if (isInt<22>(Imm))
    emit(I, IA64::ADDL, Dst).addImm(Imm).addReg(IA64::R0).addReg(IA64::P0);
  else
    emit(I, IA64::MOVL, Dst).addImm(Imm).addReg(IA64::P0);
}

// Try the patterns with every GR operand retyped to s64. A load or store
// address stays a pointer, since that is what those patterns expect.
bool IA64InstructionSelector::selectByPattern(MachineInstr &I,
                                              MachineRegisterInfo &MRI) const {
  const LLT S64 = LLT::scalar(64);
  SmallVector<std::pair<Register, LLT>, 4> Retyped;
  bool AccessesMemory = I.mayLoadOrStore();
  for (const MachineOperand &MO : I.operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    Register Reg = MO.getReg();
    LLT Ty = MRI.getType(Reg);
    if (!Ty.isValid() || Ty == S64 || Ty.isVector() ||
        Ty.getSizeInBits() > 64 || isFloatingPoint(Reg, MRI))
      continue;
    if (AccessesMemory && MO.isUse() && Ty.isPointer())
      continue;
    Retyped.emplace_back(Reg, Ty);
    MRI.setType(Reg, S64);
  }
  bool Selected = selectImpl(I, *CoverageInfo);
  for (const auto &[Reg, Ty] : Retyped)
    MRI.setType(Reg, Ty);
  return Selected;
}

// The access size picks the instruction, and the value's bank picks an FP
// load/store, which converts between the memory and register formats.
bool IA64InstructionSelector::selectLoadStore(MachineInstr &I,
                                              MachineRegisterInfo &MRI) const {
  auto &Access = cast<GLoadStore>(I);
  if (Access.isAtomic() && isa<GStore>(I))
    return selectAtomic(I, MRI);

  bool IsLoad = isa<GAnyLoad>(I);
  Register Value = I.getOperand(0).getReg();
  Register Address = Access.getPointerReg();
  uint64_t Bits = Access.getMemSizeInBits().getValue();
  unsigned Opcode, Extend = 0;
  if (isFloatingPoint(Value, MRI)) {
    if ((I.getOpcode() != TargetOpcode::G_LOAD &&
         I.getOpcode() != TargetOpcode::G_STORE) ||
        Access.isAtomic())
      return false;
    switch (MRI.getType(Value).getSizeInBits()) {
    case 32:
      Opcode = IsLoad ? IA64::LDFS : IA64::STFS;
      break;
    case 64:
      Opcode = IsLoad ? IA64::LDFD : IA64::STFD;
      break;
    case 80:
      Opcode = IsLoad ? IA64::LDFE : IA64::STFE;
      break;
    default:
      return false;
    }
    if (Bits != MRI.getType(Value).getSizeInBits())
      return false;
  } else {
    switch (Bits) {
    case 8:
      Opcode = IsLoad ? IA64::LD1 : IA64::ST1;
      Extend = IA64::SXT1;
      break;
    case 16:
      Opcode = IsLoad ? IA64::LD2 : IA64::ST2;
      Extend = IA64::SXT2;
      break;
    case 32:
      Opcode = IsLoad ? IA64::LD4 : IA64::ST4;
      Extend = IA64::SXT4;
      break;
    case 64:
      Opcode = IsLoad ? IA64::LD8 : IA64::ST8;
      break;
    default:
      return false;
    }
  }

  if (!IsLoad) {
    emit(I, Opcode)
        .addReg(Address)
        .addReg(Value)
        .addImm(0)
        .addReg(IA64::P0)
        .cloneMemRefs(I);
    I.eraseFromParent();
    return true;
  }

  // Atomic loads use acquire semantics.
  if (Access.isAtomic()) {
    static const unsigned Opcodes[] = {IA64::LD1_ACQ, IA64::LD2_ACQ,
                                       IA64::LD4_ACQ, IA64::LD8_ACQ};
    Opcode = Opcodes[Log2_64(Bits / 8)];
  }

  // No load sign-extends.
  bool SignExtends = I.getOpcode() == TargetOpcode::G_SEXTLOAD;
  if (SignExtends && !Extend)
    return false;
  Register Loaded =
      SignExtends ? MRI.createVirtualRegister(&IA64::GRRegClass) : Value;
  emit(I, Opcode, Loaded)
      .addReg(Address)
      .addImm(0)
      .addReg(IA64::P0)
      .cloneMemRefs(I);
  if (SignExtends)
    emit(I, Extend, Value).addReg(Loaded).addReg(IA64::P0);
  I.eraseFromParent();
  return true;
}

// ld.acq orders the accesses after it and st.rel the ones before it. xchg
// and cmpxchg have acquire semantics; fetchadd also has a .rel form. An
// operation that needs both acquire and release gets an mf before it, and a
// seq_cst store gets an mf after it.
bool IA64InstructionSelector::selectAtomic(MachineInstr &I,
                                           MachineRegisterInfo &MRI) const {
  const MachineMemOperand &MMO = **I.memoperands_begin();
  AtomicOrdering Ordering = MMO.getMergedOrdering();
  uint64_t Size = MMO.getSize().getValue();
  if (!isPowerOf2_64(Size) || Size > 8)
    return false;
  bool Release = isReleaseOrStronger(Ordering);

  if (I.getOpcode() == TargetOpcode::G_STORE) {
    static const unsigned Opcodes[] = {IA64::ST1_REL, IA64::ST2_REL,
                                       IA64::ST4_REL, IA64::ST8_REL};
    // There is no FP st.rel, so store the bits from a GR.
    Register Value = I.getOperand(0).getReg();
    if (isFloatingPoint(Value, MRI)) {
      if (Size != 4 && Size != 8)
        return false;
      Register Bits = MRI.createVirtualRegister(&IA64::GRRegClass);
      emit(I, Size == 4 ? IA64::GETF_S : IA64::GETF_D, Bits)
          .addReg(Value)
          .addReg(IA64::P0);
      Value = Bits;
    }
    emit(I, Opcodes[Log2_64(Size)])
        .addReg(I.getOperand(1).getReg())
        .addReg(Value)
        .addImm(0)
        .addReg(IA64::P0)
        .cloneMemRefs(I);
    if (Ordering == AtomicOrdering::SequentiallyConsistent)
      emit(I, IA64::MF).addReg(IA64::P0);
    I.eraseFromParent();
    return true;
  }

  Register Dst = I.getOperand(0).getReg();
  Register Address = I.getOperand(1).getReg();
  switch (I.getOpcode()) {
  case TargetOpcode::G_ATOMIC_CMPXCHG: {
    // cmpxchg compares all 64 bits of ar.ccv with the zero-extended value
    // read, so zero-extend the expected value.
    static const unsigned Extends[] = {IA64::ZXT1, IA64::ZXT2, IA64::ZXT4};
    Register Compared = I.getOperand(2).getReg();
    if (Size < 8) {
      Register Narrow = Compared;
      Compared = MRI.createVirtualRegister(&IA64::GRRegClass);
      emit(I, Extends[Log2_64(Size)], Compared).addReg(Narrow).addReg(IA64::P0);
    }
    if (Release)
      emit(I, IA64::MF).addReg(IA64::P0);
    emit(I, IA64::CMPXCHG, Dst)
        .addReg(Address)
        .addReg(Compared)
        .addReg(I.getOperand(3).getReg())
        .addImm(Size)
        .cloneMemRefs(I);
    break;
  }
  case TargetOpcode::G_ATOMICRMW_XCHG: {
    static const unsigned Opcodes[] = {IA64::XCHG1, IA64::XCHG2, IA64::XCHG4,
                                       IA64::XCHG8};
    if (Release)
      emit(I, IA64::MF).addReg(IA64::P0);
    emit(I, Opcodes[Log2_64(Size)], Dst)
        .addReg(Address)
        .addReg(I.getOperand(2).getReg())
        .addImm(0)
        .addReg(IA64::P0)
        .cloneMemRefs(I);
    break;
  }
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB: {
    auto C = getIConstantVRegValWithLookThrough(I.getOperand(2).getReg(), MRI);
    if (!C || Size < 4)
      return false;
    int64_t Inc = C->Value.getSExtValue();
    if (Size == 4)
      Inc = SignExtend64<32>(Inc);
    if (I.getOpcode() == TargetOpcode::G_ATOMICRMW_SUB)
      Inc = -uint64_t(Inc);
    if (!IA64TargetLowering::isFetchAddIncrement(Inc))
      return false;
    bool UseRel = Release && !isAcquireOrStronger(Ordering);
    unsigned Opcode;
    if (Size == 4)
      Opcode = UseRel ? IA64::FETCHADD4_REL : IA64::FETCHADD4_ACQ;
    else
      Opcode = UseRel ? IA64::FETCHADD8_REL : IA64::FETCHADD8_ACQ;
    if (Release && !UseRel)
      emit(I, IA64::MF).addReg(IA64::P0);
    emit(I, Opcode, Dst)
        .addReg(Address)
        .addImm(Inc)
        .addImm(0)
        .addReg(IA64::P0)
        .cloneMemRefs(I);
    break;
  }
  default:
    return false;
  }
  I.eraseFromParent();
  return true;
}

// The bits above a narrow value are undefined, so an extension extracts the
// low field of the register.
bool IA64InstructionSelector::selectExtension(MachineInstr &I,
                                              MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  Register Src = I.getOperand(1).getReg();
  bool IsSigned = I.getOpcode() != TargetOpcode::G_ZEXT;
  unsigned Width = I.getOpcode() == TargetOpcode::G_SEXT_INREG
                       ? I.getOperand(2).getImm()
                       : MRI.getType(Src).getSizeInBits();
  if (isFloatingPoint(Dst, MRI) || isFloatingPoint(Src, MRI) || Width >= 64)
    return false;
  unsigned Opcode = 0;
  switch (Width) {
  case 8:
    Opcode = IsSigned ? IA64::SXT1 : IA64::ZXT1;
    break;
  case 16:
    Opcode = IsSigned ? IA64::SXT2 : IA64::ZXT2;
    break;
  case 32:
    Opcode = IsSigned ? IA64::SXT4 : IA64::ZXT4;
    break;
  }
  if (Opcode)
    emit(I, Opcode, Dst).addReg(Src).addReg(IA64::P0);
  else
    emit(I, IsSigned ? IA64::EXTR : IA64::EXTR_U, Dst)
        .addReg(Src)
        .addImm(0)
        .addImm(Width)
        .addReg(IA64::P0);
  I.eraseFromParent();
  return true;
}

// A constant shift moves a bit field: the surviving bits are deposited at
// their new position (dep.z) or extracted from their old one (extr).
bool IA64InstructionSelector::selectShift(MachineInstr &I,
                                          MachineRegisterInfo &MRI) const {
  auto Amount =
      getIConstantVRegValWithLookThrough(I.getOperand(2).getReg(), MRI);
  if (!Amount || Amount->Value.uge(64) || Amount->Value.isZero())
    return selectByPattern(I, MRI);
  unsigned Count = Amount->Value.getZExtValue();
  unsigned Opcode;
  switch (I.getOpcode()) {
  case TargetOpcode::G_SHL:
    Opcode = IA64::DEP_Z;
    break;
  case TargetOpcode::G_LSHR:
    Opcode = IA64::EXTR_U;
    break;
  default:
    Opcode = IA64::EXTR;
    break;
  }
  emit(I, Opcode, I.getOperand(0).getReg())
      .addReg(I.getOperand(1).getReg())
      .addImm(Count)
      .addImm(64 - Count)
      .addReg(IA64::P0);
  I.eraseFromParent();
  return true;
}

static IA64CC::CondCode getIntegerCondition(CmpInst::Predicate Pred) {
  switch (Pred) {
  case CmpInst::ICMP_EQ:
    return IA64CC::EQ;
  case CmpInst::ICMP_NE:
    return IA64CC::NE;
  case CmpInst::ICMP_SLT:
    return IA64CC::LT;
  case CmpInst::ICMP_SGE:
    return IA64CC::GE;
  case CmpInst::ICMP_SGT:
    return IA64CC::GT;
  case CmpInst::ICMP_SLE:
    return IA64CC::LE;
  case CmpInst::ICMP_ULT:
    return IA64CC::LTU;
  case CmpInst::ICMP_UGE:
    return IA64CC::GEU;
  case CmpInst::ICMP_UGT:
    return IA64CC::GTU;
  case CmpInst::ICMP_ULE:
    return IA64CC::LEU;
  default:
    llvm_unreachable("not a compare of integers");
  }
}

// The FP compare conditions. The two missing ones are ONE (less or
// greater) and UEQ (its complement).
static std::optional<IA64CC::CondCode>
getFloatingPointCondition(CmpInst::Predicate Pred) {
  switch (Pred) {
  case CmpInst::FCMP_OEQ:
    return IA64CC::OEQ;
  case CmpInst::FCMP_UNE:
    return IA64CC::UNE;
  case CmpInst::FCMP_OLT:
    return IA64CC::OLT;
  case CmpInst::FCMP_UGE:
    return IA64CC::UGE;
  case CmpInst::FCMP_OLE:
    return IA64CC::OLE;
  case CmpInst::FCMP_UGT:
    return IA64CC::UGT;
  case CmpInst::FCMP_OGT:
    return IA64CC::OGT;
  case CmpInst::FCMP_ULE:
    return IA64CC::ULE;
  case CmpInst::FCMP_OGE:
    return IA64CC::OGE;
  case CmpInst::FCMP_ULT:
    return IA64CC::ULT;
  case CmpInst::FCMP_UNO:
    return IA64CC::UO;
  case CmpInst::FCMP_ORD:
    return IA64CC::ORD;
  default:
    return std::nullopt;
  }
}

// The condition a tested register represents. If a compare defines it, the
// compare is repeated where it is used, since testing the register would be
// a compare as well and no cheaper. Anything else is tested for nonzero.
Condition
IA64InstructionSelector::getCondition(Register Reg,
                                      const MachineRegisterInfo &MRI) const {
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (Def && Def->getOpcode() == TargetOpcode::G_ICMP) {
    auto Pred = CmpInst::Predicate(Def->getOperand(1).getPredicate());
    IA64CC::CondCode CC = getIntegerCondition(Pred);
    Register LHS = Def->getOperand(2).getReg();
    Register RHS = Def->getOperand(3).getReg();
    // A test of whether a single bit is set is a bit test.
    const MachineInstr *And = MRI.getVRegDef(LHS);
    auto Zero = getIConstantVRegValWithLookThrough(RHS, MRI);
    if ((CC == IA64CC::EQ || CC == IA64CC::NE) && Zero &&
        Zero->Value.isZero() && And &&
        And->getOpcode() == TargetOpcode::G_AND) {
      if (auto Mask = getIConstantVRegValWithLookThrough(
              And->getOperand(2).getReg(), MRI);
          Mask && Mask->Value.isPowerOf2())
        return {Condition::RegisterAndConstant,
                CC == IA64CC::EQ ? IA64CC::BitClear : IA64CC::BitSet,
                And->getOperand(1).getReg(), Register(),
                Mask->Value.logBase2()};
    }
    if (auto C = getIConstantVRegValWithLookThrough(RHS, MRI);
        C && C->Value.isSignedIntN(64) &&
        IA64CC::isLegalImmediate(CC, C->Value.getSExtValue()))
      return {Condition::RegisterAndConstant, CC, LHS, Register(),
              C->Value.getSExtValue()};
    return {Condition::TwoRegisters, CC, LHS, RHS, 0};
  }
  if (Def && Def->getOpcode() == TargetOpcode::G_FCMP) {
    auto Pred = CmpInst::Predicate(Def->getOperand(1).getPredicate());
    if (auto CC = getFloatingPointCondition(Pred))
      return {Condition::FloatingPoint, *CC, Def->getOperand(2).getReg(),
              Def->getOperand(3).getReg(), 0};
  }
  return {Condition::TwoRegisters, IA64CC::NE, Reg, IA64::R0, 0};
}

static void addCondition(MachineInstrBuilder &MIB, const Condition &Cond) {
  MIB.addImm(Cond.CC).addReg(Cond.LHS);
  if (Cond.Kind == Condition::RegisterAndConstant)
    MIB.addImm(Cond.Imm);
  else
    MIB.addReg(Cond.RHS);
}

bool IA64InstructionSelector::selectICmp(MachineInstr &I,
                                         MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  Condition Cond = getCondition(Dst, MRI);
  MachineInstrBuilder MIB = emit(
      I,
      Cond.Kind == Condition::RegisterAndConstant ? IA64::SETCCi : IA64::SETCC,
      Dst);
  addCondition(MIB, Cond);
  I.eraseFromParent();
  return true;
}

bool IA64InstructionSelector::selectFCmp(MachineInstr &I,
                                         MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  Register LHS = I.getOperand(2).getReg(), RHS = I.getOperand(3).getReg();
  auto Pred = CmpInst::Predicate(I.getOperand(1).getPredicate());
  auto Compare = [&](IA64CC::CondCode CC, Register Result) {
    emit(I, IA64::SETCCf, Result).addImm(CC).addReg(LHS).addReg(RHS);
  };
  auto Both = [&](IA64CC::CondCode First, IA64CC::CondCode Second,
                  unsigned Combine) {
    Register A = MRI.createVirtualRegister(&IA64::GRRegClass);
    Register B = MRI.createVirtualRegister(&IA64::GRRegClass);
    Compare(First, A);
    Compare(Second, B);
    emit(I, Combine, Dst).addReg(A).addReg(B).addReg(IA64::P0);
  };
  switch (Pred) {
  case CmpInst::FCMP_FALSE:
    materialize(I, Dst, 0);
    break;
  case CmpInst::FCMP_TRUE:
    materialize(I, Dst, 1);
    break;
  // Less or greater.
  case CmpInst::FCMP_ONE:
    Both(IA64CC::OLT, IA64CC::OGT, IA64::OR);
    break;
  // Neither less nor greater.
  case CmpInst::FCMP_UEQ:
    Both(IA64CC::UGE, IA64CC::ULE, IA64::AND);
    break;
  default:
    Compare(*getFloatingPointCondition(Pred), Dst);
    break;
  }
  I.eraseFromParent();
  return true;
}

// The result starts as the false value, and the true value replaces it under
// the compare's predicate.
bool IA64InstructionSelector::selectSelect(MachineInstr &I,
                                           MachineRegisterInfo &MRI) const {
  static const unsigned Opcodes[2][3] = {
      {IA64::SELECT, IA64::SELECTi, IA64::SELECTf},
      {IA64::SELECT_FR, IA64::SELECT_FRi, IA64::SELECT_FRf}};
  Register Dst = I.getOperand(0).getReg();
  Condition Cond = getCondition(I.getOperand(1).getReg(), MRI);
  MachineInstrBuilder MIB =
      emit(I, Opcodes[isFloatingPoint(Dst, MRI)][Cond.Kind], Dst)
          .addReg(I.getOperand(3).getReg())
          .addReg(I.getOperand(2).getReg());
  addCondition(MIB, Cond);
  I.eraseFromParent();
  return true;
}

// f0 is +0.0 and f1 is 1.0. Any other constant is built in a GR and moved
// over with setf.
bool IA64InstructionSelector::selectFConstant(MachineInstr &I,
                                              MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  const APFloat &Value = I.getOperand(1).getFPImm()->getValueAPF();
  unsigned Size = MRI.getType(Dst).getSizeInBits();
  if (!isFloatingPoint(Dst, MRI) || (Size != 32 && Size != 64))
    return false;
  if (Value.isPosZero()) {
    emit(I, TargetOpcode::COPY, Dst).addReg(IA64::F0);
  } else if (Value.isExactlyValue(1.0)) {
    emit(I, TargetOpcode::COPY, Dst).addReg(IA64::F1);
  } else {
    Register Bits = MRI.createVirtualRegister(&IA64::GRRegClass);
    materialize(I, Bits, Value.bitcastToAPInt().getZExtValue());
    emit(I, Size == 32 ? IA64::SETF_S : IA64::SETF_D, Dst)
        .addReg(Bits)
        .addReg(IA64::P0);
  }
  I.eraseFromParent();
  return true;
}

// Load a symbol's address from its linkage table entry at a GP-relative
// offset.
Register
IA64InstructionSelector::loadLinkageTableEntry(MachineInstr &I, Register Dst,
                                               const MachineOperand &Symbol,
                                               unsigned Flags) const {
  MachineFunction &MF = *I.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register Entry = MRI.createVirtualRegister(&IA64::GRRegClass);
  MachineOperand Flagged = Symbol;
  Flagged.setTargetFlags(Flags);
  emit(I, IA64::ADDL, Entry).add(Flagged).addReg(IA64::R1).addReg(IA64::P0);
  emit(I, IA64::LD8, Dst)
      .addReg(Entry)
      .addImm(0)
      .addReg(IA64::P0)
      .addMemOperand(MF.getMachineMemOperand(
          MachinePointerInfo::getGOT(MF),
          MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
              MachineMemOperand::MOInvariant,
          LLT::scalar(64), Align(8)));
  return Dst;
}

// The address of a possibly preemptible symbol is loaded from the linkage
// table, as is a function's, which is the address of its function
// descriptor. A module-local symbol is at a link-time GP-relative offset.
bool IA64InstructionSelector::selectGlobalValue(
    MachineInstr &I, MachineRegisterInfo &MRI) const {
  Register Dst = I.getOperand(0).getReg();
  const MachineOperand &Symbol = I.getOperand(1);
  const GlobalValue *GV = Symbol.getGlobal();
  if (Symbol.getOffset() != 0)
    return false;

  // Local-exec and initial-exec TLS variables are at an offset from the
  // thread pointer, known at link time or loaded from the linkage table. A
  // link-time offset is added with one addl if it fits in 22 bits, which is
  // assumed unless the TLS size says otherwise, and addl only takes r0-r3 as
  // its base. The legalizer turned the other models into calls.
  if (GV->isThreadLocal()) {
    MachineOperand Relative = Symbol;
    Relative.setTargetFlags(IA64::S_TPREL);
    unsigned Size = TM.Options.TLSSize;
    switch (TM.getTLSModel(GV)) {
    case TLSModel::LocalExec:
      if (Size != 0 && Size <= 14) {
        emit(I, IA64::ADDS, Dst)
            .add(Relative)
            .addReg(IA64::R13)
            .addReg(IA64::P0);
      } else if (Size == 0 || Size <= 22) {
        Register Thread = MRI.createVirtualRegister(&IA64::GR0_3RegClass);
        emit(I, TargetOpcode::COPY, Thread).addReg(IA64::R13);
        emit(I, IA64::ADDL, Dst).add(Relative).addReg(Thread).addReg(IA64::P0);
      } else {
        Register Offset = MRI.createVirtualRegister(&IA64::GRRegClass);
        emit(I, IA64::MOVL, Offset).add(Relative).addReg(IA64::P0);
        emit(I, IA64::ADD, Dst)
            .addReg(Offset)
            .addReg(IA64::R13)
            .addReg(IA64::P0);
      }
      break;
    case TLSModel::InitialExec: {
      Register Offset = MRI.createVirtualRegister(&IA64::GRRegClass);
      loadLinkageTableEntry(I, Offset, Symbol, IA64::S_LTOFF_TPREL);
      emit(I, IA64::ADD, Dst).addReg(Offset).addReg(IA64::R13).addReg(IA64::P0);
      break;
    }
    default:
      return false;
    }
    I.eraseFromParent();
    return true;
  }

  // A small code model variable is addressable by addl from r0.
  if (const auto *Var = dyn_cast<GlobalVariable>(GV);
      Var && Var->getCodeModel() == CodeModel::Small) {
    emit(I, IA64::ADDL, Dst).add(Symbol).addReg(IA64::R0).addReg(IA64::P0);
    I.eraseFromParent();
    return true;
  }

  // An undefined weak symbol has no GP-relative offset.
  if (GV->getValueType()->isFunctionTy() || !TM.shouldAssumeDSOLocal(GV) ||
      GV->hasExternalWeakLinkage()) {
    loadLinkageTableEntry(I, Dst, Symbol,
                          GV->getValueType()->isFunctionTy()
                              ? IA64::S_LTOFF_FPTR
                              : IA64::S_LTOFF);
    I.eraseFromParent();
    return true;
  }

  MachineOperand Relative = Symbol;
  Relative.setTargetFlags(IA64::S_GPREL);
  const auto &TLOF =
      static_cast<const IA64TargetObjectFile &>(*TM.getObjFileLowering());
  if (TLOF.isShortData(GV, TM)) {
    emit(I, IA64::ADDL, Dst).add(Relative).addReg(IA64::R1).addReg(IA64::P0);
  } else {
    Register Offset = MRI.createVirtualRegister(&IA64::GRRegClass);
    emit(I, IA64::MOVL, Offset).add(Relative).addReg(IA64::P0);
    emit(I, IA64::ADD, Dst).addReg(Offset).addReg(IA64::R1).addReg(IA64::P0);
  }
  I.eraseFromParent();
  return true;
}

bool IA64InstructionSelector::selectInstruction(
    MachineInstr &I, MachineRegisterInfo &MRI) const {
  if (!I.isPreISelOpcode()) {
    if (I.isCopy())
      return selectCopy(I, MRI);
    if (I.isPHI())
      return selectPHI(I, MRI);
    return true;
  }

  switch (I.getOpcode()) {
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_ZEXTLOAD:
  case TargetOpcode::G_SEXTLOAD:
  case TargetOpcode::G_STORE:
    return selectLoadStore(I, MRI);
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_ASHR:
    return selectShift(I, MRI);
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_SEXT:
    return selectExtension(I, MRI);
  case TargetOpcode::G_SEXT_INREG:
    return selectByPattern(I, MRI) || selectExtension(I, MRI);
  case TargetOpcode::G_ICMP:
    return selectICmp(I, MRI);
  case TargetOpcode::G_FCMP:
    return selectFCmp(I, MRI);
  case TargetOpcode::G_SELECT:
    return selectSelect(I, MRI);
  case TargetOpcode::G_FCONSTANT:
    return selectFConstant(I, MRI);
  case TargetOpcode::G_GLOBAL_VALUE:
    return selectGlobalValue(I, MRI);

  case TargetOpcode::G_CONSTANT: {
    const APInt &Value = I.getOperand(1).getCImm()->getValue();
    if (Value.getBitWidth() > 64 ||
        isFloatingPoint(I.getOperand(0).getReg(), MRI))
      return false;
    materialize(I, I.getOperand(0).getReg(), Value.getSExtValue());
    I.eraseFromParent();
    return true;
  }

  // The same bits, in the same register or in one of the other bank.
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_ANYEXT:
  case TargetOpcode::G_PTRTOINT:
  case TargetOpcode::G_INTTOPTR:
  case TargetOpcode::G_BITCAST:
  case TargetOpcode::G_FREEZE:
  case TargetOpcode::G_CONSTANT_FOLD_BARRIER:
    I.setDesc(TII.get(TargetOpcode::COPY));
    return selectCopy(I, MRI);
  case TargetOpcode::G_PHI:
    I.setDesc(TII.get(TargetOpcode::PHI));
    return selectPHI(I, MRI);
  case TargetOpcode::G_IMPLICIT_DEF:
    I.setDesc(TII.get(TargetOpcode::IMPLICIT_DEF));
    return constrainToBank(I.getOperand(0).getReg(), MRI);

  // Pointer arithmetic is integer arithmetic.
  case TargetOpcode::G_PTR_ADD:
    I.setDesc(TII.get(TargetOpcode::G_ADD));
    return selectByPattern(I, MRI);

  // adds r1 = offset, sp, when the offset is known.
  case TargetOpcode::G_FRAME_INDEX:
    emit(I, IA64::ADDS, I.getOperand(0).getReg())
        .addImm(0)
        .add(I.getOperand(1))
        .addReg(IA64::P0);
    I.eraseFromParent();
    return true;

  case TargetOpcode::G_JUMP_TABLE:
  case TargetOpcode::G_BLOCK_ADDR:
    loadLinkageTableEntry(I, I.getOperand(0).getReg(), I.getOperand(1),
                          IA64::S_LTOFF);
    I.eraseFromParent();
    return true;

  // Constant pool entries are module-local.
  case TargetOpcode::G_CONSTANT_POOL: {
    Register Offset = MRI.createVirtualRegister(&IA64::GRRegClass);
    MachineOperand Relative = I.getOperand(1);
    Relative.setTargetFlags(IA64::S_GPREL);
    emit(I, IA64::MOVL, Offset).add(Relative).addReg(IA64::P0);
    emit(I, IA64::ADD, I.getOperand(0).getReg())
        .addReg(Offset)
        .addReg(IA64::R1)
        .addReg(IA64::P0);
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BRCOND: {
    static const unsigned Opcodes[] = {IA64::BRCC, IA64::BRCCi, IA64::BRCCf};
    Condition Cond = getCondition(I.getOperand(0).getReg(), MRI);
    MachineInstrBuilder MIB = emit(I, Opcodes[Cond.Kind]).add(I.getOperand(1));
    addCondition(MIB, Cond);
    I.eraseFromParent();
    return true;
  }
  case TargetOpcode::G_ATOMIC_CMPXCHG:
  case TargetOpcode::G_ATOMICRMW_XCHG:
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB:
    return selectAtomic(I, MRI);

  // Atomic loads and stores already carry the ordering they need, so only a
  // cross-thread seq_cst fence emits an mf.
  case TargetOpcode::G_FENCE:
    if (I.getOperand(0).getImm() ==
            int64_t(AtomicOrdering::SequentiallyConsistent) &&
        I.getOperand(1).getImm() != SyncScope::SingleThread)
      emit(I, IA64::MF).addReg(IA64::P0);
    I.eraseFromParent();
    return true;

  // The lfetch hint is the number of cache levels the line need not be
  // kept in (3 - locality), and .excl is for a prefetch for write.
  case TargetOpcode::G_PREFETCH:
    emit(I, I.getOperand(1).getImm() ? IA64::LFETCH_EXCL : IA64::LFETCH)
        .addReg(I.getOperand(0).getReg())
        .addImm(3 - I.getOperand(2).getImm())
        .addReg(IA64::P0);
    I.eraseFromParent();
    return true;

  case TargetOpcode::G_BRINDIRECT:
    emit(I, IA64::BRIND).addReg(I.getOperand(0).getReg());
    I.eraseFromParent();
    return true;

  // The quotient plus three scratch registers for the iteration.
  case TargetOpcode::G_FDIV: {
    unsigned Size = MRI.getType(I.getOperand(0).getReg()).getSizeInBits();
    if (Size != 32 && Size != 64)
      return false;
    MachineInstrBuilder MIB = emit(I, Size == 32 ? IA64::FDIV_S : IA64::FDIV_D,
                                   I.getOperand(0).getReg());
    for (unsigned N = 0; N != 3; ++N)
      MIB.addDef(MRI.createVirtualRegister(&IA64::FRRegClass),
                 RegState::EarlyClobber);
    MIB->getOperand(0).setIsEarlyClobber();
    MIB.addReg(I.getOperand(1).getReg()).addReg(I.getOperand(2).getReg());
    MIB.setMIFlags(I.getFlags());
    I.eraseFromParent();
    return true;
  }

  // fmerge.s takes the sign of one value and the rest of the other,
  // regardless of either's precision.
  case TargetOpcode::G_FCOPYSIGN:
    emit(I, IA64::FMERGE_S, I.getOperand(0).getReg())
        .addReg(I.getOperand(2).getReg())
        .addReg(I.getOperand(1).getReg())
        .addReg(IA64::P0);
    I.eraseFromParent();
    return true;

  default:
    return selectByPattern(I, MRI);
  }
}

// A call that throws does not return normally, and gp holds whatever the
// throwing function had there. A landing pad must restore its own function's
// GP first, as the code after a call does.
void IA64InstructionSelector::setupMF(MachineFunction &MF,
                                      GISelValueTracking *VT,
                                      CodeGenCoverage *CoverageInfo,
                                      ProfileSummaryInfo *PSI,
                                      BlockFrequencyInfo *BFI) {
  InstructionSelector::setupMF(MF, VT, CoverageInfo, PSI, BFI);
  if (none_of(MF, [](const MachineBasicBlock &MBB) { return MBB.isEHPad(); }))
    return;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register GPReg = MRI.getLiveInVirtReg(IA64::R1);
  if (!GPReg) {
    MachineBasicBlock &Entry = MF.front();
    GPReg = MF.addLiveIn(IA64::R1, &IA64::GRSaveRegClass);
    if (!Entry.isLiveIn(IA64::R1))
      Entry.addLiveIn(IA64::R1);
    BuildMI(Entry, Entry.begin(), DebugLoc(), TII.get(TargetOpcode::COPY),
            GPReg)
        .addReg(IA64::R1);
  }

  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.isEHPad())
      continue;
    BuildMI(MBB, MBB.SkipPHIsLabelsAndDebug(MBB.begin()), DebugLoc(),
            TII.get(TargetOpcode::COPY), IA64::R1)
        .addReg(GPReg);
  }
}

bool IA64InstructionSelector::select(MachineInstr &I) {
  MachineRegisterInfo &MRI = I.getMF()->getRegInfo();
  Emitted.clear();
  if (!selectInstruction(I, MRI))
    return false;
  for (MachineInstr *MI : Emitted) {
    if (MI->isCopy()) {
      if (!selectCopy(*MI, MRI))
        return false;
    } else {
      constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
    }
  }
  return true;
}

namespace llvm {
InstructionSelector *
createIA64InstructionSelector(const IA64TargetMachine &TM,
                              const IA64Subtarget &STI,
                              const IA64RegisterBankInfo &RBI) {
  return new IA64InstructionSelector(TM, STI, RBI);
}
} // end namespace llvm

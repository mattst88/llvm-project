//===-- IA64CallLowering.cpp - Call lowering ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GlobalISel lowering of formal arguments, returns and calls. Locations come
// from the same calling convention SelectionDAG uses, and the handling here
// mirrors IA64ISelLowering.cpp. Anything not yet supported is rejected, and
// the function falls back to SelectionDAG.
//
//===----------------------------------------------------------------------===//

#include "IA64CallLowering.h"
#include "IA64CallingConv.h"
#include "IA64FrameLowering.h"
#include "IA64ISelLowering.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

/// The arguments a function finds, which are in its input registers and
/// above the scratch area its caller left it.
struct IA64IncomingValueHandler : public CallLowering::IncomingValueHandler {
  IA64IncomingValueHandler(MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI)
      : CallLowering::IncomingValueHandler(MIRBuilder, MRI) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    // Offsets are relative to the end of the caller's scratch area, where the
    // first stack argument is.
    MachineFunction &MF = MIRBuilder.getMF();
    int FI = MF.getFrameInfo().CreateFixedObject(alignTo(Size, 8), Offset,
                                                 /*IsImmutable=*/true);
    MPO = MachinePointerInfo::getFixedStack(MF, FI);
    return MIRBuilder.buildFrameIndex(LLT::pointer(0, 64), FI).getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    MIRBuilder.buildLoad(
        ValVReg, Addr,
        *MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                 inferAlignFromPtrInfo(MF, MPO)));
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags = {}) override {
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  /// A floating-point argument there was no floating-point register left
  /// for, which is its bits in the slot.
  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk = nullptr) override {
    const CCValAssign &VA = VAs[0];
    if (!VA.isRegLoc() || VA.getLocInfo() != CCValAssign::BCvt)
      return 0;
    markPhysRegUsed(VA.getLocReg());
    if (VA.getValVT() == MVT::f32)
      MIRBuilder.buildTrunc(
          Arg.Regs[0],
          MIRBuilder.buildCopy(LLT::scalar(64), Register(VA.getLocReg())));
    else
      MIRBuilder.buildCopy(Arg.Regs[0], Register(VA.getLocReg()));
    return 1;
  }

  virtual void markPhysRegUsed(MCRegister PhysReg) {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

/// Values returned by a call, which the call implicitly defines.
struct IA64CallReturnHandler : public IA64IncomingValueHandler {
  IA64CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                        MachineInstrBuilder &MIB)
      : IA64IncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder &MIB;
};

/// Outgoing call arguments and return values.
struct IA64OutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  IA64OutgoingValueHandler(MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI, MachineInstrBuilder &MIB,
                           bool IsCall)
      : CallLowering::OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB),
        IsCall(IsCall) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    // The 16 bytes at sp are the callee's scratch area; stack arguments
    // start above it.
    MachineFunction &MF = MIRBuilder.getMF();
    LLT P0 = LLT::pointer(0, 64);
    Offset += IA64FrameLowering::ScratchAreaSize;
    if (!StackPtr)
      StackPtr = MIRBuilder.buildCopy(P0, Register(IA64::R12)).getReg(0);
    MPO = MachinePointerInfo::getStack(MF, Offset);
    return MIRBuilder
        .buildPtrAdd(P0, StackPtr,
                     MIRBuilder.buildConstant(LLT::scalar(64), Offset))
        .getReg(0);
  }

  /// The 64-bit slot value for this argument. An FP value passed in a slot
  /// is its bit pattern; a float occupies the low half of the slot.
  Register toSlot(Register ValVReg, const CCValAssign &VA) {
    if (VA.getLocInfo() != CCValAssign::BCvt)
      return extendRegister(ValVReg, VA);
    if (MRI.getType(ValVReg).getSizeInBits() == 64)
      return ValVReg;
    return MIRBuilder.buildAnyExt(LLT::scalar(64), ValVReg).getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    // Slots are 8 bytes at a multiple of 8 from sp, which the pointer info
    // alone does not convey.
    MachineFunction &MF = MIRBuilder.getMF();
    Register Slot = toSlot(ValVReg, VA);
    if (VA.getLocInfo() == CCValAssign::BCvt)
      MemTy = MRI.getType(Slot);
    MIRBuilder.buildStore(Slot, Addr,
                          *MF.getMachineMemOperand(MPO,
                                                   MachineMemOperand::MOStore,
                                                   MemTy, Align(8)));
  }

  /// A floating-point argument there was no floating-point register left
  /// for, which is its bits in the slot.
  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk = nullptr) override {
    const CCValAssign &VA = VAs[0];
    if (!VA.isRegLoc() || VA.getLocInfo() != CCValAssign::BCvt)
      return 0;
    assignValueToReg(Arg.Regs[0], VA.getLocReg(), VA);
    return 1;
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA,
                        ISD::ArgFlagsTy Flags = {}) override {
    Register ExtReg = toSlot(ValVReg, VA);
    if (!IsCall) {
      MIRBuilder.buildCopy(PhysReg, ExtReg);
      MIB.addUse(PhysReg, RegState::Implicit);
      return;
    }
    // The calling convention names the callee's input registers; the caller
    // passes them in its output registers, r120-r127.
    if (PhysReg >= IA64::R32 && PhysReg <= IA64::R39)
      PhysReg = IA64::R120 + (PhysReg - IA64::R32);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addUse(PhysReg);
  }

  MachineInstrBuilder &MIB;
  bool IsCall;
  Register StackPtr;
};

} // end anonymous namespace

/// The argument registers of the first eight slots, as the callee sees them.
static const MCPhysReg Slots[] = {IA64::R32, IA64::R33, IA64::R34, IA64::R35,
                                  IA64::R36, IA64::R37, IA64::R38, IA64::R39};

IA64CallLowering::IA64CallLowering(const IA64TargetLowering &TLI)
    : CallLowering(&TLI) {}

/// The ABI leaves the bits above a value narrower than its slot undefined,
/// so drop zeroext/signext: code from another compiler may not have extended
/// the value, whatever the attributes say.
static void dropExtensions(MutableArrayRef<CallLowering::ArgInfo> Args) {
  for (CallLowering::ArgInfo &Arg : Args) {
    for (ISD::ArgFlagsTy &Flags : Arg.Flags) {
      ISD::ArgFlagsTy Kept;
      if (Flags.isInReg())
        Kept.setInReg();
      if (Flags.isSRet())
        Kept.setSRet();
      if (Flags.isNest())
        Kept.setNest();
      if (Flags.isReturned())
        Kept.setReturned();
      if (Flags.isPointer()) {
        Kept.setPointer();
        Kept.setPointerAddrSpace(Flags.getPointerAddrSpace());
      }
      Kept.setOrigAlign(Flags.getNonZeroOrigAlign());
      Kept.setMemAlign(Flags.getNonZeroMemAlign());
      Flags = Kept;
    }
  }
}

/// Whether all of these are of the kinds that are done here: integers,
/// pointers and single and double precision, each as itself.
static bool areSupported(ArrayRef<CallLowering::ArgInfo> Args) {
  return all_of(Args, [](const CallLowering::ArgInfo &Arg) {
    const ISD::ArgFlagsTy &Flags = Arg.Flags[0];
    if (Flags.isByVal() || Flags.isByRef() || Flags.isInAlloca() ||
        Flags.isPreallocated() || Flags.isInConsecutiveRegs() ||
        Flags.isSwiftSelf() || Flags.isSwiftAsync() || Flags.isSwiftError())
      return false;
    return Arg.Ty->isIntegerTy() || Arg.Ty->isPointerTy() ||
           Arg.Ty->isFloatTy() || Arg.Ty->isDoubleTy();
  });
}

/// A floating-point argument of a call that says its callee takes a variable
/// number of them is in a floating-point register and in its slot too, since
/// the callee may or may not have named it. The second of the two places is
/// taken out of the list, which is to have one place for each value, and put
/// in \p SlotCopies. Whether what is left can be done here is the answer.
static bool takeSlotCopies(SmallVectorImpl<CCValAssign> &ArgLocs,
                           SmallVectorImpl<CCValAssign> &SlotCopies) {
  for (unsigned I = 0; I != ArgLocs.size();) {
    const CCValAssign &VA = ArgLocs[I];
    if (!VA.needsCustom()) {
      ++I;
      continue;
    }
    // Extended precision is two slots, and a member of an aggregate may be
    // half of one.
    if (!VA.isRegLoc() || VA.getLocInfo() != CCValAssign::BCvt ||
        VA.getValVT() == MVT::f80)
      return false;
    if (I != 0 && ArgLocs[I - 1].getValNo() == VA.getValNo()) {
      SlotCopies.push_back(VA);
      ArgLocs.erase(ArgLocs.begin() + I);
      continue;
    }
    ++I;
  }
  return true;
}

bool IA64CallLowering::canLowerReturn(MachineFunction &MF,
                                      CallingConv::ID CallConv,
                                      SmallVectorImpl<BaseArgInfo> &Outs,
                                      bool IsVarArg) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, MF.getFunction().getContext());
  return checkReturn(CCInfo, Outs, RetCC_IA64);
}

bool IA64CallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                   const Value *Val, ArrayRef<Register> VRegs,
                                   FunctionLoweringInfo &FLI,
                                   Register SwiftErrorVReg) const {
  if (SwiftErrorVReg)
    return false;

  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getDataLayout();

  auto MIB = MIRBuilder.buildInstrNoInsert(IA64::BR_RET)
                 .addUse(IA64::B0)
                 .addImm(0)
                 .addImm(1)
                 .addImm(0)
                 .addUse(IA64::P0);

  // A function returning in memory returns the sret address in r8.
  Register Address = MF.getInfo<IA64MachineFunctionInfo>()->getSRetReg();
  if (!FLI.CanLowerReturn) {
    insertSRetStores(MIRBuilder, Val->getType(), VRegs, FLI.DemoteRegister);
  } else if (!VRegs.empty()) {
    SmallVector<ArgInfo, 4> SplitArgs;
    ArgInfo OrigArg{VRegs, Val->getType(), 0};
    setArgFlags(OrigArg, AttributeList::ReturnIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    if (!areSupported(SplitArgs))
      return false;

    OutgoingValueAssigner Assigner(RetCC_IA64);
    IA64OutgoingValueHandler Handler(MIRBuilder, MRI, MIB, /*IsCall=*/false);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                       F.getCallingConv(), F.isVarArg()))
      return false;
  }
  if (Address) {
    MIRBuilder.buildCopy(Register(IA64::R8), Address);
    MIB.addUse(IA64::R8, RegState::Implicit);
  }

  MIRBuilder.insertInstr(MIB);
  return true;
}

bool IA64CallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                            const Function &F,
                                            ArrayRef<ArrayRef<Register>> VRegs,
                                            FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getDataLayout();

  // A return demoted to memory gets an sret argument, as an explicit sret
  // would.
  SmallVector<ArgInfo, 8> SplitArgs;
  Register Address;
  if (!FLI.CanLowerReturn) {
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);
    Address = FLI.DemoteRegister;
  }

  // Zero-sized arguments have no vregs and are skipped.
  unsigned Idx = 0;
  for (const Argument &Arg : F.args()) {
    if (DL.getTypeStoreSize(Arg.getType()).isZero())
      continue;
    ArgInfo OrigArg{VRegs[Idx], Arg.getType(), Arg.getArgNo()};
    setArgFlags(OrigArg, Arg.getArgNo() + AttributeList::FirstArgIndex, DL, F);
    if (Arg.hasStructRetAttr() && !Address)
      Address = VRegs[Idx][0];
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    ++Idx;
  }
  if (!areSupported(SplitArgs))
    return false;
  dropExtensions(SplitArgs);

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs, F.getContext());
  IncomingValueAssigner Assigner(CC_IA64);
  // In a varargs function these are named arguments, read from the FP
  // register and not also from the GR slot.
  SmallVector<CCValAssign, 8> SlotCopies;
  if (!determineAssignments(Assigner, SplitArgs, CCInfo) ||
      !takeSlotCopies(ArgLocs, SlotCopies))
    return false;
  IA64IncomingValueHandler Handler(MIRBuilder, MRI);
  if (!handleAssignments(Handler, SplitArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;

  // va_arg reads variadic arguments from consecutive slots in memory, so the
  // ones that came in registers are stored just below the stack ones. The
  // last two fit in the caller's scratch area and the rest go at the top of
  // this frame. An unused argument register may have its NaT bit set, on
  // which an ordinary store faults, so st8.spill is used.
  if (F.isVarArg()) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    LLT P0 = LLT::pointer(0, 64);
    unsigned First = CCInfo.getFirstUnallocated(Slots);
    auto SlotOffset = [](unsigned Slot) { return -8 * int64_t(8 - Slot); };
    // The slots are a single mutable object: va_arg walks from slot to
    // slot, and a load folded to an offset from the first slot must alias
    // the spill of the slot it reads.
    int VarArgsFI =
        First != 8
            ? MFI.CreateFixedObject(8 * (8 - First), SlotOffset(First), false)
            : MFI.CreateFixedObject(8, CCInfo.getStackSize(), true);
    for (unsigned Slot = First; Slot != 8; ++Slot) {
      int64_t Offset = 8 * int64_t(Slot - First);
      Handler.markPhysRegUsed(Slots[Slot]);
      auto Base = MIRBuilder.buildFrameIndex(P0, VarArgsFI);
      auto Addr = MIRBuilder.buildPtrAdd(
          P0, Base, MIRBuilder.buildConstant(LLT::scalar(64), Offset));
      auto Value = MIRBuilder.buildCopy(LLT::scalar(64), Register(Slots[Slot]));
      MRI.setRegClass(Addr.getReg(0), &IA64::GRRegClass);
      MRI.setRegClass(Value.getReg(0), &IA64::GRRegClass);
      MIRBuilder.buildInstr(IA64::ST8_SPILL)
          .addUse(Addr.getReg(0))
          .addUse(Value.getReg(0))
          .addImm(0)
          .addReg(IA64::P0)
          .addMemOperand(MF.getMachineMemOperand(
              MachinePointerInfo::getFixedStack(MF, VarArgsFI, Offset),
              MachineMemOperand::MOStore, LLT::scalar(64), Align(8)));
    }
    MF.getInfo<IA64MachineFunctionInfo>()->setSpillsVarArgs(First != 8);
    MF.getInfo<IA64MachineFunctionInfo>()->setVarArgsFrameIndex(VarArgsFI);
  }

  // A function returning in memory returns the sret address in r8, so keep
  // it.
  if (Address) {
    Register SRetReg = MRI.createGenericVirtualRegister(LLT::pointer(0, 64));
    MRI.setRegClass(SRetReg, &IA64::GRRegClass);
    MIRBuilder.buildCopy(SRetReg, Address);
    MF.getInfo<IA64MachineFunctionInfo>()->setSRetReg(SRetReg);
  }
  return true;
}

/// The virtual register holding this function's GP, copied from r1 on
/// entry.
static Register getGlobalPointerSave(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (Register Reg = MRI.getLiveInVirtReg(IA64::R1))
    return Reg;
  Register Reg = MF.addLiveIn(IA64::R1, &IA64::GRSaveRegClass);
  MachineBasicBlock &Entry = MF.front();
  Entry.addLiveIn(IA64::R1);
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  BuildMI(Entry, Entry.begin(), DebugLoc(), TII.get(TargetOpcode::COPY), Reg)
      .addReg(IA64::R1);
  return Reg;
}

bool IA64CallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                 CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const IA64Subtarget &STI = MF.getSubtarget<IA64Subtarget>();
  const IA64RegisterInfo *TRI = STI.getRegisterInfo();
  const DataLayout &DL = F.getDataLayout();
  LLT P0 = LLT::pointer(0, 64);
  LLT S64 = LLT::scalar(64);

  // A call that has to be one in place of a return is not made one here,
  // and one that may unwind needs the global pointer put back where it
  // lands.
  if (Info.IsMustTailCall || (Info.CB && !isa<CallInst>(Info.CB)))
    return false;
  if (!Info.Callee.isReg() && !Info.Callee.isGlobal() &&
      !Info.Callee.isSymbol())
    return false;

  SmallVector<ArgInfo, 8> OutArgs;
  for (const ArgInfo &OrigArg : Info.OrigArgs)
    splitToValueTypes(OrigArg, OutArgs, DL, Info.CallConv);
  SmallVector<ArgInfo, 4> InArgs;
  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, InArgs, DL, Info.CallConv);
  if (!areSupported(OutArgs) || !areSupported(InArgs))
    return false;
  dropExtensions(InArgs);

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(Info.CallConv, Info.IsVarArg, MF, ArgLocs, F.getContext());
  OutgoingValueAssigner ArgAssigner(CC_IA64);
  SmallVector<CCValAssign, 8> SlotCopies;
  if (!determineAssignments(ArgAssigner, OutArgs, CCInfo) ||
      !takeSlotCopies(ArgLocs, SlotCopies))
    return false;
  unsigned NumBytes =
      IA64FrameLowering::ScratchAreaSize + CCInfo.getStackSize();

  // A function pointer is the address of its function descriptor: the entry
  // point followed by the callee's GP. Calling a non-function global treats
  // the global's contents as the descriptor.
  if (Info.Callee.isGlobal() &&
      (Info.Callee.getOffset() != 0 ||
       !Info.Callee.getGlobal()->getValueType()->isFunctionTy()))
    Info.Callee = MachineOperand::CreateReg(
        MIRBuilder.buildGlobalValue(P0, Info.Callee.getGlobal()).getReg(0),
        /*isDef=*/false);
  bool Indirect = Info.Callee.isReg();
  Register Entry, CalleeGP;
  if (Indirect) {
    Register Descriptor = Info.Callee.getReg();
    Entry =
        MIRBuilder.buildLoad(S64, Descriptor, MachinePointerInfo(), Align(8))
            .getReg(0);
    CalleeGP =
        MIRBuilder
            .buildLoad(S64,
                       MIRBuilder.buildPtrAdd(P0, Descriptor,
                                              MIRBuilder.buildConstant(S64, 8)),
                       MachinePointerInfo(), Align(8))
            .getReg(0);
  }

  // A DSO-local callee shares this function's GP. Any other callee may
  // return with its own GP in r1, unless the program has a single constant
  // GP and the call is direct.
  bool SameModule = !Indirect && STI.hasConstantGP();
  bool ReturnsTwice = Info.CB && Info.CB->hasFnAttr(Attribute::ReturnsTwice);
  if (Info.Callee.isGlobal()) {
    SameModule |= MF.getTarget().shouldAssumeDSOLocal(Info.Callee.getGlobal());
  } else if (Info.Callee.isSymbol()) {
    // A libcall refers to the module's function of that name, if any.
    const Function *Callee =
        F.getParent()->getFunction(Info.Callee.getSymbolName());
    ReturnsTwice = Callee && Callee->hasFnAttribute(Attribute::ReturnsTwice);
  }

  // The CALL pseudo restores the GP from its save-register operand, which
  // must therefore be preserved across the call. That cannot be relied on
  // for a returns_twice callee, so there the GP is restored by a separate
  // copy after the call.
  Register GP = IA64::R0, GPAfter;
  if (!SameModule) {
    if (ReturnsTwice)
      GPAfter = getGlobalPointerSave(MF);
    else
      GP = getGlobalPointerSave(MF);
  }
  if (ReturnsTwice)
    MF.setExposesReturnsTwice(true);

  MIRBuilder.buildInstr(IA64::ADJCALLSTACKDOWN).addImm(NumBytes).addImm(0);

  auto MIB =
      MIRBuilder.buildInstrNoInsert(Indirect ? IA64::CALL_IND : IA64::CALL);
  if (Indirect)
    MIB.addUse(Entry).addUse(CalleeGP);
  else
    MIB.add(Info.Callee);
  MIB.addUse(GP);

  IA64OutgoingValueHandler ArgHandler(MIRBuilder, MRI, MIB, /*IsCall=*/true);
  if (!handleAssignments(ArgHandler, OutArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;
  for (const CCValAssign &VA : SlotCopies)
    ArgHandler.assignValueToReg(OutArgs[VA.getValNo()].Regs[0], VA.getLocReg(),
                                VA);
  MIB.addRegMask(ReturnsTwice ? TRI->getReturnsTwiceCallPreservedMask()
                              : TRI->getCallPreservedMask(MF, Info.CallConv));
  MIRBuilder.insertInstr(MIB);
  if (Indirect)
    for (unsigned Op : {0, 1})
      MIB->getOperand(Op).setReg(constrainOperandRegClass(
          MF, *TRI, MRI, *STI.getInstrInfo(), *STI.getRegBankInfo(), *MIB,
          MIB->getDesc(), MIB->getOperand(Op), Op));
  if (Info.CB && Info.CB->hasFnAttr(Attribute::NoMerge))
    MIB->setFlag(MachineInstr::NoMerge);

  MIRBuilder.buildInstr(IA64::ADJCALLSTACKUP).addImm(NumBytes).addImm(0);
  if (GPAfter)
    MIRBuilder.buildCopy(Register(IA64::R1), GPAfter);

  if (!InArgs.empty()) {
    IncomingValueAssigner RetAssigner(RetCC_IA64);
    IA64CallReturnHandler RetHandler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(RetHandler, RetAssigner, InArgs,
                                       MIRBuilder, Info.CallConv,
                                       Info.IsVarArg))
      return false;
  }
  // Load a result demoted to memory from the sret slot.
  if (!Info.CanLowerReturn)
    insertSRetLoads(MIRBuilder, Info.OrigRet.Ty, Info.OrigRet.Regs,
                    Info.DemoteRegister, Info.DemoteStackIndex);
  return true;
}

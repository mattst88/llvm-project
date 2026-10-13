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
    int FI =
        MF.getFrameInfo().CreateFixedObject(Size, Offset, /*IsImmutable=*/true);
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

  /// A by-value aggregate is accessed through its address, so it must be
  /// contiguous in memory. The parts passed in registers are stored just
  /// below the part passed on the stack, which starts at the caller's frame,
  /// or to a local stack object if the whole aggregate came in registers.
  unsigned assignByVal(CallLowering::ArgInfo &Arg, ArrayRef<CCValAssign> VAs) {
    MachineFunction &MF = MIRBuilder.getMF();
    MachineFrameInfo &MFI = MF.getFrameInfo();
    const ISD::ArgFlagsTy &Flags = Arg.Flags[0];
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);
    unsigned ValNo = VAs[0].getValNo();
    unsigned NumRegs = 0;
    while (NumRegs != VAs.size() && VAs[NumRegs].isRegLoc() &&
           VAs[NumRegs].getValNo() == ValNo)
      ++NumRegs;
    bool HasMem = NumRegs != VAs.size() && VAs[NumRegs].getValNo() == ValNo;
    // The function accesses the object through its address, so it is not
    // immutable.
    unsigned Size = std::max(Flags.getByValSize(), 1u);
    int FI;
    if (NumRegs == 0)
      FI = MFI.CreateFixedObject(Size, VAs[0].getLocMemOffset(), false, true);
    else if (HasMem)
      FI = MFI.CreateFixedObject(Size, -8 * int64_t(NumRegs), false, true);
    else
      FI = MFI.CreateStackObject(
          alignTo(Size, 8), std::max(Flags.getNonZeroByValAlign(), Align(8)),
          false);
    MIRBuilder.buildFrameIndex(Arg.Regs[0], FI);
    for (unsigned Slot = 0; Slot != NumRegs; ++Slot) {
      MCRegister Reg = VAs[Slot].getLocReg();
      markPhysRegUsed(Reg);
      // Build these in separate statements: argument evaluation order is
      // unspecified in C++, and the output must not depend on the host
      // compiler.
      auto Addr = MIRBuilder.buildPtrAdd(
          P0, Arg.Regs[0], MIRBuilder.buildConstant(S64, 8 * Slot));
      auto Value = MIRBuilder.buildCopy(S64, Register(Reg));
      MIRBuilder.buildStore(Value, Addr,
                            MachinePointerInfo::getFixedStack(MF, FI, 8 * Slot),
                            Align(8));
    }
    return NumRegs + HasMem;
  }

  /// Custom locations: an FP argument that ran out of FP registers and is
  /// passed as its bits in one or more slots, and a by-value aggregate.
  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk = nullptr) override {
    const CCValAssign &VA = VAs[0];
    if (Arg.Flags[0].isByVal())
      return assignByVal(Arg, VAs);

    MachineFunction &MF = MIRBuilder.getMF();
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);
    auto Slot = [&](const CCValAssign &VA) {
      if (VA.isMemLoc()) {
        MachinePointerInfo MPO;
        Register Addr =
            getStackAddress(8, VA.getLocMemOffset(), MPO, Arg.Flags[0]);
        return MIRBuilder.buildLoad(S64, Addr, MPO, Align(8)).getReg(0);
      }
      markPhysRegUsed(VA.getLocReg());
      return MIRBuilder.buildCopy(S64, Register(VA.getLocReg())).getReg(0);
    };

    // An f80 occupies two slots; store them and reload as f80.
    if (VA.getValVT() == MVT::f80) {
      if (VAs.size() < 2 || VAs[1].getValNo() != VA.getValNo())
        return 0;
      int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
      auto Temp = MIRBuilder.buildFrameIndex(P0, FI);
      MIRBuilder.buildStore(
          Slot(VA), Temp, MachinePointerInfo::getFixedStack(MF, FI), Align(16));
      auto HighAddr =
          MIRBuilder.buildPtrAdd(P0, Temp, MIRBuilder.buildConstant(S64, 8));
      Register High = Slot(VAs[1]);
      MIRBuilder.buildStore(High, HighAddr,
                            MachinePointerInfo::getFixedStack(MF, FI, 8),
                            Align(8));
      MIRBuilder.buildLoad(Arg.Regs[0], Temp,
                           MachinePointerInfo::getFixedStack(MF, FI),
                           Align(16));
      return 2;
    }

    // Two float members of an aggregate may share one slot.
    Register Bits = Slot(VA);
    if (VA.getLocInfo() == CCValAssign::AExtUpper)
      Bits = MIRBuilder.buildLShr(S64, Bits, MIRBuilder.buildConstant(S64, 32))
                 .getReg(0);
    if (VA.getValVT() == MVT::f32)
      MIRBuilder.buildTrunc(Arg.Regs[0], Bits);
    else
      MIRBuilder.buildCopy(Arg.Regs[0], Bits);
    return 1;
  }

  virtual void markPhysRegUsed(MCRegister PhysReg) {
    MachineBasicBlock &MBB = MIRBuilder.getMBB();
    if (MBB.isLiveIn(PhysReg))
      return;
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MBB.addLiveIn(PhysReg);
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
    if (VA.getLocInfo() != CCValAssign::BCvt &&
        VA.getLocInfo() != CCValAssign::AExtUpper)
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
    MIRBuilder.buildStore(
        Slot, Addr,
        *MF.getMachineMemOperand(MPO, MachineMemOperand::MOStore, MemTy,
                                 commonAlignment(Align(16), MPO.Offset)));
  }

  /// Custom locations: an FP argument that ran out of FP registers and is
  /// passed as its bits in one or more slots, and a by-value aggregate, whose
  /// slots were loaded before the call sequence.
  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk = nullptr) override {
    const CCValAssign &VA = VAs[0];
    MachineFunction &MF = MIRBuilder.getMF();
    LLT P0 = LLT::pointer(0, 64);
    LLT S64 = LLT::scalar(64);

    if (Arg.Flags[0].isByVal()) {
      ArrayRef<Register> Read = ByValSlots.lookup(VA.getValNo());
      unsigned N = 0;
      for (; N != VAs.size() && VAs[N].isRegLoc() &&
             VAs[N].getValNo() == VA.getValNo();
           ++N)
        assignValueToReg(Read[N], VAs[N].getLocReg(), VAs[N]);
      if (N != VAs.size() && VAs[N].getValNo() == VA.getValNo())
        ++N;
      return N;
    }

    // An f80 occupies two slots; store it and reload it as two i64s.
    if (VA.getValVT() == MVT::f80) {
      if (VAs.size() < 2 || VAs[1].getValNo() != VA.getValNo())
        return 0;
      int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
      auto Temp = MIRBuilder.buildFrameIndex(P0, FI);
      MIRBuilder.buildStore(Arg.Regs[0], Temp,
                            MachinePointerInfo::getFixedStack(MF, FI),
                            Align(16));
      auto Lo = MIRBuilder.buildLoad(
          S64, Temp, MachinePointerInfo::getFixedStack(MF, FI), Align(16));
      auto Hi = MIRBuilder.buildLoad(
          S64,
          MIRBuilder.buildPtrAdd(P0, Temp, MIRBuilder.buildConstant(S64, 8)),
          MachinePointerInfo::getFixedStack(MF, FI, 8), Align(8));
      assignValueToReg(Lo.getReg(0), VA.getLocReg(), VA);
      if (VAs[1].isRegLoc()) {
        assignValueToReg(Hi.getReg(0), VAs[1].getLocReg(), VAs[1]);
      } else {
        MachinePointerInfo MPO;
        Register Addr =
            getStackAddress(8, VAs[1].getLocMemOffset(), MPO, Arg.Flags[0]);
        MIRBuilder.buildStore(Hi, Addr, MPO, Align(8));
      }
      return 2;
    }

    if (!VA.isRegLoc())
      return 0;
    // Two float members of an aggregate may share one slot, so the low half
    // is deferred until it is known whether a high half follows.
    if (VA.getValVT() == MVT::f32 && VA.getLocInfo() == CCValAssign::BCvt) {
      LowHalves.push_back({Arg.Regs[0], VA});
      return 1;
    }
    if (VA.getLocInfo() != CCValAssign::AExtUpper) {
      assignValueToReg(Arg.Regs[0], VA.getLocReg(), VA);
      return 1;
    }
    auto ThirtyTwo = MIRBuilder.buildConstant(S64, 32);
    auto Wide = MIRBuilder.buildAnyExt(S64, Arg.Regs[0]);
    Register Slot = MIRBuilder.buildShl(S64, Wide, ThirtyTwo).getReg(0);
    auto *Low = find_if(LowHalves, [&](const auto &Half) {
      return Half.second.getLocReg() == VA.getLocReg();
    });
    if (Low != LowHalves.end()) {
      Slot =
          MIRBuilder.buildOr(S64, MIRBuilder.buildZExt(S64, Low->first), Slot)
              .getReg(0);
      LowHalves.erase(Low);
    }
    assignValueToReg(Slot, VA.getLocReg(), VA);
    return 1;
  }

  /// Emit the low halves that had no matching high half.
  void finish() {
    for (const auto &[Reg, VA] : LowHalves)
      assignValueToReg(Reg, VA.getLocReg(), VA);
    LowHalves.clear();
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
  SmallVector<std::pair<Register, CCValAssign>, 4> LowHalves;
  /// The loaded register slots of each by-value argument, by value number.
  DenseMap<unsigned, SmallVector<Register, 8>> ByValSlots;
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
      if (Flags.isByVal()) {
        Kept.setByVal();
        Kept.setByValSize(Flags.getByValSize());
      }
      Kept.setInConsecutiveRegs(Flags.isInConsecutiveRegs());
      Kept.setInConsecutiveRegsLast(Flags.isInConsecutiveRegsLast());
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

/// Whether every argument has a supported type: integers, pointers, and the
/// three FP types with hardware support.
static bool areSupported(ArrayRef<CallLowering::ArgInfo> Args) {
  return all_of(Args, [](const CallLowering::ArgInfo &Arg) {
    const ISD::ArgFlagsTy &Flags = Arg.Flags[0];
    if (Flags.isByRef() || Flags.isInAlloca() || Flags.isPreallocated() ||
        Flags.isSwiftSelf() || Flags.isSwiftAsync() || Flags.isSwiftError())
      return false;
    if (Arg.Ty->isX86_FP80Ty())
      return true;
    // A vector is passed as its individual elements, except <2 x float>,
    // which is passed as the significand of an FP register.
    const Type *Ty = Arg.Ty;
    if (const auto *VTy = dyn_cast<FixedVectorType>(Ty)) {
      Ty = VTy->getElementType();
      if (Ty->isFloatTy() && VTy->getNumElements() == 2)
        return false;
    }
    return Ty->isIntegerTy() || Ty->isPointerTy() || Ty->isFloatTy() ||
           Ty->isDoubleTy();
  });
}

/// FP arguments to a variadic callee are passed both in an FP register and in
/// their GR slot, since the callee may or may not have named them. Move the
/// slot locations after the register (two for f80) out of \p ArgLocs, which
/// must have one location per register value, into \p SlotCopies.
static void takeSlotCopies(SmallVectorImpl<CCValAssign> &ArgLocs,
                           SmallVectorImpl<CCValAssign> &SlotCopies) {
  for (unsigned I = 1; I < ArgLocs.size();) {
    const CCValAssign &VA = ArgLocs[I];
    if (VA.needsCustom() && !ArgLocs[I - 1].needsCustom() &&
        ArgLocs[I - 1].getValNo() == VA.getValNo()) {
      SlotCopies.push_back(VA);
      ArgLocs.erase(ArgLocs.begin() + I);
      continue;
    }
    ++I;
  }
}

/// Whether any vector element has a custom location. Custom locations are
/// handled per value, and an element is not a separate value until the
/// vector has been unmerged.
static bool hasVectorInCustomPlace(ArrayRef<CallLowering::ArgInfo> Args,
                                   ArrayRef<CCValAssign> ArgLocs) {
  return any_of(ArgLocs, [&](const CCValAssign &VA) {
    return VA.needsCustom() && Args[VA.getValNo()].Ty->isVectorTy();
  });
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
  if (!determineAssignments(Assigner, SplitArgs, CCInfo))
    return false;
  if (hasVectorInCustomPlace(SplitArgs, ArgLocs))
    return false;
  takeSlotCopies(ArgLocs, SlotCopies);
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

  // Invokes are lowered like ordinary calls: the instruction selector
  // restores the GP at the landing pad.
  if (Info.CB && isa<CallBrInst>(Info.CB))
    return false;
  // Operand bundles are not handled here; leave them to SelectionDAG, which
  // handles or rejects them.
  if (Info.CB && Info.CB->hasOperandBundles())
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
  if (!determineAssignments(ArgAssigner, OutArgs, CCInfo))
    return false;
  if (hasVectorInCustomPlace(OutArgs, ArgLocs))
    return false;
  takeSlotCopies(ArgLocs, SlotCopies);
  unsigned NumBytes =
      IA64FrameLowering::ScratchAreaSize + CCInfo.getStackSize();

  // A tail call requires the callee to see the same state this function was
  // entered with: the same GP, which holds for a DSO-local callee, and no
  // stack arguments, so only register arguments are allowed.
  bool TailCall =
      Info.IsTailCall && Info.CanLowerReturn && Info.Callee.isGlobal() &&
      Info.Callee.getOffset() == 0 &&
      Info.Callee.getGlobal()->getValueType()->isFunctionTy() &&
      MF.getTarget().shouldAssumeDSOLocal(Info.Callee.getGlobal()) &&
      CCInfo.getStackSize() == 0 && SlotCopies.empty() && !F.isVarArg() &&
      !F.hasFnAttribute("syscall-linkage") &&
      F.getCallingConv() == Info.CallConv &&
      !(Info.CB && Info.CB->hasFnAttr(Attribute::ReturnsTwice)) &&
      none_of(OutArgs,
              [](const ArgInfo &Arg) { return Arg.Flags[0].isByVal(); });
  // Leave an unsatisfiable musttail for SelectionDAG to diagnose.
  if (Info.IsMustTailCall && !TailCall)
    return false;
  if (TailCall) {
    // The callee receives its arguments in this function's input registers,
    // and the register frame is resized to the callee's input count.
    auto MIB = MIRBuilder.buildInstrNoInsert(IA64::TCRETURN)
                   .add(Info.Callee)
                   .addImm(0);
    IA64OutgoingValueHandler ArgHandler(MIRBuilder, MRI, MIB,
                                        /*IsCall=*/false);
    if (!handleAssignments(ArgHandler, OutArgs, CCInfo, ArgLocs, MIRBuilder))
      return false;
    ArgHandler.finish();
    unsigned Inputs = 0;
    for (const CCValAssign &VA : ArgLocs)
      if (VA.isRegLoc() && VA.getLocReg() >= IA64::R32 &&
          VA.getLocReg() <= IA64::R39)
        Inputs = std::max(Inputs, unsigned(VA.getLocReg() - IA64::R32 + 1));
    MIB->getOperand(1).setImm(Inputs);
    MIRBuilder.insertInstr(MIB);
    if (Info.CB && Info.CB->hasFnAttr(Attribute::NoMerge))
      MIB->setFlag(MachineInstr::NoMerge);
    MF.getFrameInfo().setHasTailCall();
    Info.LoweredTailCall = true;
    return true;
  }

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

  auto MIB =
      MIRBuilder.buildInstrNoInsert(Indirect ? IA64::CALL_IND : IA64::CALL);
  if (Indirect)
    MIB.addUse(Entry).addUse(CalleeGP);
  else
    MIB.add(Info.Callee);
  MIB.addUse(GP);
  IA64OutgoingValueHandler ArgHandler(MIRBuilder, MRI, MIB, /*IsCall=*/true);

  // By-value arguments are loaded one register slot at a time, and the
  // stack part is copied with memcpy. This is done before ADJCALLSTACKDOWN,
  // since the memcpy may itself be a call, and nothing moves sp in
  // between.
  for (unsigned I = 0, E = ArgLocs.size(); I != E;) {
    unsigned ValNo = ArgLocs[I].getValNo();
    const ISD::ArgFlagsTy &Flags = OutArgs[ValNo].Flags[0];
    if (!Flags.isByVal()) {
      ++I;
      continue;
    }
    Register From = OutArgs[ValNo].Regs[0];
    unsigned Size = Flags.getByValSize();
    Align Alignment = Flags.getNonZeroByValAlign();
    auto At = [&](unsigned Offset) {
      return MIRBuilder.buildPtrAdd(P0, From,
                                    MIRBuilder.buildConstant(S64, Offset));
    };
    SmallVectorImpl<Register> &Read = ArgHandler.ByValSlots[ValNo];
    unsigned Offset = 0;
    for (; I != E && ArgLocs[I].getValNo() == ValNo && ArgLocs[I].isRegLoc();
         ++I, Offset += 8) {
      // The last slot may be partial, and the bytes past the end of the
      // argument must not be read.
      Register Slot;
      unsigned Bytes = std::min(Size - Offset, 8u);
      for (unsigned Width : {8, 4, 2, 1}) {
        if (Bytes < Width)
          continue;
        unsigned Byte = Offset + std::min(Size - Offset, 8u) - Bytes;
        Register Piece =
            MIRBuilder
                .buildLoadInstr(
                    Width == 8 ? TargetOpcode::G_LOAD
                               : TargetOpcode::G_ZEXTLOAD,
                    S64, At(Byte),
                    *MF.getMachineMemOperand(MachinePointerInfo(),
                                             MachineMemOperand::MOLoad,
                                             LLT::scalar(8 * Width),
                                             commonAlignment(Alignment, Byte)))
                .getReg(0);
        if (Byte != Offset)
          Piece =
              MIRBuilder
                  .buildShl(S64, Piece,
                            MIRBuilder.buildConstant(S64, 8 * (Byte - Offset)))
                  .getReg(0);
        Slot = Slot ? MIRBuilder.buildOr(S64, Slot, Piece).getReg(0) : Piece;
        Bytes -= Width;
      }
      Read.push_back(Slot);
    }
    if (I == E || ArgLocs[I].getValNo() != ValNo)
      continue;
    unsigned To =
        IA64FrameLowering::ScratchAreaSize + ArgLocs[I].getLocMemOffset();
    if (Size != Offset) {
      auto Length = MIRBuilder.buildConstant(S64, Size - Offset);
      auto Source = At(Offset);
      auto ToOffset = MIRBuilder.buildConstant(S64, To);
      auto StackPtr = MIRBuilder.buildCopy(P0, Register(IA64::R12));
      MIRBuilder.buildMemCpy(
          MIRBuilder.buildPtrAdd(P0, StackPtr, ToOffset), Source, Length,
          *MF.getMachineMemOperand(MachinePointerInfo::getStack(MF, To),
                                   MachineMemOperand::MOStore, LLT::scalar(8),
                                   Align(8)),
          *MF.getMachineMemOperand(MachinePointerInfo(),
                                   MachineMemOperand::MOLoad, LLT::scalar(8),
                                   commonAlignment(Alignment, Offset)));
    }
    ++I;
  }

  MIRBuilder.buildInstr(IA64::ADJCALLSTACKDOWN).addImm(NumBytes).addImm(0);

  if (!handleAssignments(ArgHandler, OutArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;
  for (unsigned I = 0, E = SlotCopies.size(); I != E;) {
    unsigned N = ArgHandler.assignCustomValue(OutArgs[SlotCopies[I].getValNo()],
                                              ArrayRef(SlotCopies).slice(I));
    if (N == 0)
      return false;
    I += N;
  }
  ArgHandler.finish();
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

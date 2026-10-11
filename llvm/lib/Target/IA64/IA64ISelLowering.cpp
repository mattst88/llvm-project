//===-- IA64ISelLowering.cpp - IA64 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ISelLowering.h"
#include "IA64.h"
#include "IA64FrameLowering.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64SelectionDAGInfo.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "IA64TargetObjectFile.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static const MCPhysReg Slots[] = {IA64::R32, IA64::R33, IA64::R34, IA64::R35,
                                  IA64::R36, IA64::R37, IA64::R38, IA64::R39};

// Argument slots are numbered across the registers and on into the stack,
// whose first slot is 16-byte aligned. Skip the next slot if it is odd, so a
// 16-byte-aligned argument starts at an even slot. The argument itself is
// not allocated here.
static bool CC_IA64_SkipOddSlot(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                CCValAssign::LocInfo &LocInfo,
                                ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (ArgFlags.getNonZeroOrigAlign() < Align(16))
    return false;
  unsigned Slot = State.getFirstUnallocated(Slots);
  if (Slot == std::size(Slots))
    Slot += State.getStackSize() / 8;
  if (Slot % 2 != 0 && !State.AllocateReg(Slots))
    State.AllocateStack(8, Align(8));
  return false;
}

static bool CC_IA64_FP(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                       CCValAssign::LocInfo &LocInfo, ISD::ArgFlagsTy &ArgFlags,
                       CCState &State) {
  static const MCPhysReg FRs[] = {IA64::F8,  IA64::F9,  IA64::F10, IA64::F11,
                                  IA64::F12, IA64::F13, IA64::F14, IA64::F15};
  // Past the eighth slot, FP arguments go on the stack even if FP registers
  // remain.
  if (!State.AllocateReg(Slots))
    return false;
  // Each takes at least one slot, so the registers outlast the slots.
  MCRegister Reg = State.AllocateReg(FRs);
  assert(Reg && "more floating-point arguments in registers than slots");
  State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  return true;
}

#define GET_CALLING_CONV_IMPL
#include "IA64GenCallingConv.inc"

IA64TargetLowering::IA64TargetLowering(const IA64TargetMachine &TM,
                                       const IA64Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes.
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f32, &IA64::FRRegClass);
  addRegisterClass(MVT::f64, &IA64::FRRegClass);

  setStackPointerRegisterToSaveRestore(IA64::R12);

  // Functions start on a 16-byte bundle boundary.
  setMinFunctionAlignment(Align(16));

  computeRegisterProperties(STI.getRegisterInfo());

  // FP registers hold every precision in a single format, so an extending
  // load needs no conversion. A truncating store does not round, though.
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // f32 and f64 constants are materialized in a GR and moved to an FR.
  setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);

  // There is no instruction that divides or takes a square root, only ones
  // that start an approximation of the reciprocal.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    for (unsigned Opc : {ISD::FDIV, ISD::FREM, ISD::FSQRT})
      setOperationAction(Opc, VT, LibCall);
    setOperationAction(ISD::FSINCOS, VT, Expand);
  }

  // The bits of a single-precision value are half a general register.
  setOperationAction(ISD::BITCAST, MVT::i32, Custom);

  // Compares write predicates; materialized as a value, the result is 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);

  // Selects are predicated on the compare itself, so a select on a value
  // becomes a select on its comparison with zero.
  for (MVT VT : {MVT::i64, MVT::f32, MVT::f64})
    setOperationAction(ISD::SELECT, VT, Expand);

  // No single compare implements ordered-not-equal or unordered-or-equal.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setCondCodeAction(ISD::SETONE, VT, Expand);
    setCondCodeAction(ISD::SETUEQ, VT, Expand);
  }
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // va_list is a pointer to the next argument; each argument occupies one or
  // more 8-byte slots.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  for (unsigned Opc : {ISD::VAARG, ISD::VACOPY, ISD::VAEND})
    setOperationAction(Opc, MVT::Other, Expand);
  setMinStackArgumentAlignment(Align(8));

  setOperationAction(ISD::RETURNADDR, MVT::i64, Custom);
  setOperationAction(ISD::FRAMEADDR, MVT::i64, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::JumpTable, MVT::i64, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i64, Custom);

  // A jump table branch loads the entry and branches through a BR.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // popcnt exists, and ctlz and cttz are expanded using it. clz exists only
  // on the latest processors.
  for (unsigned Opc :
       {ISD::CTLZ, ISD::CTTZ, ISD::BITREVERSE, ISD::ROTL, ISD::ROTR})
    setOperationAction(Opc, MVT::i64, Expand);

  // Expand double-register shifts inline instead of calling a libcall.
  for (unsigned Opc : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Opc, MVT::i64, Custom);

  // MUL_LOHI expands to two multiplies, one for each half.
  // Atomics wider than 64 bits are libcalls.
  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(8);

  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);

  // There is no integer divide instruction; division is a libcall.
  for (unsigned Opc :
       {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM, ISD::SDIVREM, ISD::UDIVREM})
    setOperationAction(Opc, MVT::i64, Expand);

  // No instruction sign-extends from i1; it takes two shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // No load sign-extends, and an i1 is a byte in memory.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    for (MVT MemVT : {MVT::i8, MVT::i16, MVT::i32})
      setLoadExtAction(ISD::SEXTLOAD, VT, MemVT, Expand);
  }
}

SDValue IA64TargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::BITCAST:
    // i32 to f32, while i32 is not yet legalized.
    if (Op.getValueType() != MVT::f32 ||
        Op.getOperand(0).getValueType() != MVT::i32)
      return SDValue();
    return DAG.getNode(
        IA64ISD::SETF_S, SDLoc(Op), MVT::f32,
        DAG.getNode(ISD::ANY_EXTEND, SDLoc(Op), MVT::i64, Op.getOperand(0)));
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::SHL_PARTS:
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS: {
    SDValue Lo, Hi;
    expandShiftParts(Op.getNode(), Lo, Hi, DAG);
    return DAG.getMergeValues({Lo, Hi}, SDLoc(Op));
  }
  default:
    llvm_unreachable("an operation with no custom lowering");
  }
}

void IA64TargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case ISD::BITCAST: {
    // f32 to i32.
    SDValue Src = N->getOperand(0);
    if (N->getValueType(0) != MVT::i32 || Src.getValueType() != MVT::f32)
      return;
    SDValue Bits = DAG.getNode(IA64ISD::GETF_S, DL, MVT::i64, Src);
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Bits));
    return;
  }
  default:
    llvm_unreachable("a result with no custom lowering");
  }
}

// The address of a symbol that may be preemptible is loaded from the linkage
// table (GOT), addressed via GP. A function's entry holds the address of its
// canonical function descriptor, which is the function pointer.
//
// A DSO-local symbol is at a link-time constant offset from GP, so no load
// is needed.
SDValue IA64TargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  const TargetMachine &TM = getTargetMachine();
  int64_t Offset = N->getOffset();
  SDLoc DL(Op);

  auto WithOffset = [&](SDValue Addr, int64_t Offset) {
    if (Offset == 0)
      return Addr;
    return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                       DAG.getConstant(Offset, DL, MVT::i64));
  };

  // An undefined weak symbol is not at a fixed offset from GP.
  if (GV->getValueType()->isFunctionTy() || !TM.shouldAssumeDSOLocal(GV) ||
      GV->hasExternalWeakLinkage()) {
    unsigned Flags =
        GV->getValueType()->isFunctionTy() ? IA64::S_LTOFF_FPTR : IA64::S_LTOFF;
    return WithOffset(
        loadLinkageTableEntry(
            DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags), DL, DAG),
        Offset);
  }

  // The folded offset must keep the address in reach of the 22-bit add,
  // which holds if it stays inside the object.
  const auto &TLOF =
      static_cast<const IA64TargetObjectFile &>(*TM.getObjFileLowering());
  if (TLOF.isShortData(GV, TM)) {
    int64_t InObject =
        Offset >= 0 && uint64_t(Offset) < IA64TargetObjectFile::ShortDataLimit
            ? Offset
            : 0;
    return WithOffset(
        DAG.getNode(IA64ISD::GPREL22, DL, MVT::i64,
                    DAG.getTargetGlobalAddress(GV, DL, MVT::i64, InObject,
                                               IA64::S_GPREL)),
        Offset - InObject);
  }
  return DAG.getNode(
      IA64ISD::GPREL64, DL, MVT::i64,
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, Offset, IA64::S_GPREL));
}

// From the executable, a TLS variable is at an offset from the thread
// pointer: a link-time constant if the executable defines it (local-exec),
// or loaded from a GOT entry the dynamic loader fills in (initial-exec).
// Otherwise its module ID and DTP-relative offset are passed to
// __tls_get_addr (general-dynamic); for a symbol in this module, the
// link-time constant offset is added to the result of __tls_get_addr for
// offset 0 (local-dynamic).
SDValue IA64TargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc DL(Op);

  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(N, DAG);

  auto Sym = [&](unsigned Flags) {
    return DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags);
  };
  auto ThreadPointer = [&] {
    return DAG.getCopyFromReg(DAG.getEntryNode(), DL, IA64::R13, MVT::i64);
  };
  auto GetAddr = [&](SDValue Module, SDValue Offset) {
    ArgListTy Args;
    Type *Ty = Type::getInt64Ty(*DAG.getContext());
    Args.emplace_back(Module, Ty);
    Args.emplace_back(Offset, Ty);
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(DAG.getEntryNode())
        .setLibCallee(CallingConv::C, PointerType::getUnqual(*DAG.getContext()),
                      DAG.getExternalSymbol("__tls_get_addr", MVT::i64),
                      std::move(Args));
    return LowerCallTo(CLI).first;
  };

  SDValue Addr;
  switch (getTargetMachine().getTLSModel(GV)) {
  case TLSModel::LocalExec:
    Addr = DAG.getNode(IA64ISD::TLSOFF, DL, MVT::i64, Sym(IA64::S_TPREL),
                       ThreadPointer());
    break;
  case TLSModel::InitialExec:
    Addr = DAG.getNode(ISD::ADD, DL, MVT::i64,
                       loadLinkageTableEntry(Sym(IA64::S_LTOFF_TPREL), DL, DAG),
                       ThreadPointer());
    break;
  case TLSModel::LocalDynamic:
    Addr = DAG.getNode(
        IA64ISD::TLSOFF, DL, MVT::i64, Sym(IA64::S_DTPREL),
        GetAddr(loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPMOD), DL, DAG),
                DAG.getConstant(0, DL, MVT::i64)));
    break;
  case TLSModel::GeneralDynamic:
    Addr = GetAddr(loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPMOD), DL, DAG),
                   loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPREL), DL, DAG));
    break;
  }
  if (N->getOffset() == 0)
    return Addr;
  return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                     DAG.getConstant(N->getOffset(), DL, MVT::i64));
}

SDValue IA64TargetLowering::loadLinkageTableEntry(SDValue Sym, const SDLoc &DL,
                                                  SelectionDAG &DAG) const {
  SDValue Entry = DAG.getNode(IA64ISD::LTOFF, DL, MVT::i64, Sym);
  return DAG.getLoad(
      MVT::i64, DL, DAG.getEntryNode(), Entry,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), Align(8),
      MachineMemOperand::MODereferenceable | MachineMemOperand::MOInvariant);
}

SDValue IA64TargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  const auto *N = cast<JumpTableSDNode>(Op);
  return loadLinkageTableEntry(
      DAG.getTargetJumpTable(N->getIndex(), MVT::i64, IA64::S_LTOFF), SDLoc(Op),
      DAG);
}

SDValue IA64TargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  const auto *N = cast<BlockAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue Addr = loadLinkageTableEntry(
      DAG.getTargetBlockAddress(N->getBlockAddress(), MVT::i64, 0,
                                IA64::S_LTOFF),
      DL, DAG);
  if (N->getOffset() == 0)
    return Addr;
  return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                     DAG.getConstant(N->getOffset(), DL, MVT::i64));
}

// va_list is the address of the next argument.
SDValue IA64TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), SDLoc(Op),
                      DAG.getFrameIndex(FI->getVarArgsFrameIndex(), MVT::i64),
                      Op.getOperand(1), MachinePointerInfo(SV));
}

// The return link is in b0 as the function is entered. Only an unwinder can
// say what that of a caller was.
SDValue IA64TargetLowering::LowerRETURNADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MF.getFrameInfo().setReturnAddressIsTaken(true);
  SDLoc DL(Op);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, MVT::i64);
  Register Reg = MF.addLiveIn(IA64::B0, &IA64::GRRegClass);
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, MVT::i64);
}

// The frame address is SP on entry, where the caller's frame ends.
SDValue IA64TargetLowering::LowerFRAMEADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, MVT::i64);
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);
  return DAG.getNode(IA64ISD::FRAMEADDR, DL, MVT::i64);
}

// SP moves down by the size, which is already a multiple of 16. The outgoing
// argument area stays at SP, so the allocation starts above it and is
// aligned from there.
SDValue IA64TargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  uint64_t Alignment = Op.getConstantOperandVal(2);

  if (Alignment > 16)
    Size = DAG.getNode(ISD::ADD, DL, MVT::i64, Size,
                       DAG.getConstant(Alignment - 16, DL, MVT::i64));
  SDValue SP = DAG.getCopyFromReg(Chain, DL, IA64::R12, MVT::i64);
  Chain = DAG.getCopyToReg(SP.getValue(1), DL, IA64::R12,
                           DAG.getNode(ISD::SUB, DL, MVT::i64, SP, Size));
  SDValue Addr = DAG.getNode(IA64ISD::ADJDYNALLOC, DL,
                             DAG.getVTList(MVT::i64, MVT::Other), Chain);
  Chain = Addr.getValue(1);
  if (Alignment > 16)
    Addr =
        DAG.getNode(ISD::AND, DL, MVT::i64,
                    DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                                DAG.getConstant(Alignment - 1, DL, MVT::i64)),
                    DAG.getSignedConstant(-int64_t(Alignment), DL, MVT::i64));
  return DAG.getMergeValues({Addr, Chain}, DL);
}

// Jump table entries are table-relative offsets, which need no dynamic
// relocation.
// A symbol offset is folded into the relocation where the address is formed,
// and added in code where the address is loaded from the GOT.
bool IA64TargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  return true;
}

unsigned IA64TargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_LabelDifference32;
}

SDValue IA64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_IA64);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (const CCValAssign &VA : ArgLocs) {
    if (VA.isMemLoc()) {
      // Offsets count from the end of the scratch area, where the first
      // stack argument is.
      int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), true);
      InVals.push_back(DAG.getLoad(VA.getLocVT(), DL, Chain,
                                   DAG.getFrameIndex(FI, MVT::i64),
                                   MachinePointerInfo::getFixedStack(MF, FI)));
      continue;
    }
    // The ABI leaves the bits above a value narrower than its slot undefined,
    // so emit no AssertZext/AssertSext whatever the zeroext/signext
    // attributes say: a caller built by another compiler may not extend.
    Register VReg = MF.addLiveIn(VA.getLocReg(), VA.getLocVT().isFloatingPoint()
                                                     ? &IA64::FRRegClass
                                                     : &IA64::GRRegClass);
    InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT()));
  }
  if (!IsVarArg)
    return Chain;

  // The arguments with no names are read from memory, one slot after
  // another, so those that came in registers are written below the ones
  // that did not. The last two of them fit in the scratch area and the
  // frame has the rest at its top.
  unsigned First = CCInfo.getFirstUnallocated(Slots);
  auto SlotOffset = [](unsigned Slot) { return -8 * int64_t(8 - Slot); };
  SmallVector<SDValue, 8> Stores;
  for (unsigned Slot = First; Slot != 8; ++Slot) {
    int FI = MFI.CreateFixedObject(8, SlotOffset(Slot), true);
    Register VReg = MF.addLiveIn(Slots[Slot], &IA64::GRRegClass);
    Stores.push_back(
        DAG.getStore(Chain, DL, DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64),
                     DAG.getFrameIndex(FI, MVT::i64),
                     MachinePointerInfo::getFixedStack(MF, FI)));
  }
  if (!Stores.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
  MF.getInfo<IA64MachineFunctionInfo>()->setVarArgsFrameIndex(
      MFI.CreateFixedObject(
          8, First != 8 ? SlotOffset(First) : CCInfo.getStackSize(), true));
  return Chain;
}

// The arguments go in the output registers, which the callee finds as its
// inputs, and the sixteen bytes at the stack pointer are the callee's to
// scribble on.
SDValue IA64TargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  const SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();

  CLI.IsTailCall = false;

  // A pointer to a function is the address of its descriptor: the entry
  // point, and then the global pointer that the function expects.
  bool Indirect =
      !isa<GlobalAddressSDNode>(Callee) && !isa<ExternalSymbolSDNode>(Callee);
  SDValue Entry, CalleeGP;
  if (Indirect) {
    Entry = DAG.getLoad(MVT::i64, DL, Chain, Callee, MachinePointerInfo());
    CalleeGP =
        DAG.getLoad(MVT::i64, DL, Chain,
                    DAG.getMemBasePlusOffset(Callee, TypeSize::getFixed(8), DL),
                    MachinePointerInfo());
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Entry.getValue(1),
                        CalleeGP.getValue(1));
  }

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_IA64);

  unsigned NumBytes =
      IA64FrameLowering::ScratchAreaSize + CCInfo.getStackSize();
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // An argument the callee has not named goes as its bits, which for a
  // single-precision value are the low half of the slot.
  SmallVector<SDValue, 8> OutVals(CLI.OutVals.begin(), CLI.OutVals.end());
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    if (ArgLocs[I].getLocInfo() != CCValAssign::BCvt)
      continue;
    SDValue &Val = OutVals[I];
    if (Val.getValueType() == MVT::f32)
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64,
                        DAG.getBitcast(MVT::i32, Val));
    else
      Val = DAG.getBitcast(MVT::i64, Val);
  }

  // Store stack arguments first, so the copies to the output registers are
  // adjacent to the call.
  SmallVector<SDValue, 8> MemOps;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (!VA.isMemLoc())
      continue;
    if (!StackPtr)
      StackPtr = DAG.getCopyFromReg(Chain, DL, IA64::R12, MVT::i64);
    unsigned Offset = IA64FrameLowering::ScratchAreaSize + VA.getLocMemOffset();
    MemOps.push_back(DAG.getStore(
        Chain, DL, OutVals[I],
        DAG.getMemBasePlusOffset(StackPtr, TypeSize::getFixed(Offset), DL),
        MachinePointerInfo::getStack(MF, Offset)));
  }
  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);

  SDValue Glue;
  SmallVector<std::pair<Register, MVT>, 8> ArgRegs;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (!VA.isRegLoc())
      continue;
    // The convention is written from the callee's side, and the general
    // registers are not the same ones on this side.
    Register Reg = VA.getLocReg();
    if (IA64::GRRegClass.contains(Reg))
      Reg = IA64::R120 + (Reg - IA64::R32);
    Chain = DAG.getCopyToReg(Chain, DL, Reg, OutVals[I], Glue);
    Glue = Chain.getValue(1);
    ArgRegs.push_back({Reg, VA.getLocVT()});
  }

  // A callee that is certain to be in this module has the same global
  // pointer. Any other may come back with its own.
  bool SameModule = false;
  if (const auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    SameModule = getTargetMachine().shouldAssumeDSOLocal(G->getGlobal());
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i64);
  } else if (const auto *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), MVT::i64);
  }

  SDValue GP = DAG.getRegister(IA64::R0, MVT::i64);
  if (!SameModule) {
    Register GPReg = MRI.getLiveInVirtReg(IA64::R1);
    if (!GPReg)
      GPReg = MF.addLiveIn(IA64::R1, &IA64::GRSaveRegClass);
    GP = DAG.getCopyFromReg(DAG.getEntryNode(), DL, GPReg, MVT::i64);
  }

  SmallVector<SDValue, 12> Ops = {Chain};
  if (Indirect)
    Ops.append({Entry, CalleeGP});
  else
    Ops.push_back(Callee);
  Ops.push_back(GP);
  for (auto [Reg, VT] : ArgRegs)
    Ops.push_back(DAG.getRegister(Reg, VT));
  Ops.push_back(
      DAG.getRegisterMask(TRI->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue.getNode())
    Ops.push_back(Glue);

  Chain = DAG.getNode(Indirect ? IA64ISD::CALL_IND : IA64ISD::CALL, DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Ops);
  Glue = Chain.getValue(1);
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetCCInfo(CLI.CallConv, CLI.IsVarArg, MF, RVLocs, *DAG.getContext());
  RetCCInfo.AnalyzeCallResult(CLI.Ins, RetCC_IA64);
  for (const CCValAssign &VA : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue)
                .getValue(1);
    InVals.push_back(Chain.getValue(0));
    Glue = Chain.getValue(2);
  }
  return Chain;
}

bool IA64TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_IA64);
}

SDValue
IA64TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_IA64);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "a result that is not in a register");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(IA64ISD::RET_GLUE, DL, MVT::Other, RetOps);
}

// fetchadd adds one of eight immediates (+/-1, 4, 8, 16) to a 4- or 8-byte
// location, and xchg stores a register. Everything else is a cmpxchg loop.
bool IA64TargetLowering::isFetchAddIncrement(int64_t Inc) {
  switch (Inc < 0 ? -uint64_t(Inc) : uint64_t(Inc)) {
  case 1:
  case 4:
  case 8:
  case 16:
    return true;
  default:
    return false;
  }
}

TargetLowering::AtomicExpansionKind
IA64TargetLowering::shouldExpandAtomicRMWInIR(const AtomicRMWInst *RMW) const {
  unsigned Bits = RMW->getType()->getPrimitiveSizeInBits();
  switch (RMW->getOperation()) {
  case AtomicRMWInst::Xchg:
    if (RMW->getType()->isIntegerTy() && Bits <= 64)
      return AtomicExpansionKind::None;
    break;
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
    if (const auto *C = dyn_cast<ConstantInt>(RMW->getValOperand()))
      if ((Bits == 32 || Bits == 64) && isFetchAddIncrement(C->getSExtValue()))
        return AtomicExpansionKind::None;
    break;
  default:
    break;
  }
  return AtomicExpansionKind::CmpXChg;
}

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

// These are GCC's IA-64 constraints: "a" is a register usable as the addl
// base (r0-r3), "b" a branch register and "f" an FP register. The capitals
// are immediates of various field widths, and "Q" and "S" are memory.
TargetLowering::ConstraintType
IA64TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'a':
    case 'b':
    case 'f':
      return C_RegisterClass;
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'R':
      return C_Immediate;
    case 'Q':
    case 'S':
      return C_Memory;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
IA64TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return {0U, &IA64::GRRegClass};
    case 'a':
      return {0U, &IA64::GR0_3RegClass};
    case 'b':
      return {0U, &IA64::BRRegClass};
    case 'f':
      return {0U, &IA64::FRRegClass};
    default:
      break;
    }
  }
  // ABI aliases for some registers.
  unsigned Reg = StringSwitch<unsigned>(Constraint.lower())
                     .Case("{gp}", IA64::R1)
                     .Case("{sp}", IA64::R12)
                     .Case("{tp}", IA64::R13)
                     .Case("{rp}", IA64::B0)
                     .Default(0);
  if (Reg)
    return {Reg, Reg == IA64::B0 ? &IA64::BRRegClass : &IA64::GRRegClass};
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

void IA64TargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                      StringRef Constraint,
                                                      std::vector<SDValue> &Ops,
                                                      SelectionDAG &DAG) const {
  if (Constraint.size() != 1)
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);
  auto *C = dyn_cast<ConstantSDNode>(Op);
  int64_t Value = C ? C->getSExtValue() : 0;
  bool Fits;
  switch (Constraint[0]) {
  default:
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);
  case 'I': // adds immediate.
    Fits = isInt<14>(Value);
    break;
  case 'J': // addl immediate.
    Fits = isInt<22>(Value);
    break;
  case 'K': // Logical instruction immediate.
    Fits = isInt<8>(Value);
    break;
  case 'L': // Compare immediate plus one.
    Fits = Value >= -127 && Value <= 128;
    break;
  case 'M': // A shift count.
    Fits = isUInt<6>(Value);
    break;
  case 'N': // Load/store post-increment immediate.
    Fits = isInt<9>(Value);
    break;
  case 'O':
    Fits = Value == 0;
    break;
  case 'P': // dep immediate form.
    Fits = Value == 0 || Value == -1;
    break;
  case 'R': // shladd shift count.
    Fits = Value >= 1 && Value <= 4;
    break;
  }
  if (C && Fits)
    Ops.push_back(
        DAG.getSignedTargetConstant(Value, SDLoc(Op), Op.getValueType()));
}

InlineAsm::ConstraintCode
IA64TargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  if (ConstraintCode == "Q")
    return InlineAsm::ConstraintCode::Q;
  if (ConstraintCode == "S")
    return InlineAsm::ConstraintCode::S;
  return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
}

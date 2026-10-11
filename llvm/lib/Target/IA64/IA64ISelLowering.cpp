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
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static bool CC_IA64_FP(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                       CCValAssign::LocInfo &LocInfo, ISD::ArgFlagsTy &ArgFlags,
                       CCState &State) {
  static const MCPhysReg Slots[] = {IA64::R32, IA64::R33, IA64::R34, IA64::R35,
                                    IA64::R36, IA64::R37, IA64::R38, IA64::R39};
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

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
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

  // Both halves of a product are two multiplications.
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

// The address of a symbol is read from the linkage table, which the global
// pointer addresses. What the entry for a function holds is the address of
// its descriptor, the one all modules agree on, and that is what a pointer
// to a function is.
SDValue IA64TargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc DL(Op);

  unsigned Flags =
      GV->getValueType()->isFunctionTy() ? IA64::S_LTOFF_FPTR : IA64::S_LTOFF;
  SDValue Addr = loadLinkageTableEntry(
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags), DL, DAG);
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

// An entry of a jump table is the distance from the table to the block,
// which needs nothing done to it when the module is loaded.
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
  static const MCPhysReg Slots[] = {IA64::R32, IA64::R33, IA64::R34, IA64::R35,
                                    IA64::R36, IA64::R37, IA64::R38, IA64::R39};
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

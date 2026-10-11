//===-- IA64ISelLowering.cpp - IA64 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ISelLowering.h"
#include "IA64.h"
#include "IA64SelectionDAGInfo.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

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

  // Compares write predicates; materialized as a value, the result is 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);

  // Selects are predicated on the compare itself, so a select on a value
  // becomes a select on its comparison with zero.
  setOperationAction(ISD::SELECT, MVT::i64, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

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
  default:
    llvm_unreachable("an operation with no custom lowering");
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
  SDValue Entry =
      DAG.getNode(IA64ISD::LTOFF, DL, MVT::i64,
                  DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags));
  SDValue Addr = DAG.getLoad(
      MVT::i64, DL, DAG.getEntryNode(), Entry,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), Align(8),
      MachineMemOperand::MODereferenceable | MachineMemOperand::MOInvariant);
  if (N->getOffset() == 0)
    return Addr;
  return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                     DAG.getConstant(N->getOffset(), DL, MVT::i64));
}

SDValue IA64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  if (IsVarArg)
    report_fatal_error("IA64 variable argument lists are not yet implemented");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_IA64);

  for (const CCValAssign &VA : ArgLocs) {
    if (!VA.isRegLoc())
      report_fatal_error("IA64 arguments in memory are not yet implemented");
    // The ABI leaves the bits above a value narrower than its slot undefined,
    // so emit no AssertZext/AssertSext whatever the zeroext/signext
    // attributes say: a caller built by another compiler may not extend.
    Register VReg = MF.addLiveIn(VA.getLocReg(), &IA64::GRRegClass);
    InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT()));
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

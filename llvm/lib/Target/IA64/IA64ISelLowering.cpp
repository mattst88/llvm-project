//===-- IA64ISelLowering.cpp - IA64 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ISelLowering.h"
#include "IA64.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

IA64TargetLowering::IA64TargetLowering(const IA64TargetMachine &TM,
                                       const IA64Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes.
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f32, &IA64::FRRegClass);
  addRegisterClass(MVT::f64, &IA64::FRRegClass);

  setStackPointerRegisterToSaveRestore(IA64::R12);

  computeRegisterProperties(STI.getRegisterInfo());
}

SDValue IA64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (!Ins.empty())
    report_fatal_error("IA64 argument lowering is not yet implemented");
  return Chain;
}

SDValue
IA64TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  report_fatal_error("IA64 return lowering is not yet implemented");
}

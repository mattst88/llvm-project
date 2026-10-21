//===-- IA64ISelLowering.h - IA64 DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that IA64 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H
#define LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class IA64Subtarget;
class IA64TargetMachine;

class IA64TargetLowering : public TargetLowering {
public:
  IA64TargetLowering(const IA64TargetMachine &TM, const IA64Subtarget &STI);

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override {
    if (VT.isVector())
      return VT.changeVectorElementTypeToInteger();
    return MVT::i64;
  }

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  bool SimplifyDemandedBitsForTargetNode(SDValue Op, const APInt &DemandedBits,
                                         const APInt &DemandedElts,
                                         KnownBits &Known,
                                         TargetLoweringOpt &TLO,
                                         unsigned Depth) const override;
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  // The exchange instructions zero-extend the value they load, and cmpxchg
  // compares it with all of ar.ccv.
  ISD::NodeType getExtendForAtomicOps() const override {
    return ISD::ZERO_EXTEND;
  }
  ISD::NodeType getExtendForAtomicCmpSwapArg() const override {
    return ISD::ZERO_EXTEND;
  }
  // Loads zero-extend as well, so a loaded value used in other blocks is
  // passed to them as it was loaded, not sign-extended for signed compares.
  bool isZExtFree(SDValue Val, EVT VT2) const override;
  bool shouldScopeInlinedMemcpy() const override { return true; }
  ISD::NodeType getPreferredExtendForValue(const Instruction *I) const override;
  // addl takes a signed immediate, so a negative 32-bit constant sign
  // extended needs no movl.
  bool signExtendConstant(const ConstantInt *C) const override {
    return C->getType()->isIntegerTy(32);
  }
  Register getRegisterByName(const char *RegName, LLT Ty,
                             const MachineFunction &MF) const override;
  ConstraintType getConstraintType(StringRef Constraint) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;
  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;

  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(const AtomicRMWInst *RMW) const override;
  static bool isFetchAddIncrement(int64_t Inc);

  // Both expand to a few branch-free instructions around popcnt.
  bool isCheapToSpeculateCttz(Type *Ty) const override { return true; }
  bool isCheapToSpeculateCtlz(Type *Ty) const override { return true; }

  // A select of an operation and one of its operands is the operation under
  // a predicate.
  bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode, EVT VT,
                                            unsigned SelectOpcode, SDValue X,
                                            SDValue Y) const override {
    return SelectOpcode == ISD::SELECT && VT.isScalarInteger();
  }

  unsigned getJumpTableEncoding() const override;

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  // The only addressing mode is a plain register.
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AddrSpace,
                             Instruction *I = nullptr) const override {
    return !AM.BaseGV && AM.BaseOffs == 0 &&
           (AM.Scale == 0 || (AM.Scale == 1 && !AM.HasBaseReg));
  }

  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  // The FP multiply instruction is fma.
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override {
    return VT == MVT::f32 || VT == MVT::f64 || VT == MVT::f80;
  }

  // f0 and f1 read as 0.0 and 1.0 at any precision.
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override {
    if (VT == MVT::f80)
      return Imm.isPosZero() || Imm.isExactlyValue(1.0);
    return VT == MVT::f32 || VT == MVT::f64;
  }
  bool isJumpTableRelative() const override { return true; }

  // The unwinder provides four registers, r15-r18, that a personality routine
  // may set for a landing pad; the C++ personality puts the exception pointer
  // in r15 and the selector in r16.
  Register getExceptionPointerRegister(ExceptionHandling,
                                       const Constant *) const override;
  Register getExceptionSelectorRegister(ExceptionHandling,
                                        const Constant *) const override;

  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;

  /// Whether \p CB can be a tail call, as a musttail call must be. Returns
  /// false when unsure.
  bool supportsTailCallFor(const CallBase *CB) const;

  Value *getIRStackGuard(IRBuilderBase &IRB,
                         const LibcallLoweringInfo &Libcalls) const override;

  bool functionArgumentNeedsConsecutiveRegisters(
      Type *Ty, CallingConv::ID CallConv, bool IsVarArg,
      const DataLayout &DL) const override;

  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override;
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CC,
                                         EVT VT) const override;

  /// The fclass tests that together check for the classes in \p Mask.
  static void getClassTests(unsigned Mask, SmallVectorImpl<unsigned> &Tests);

private:
  const IA64Subtarget &Subtarget;

private:
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSPONENTRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINIT_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEH_DWARF_CFA(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGET_DYNAMIC_AREA_OFFSET(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerIS_FPCLASS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue loadLinkageTableEntry(SDValue Sym, const SDLoc &DL,
                                SelectionDAG &DAG) const;
  MachineBasicBlock *emitFDiv(MachineInstr &MI, MachineBasicBlock *MBB) const;
  MachineBasicBlock *emitFDivConst(MachineInstr &MI,
                                   MachineBasicBlock *MBB) const;
  MachineBasicBlock *emitIDiv(MachineInstr &MI, MachineBasicBlock *MBB) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H

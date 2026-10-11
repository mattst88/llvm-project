//===- IA64ISelDAGToDAG.cpp - A dag to dag inst selector for IA64 ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the IA64 target.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-isel"

namespace {

class IA64DAGToDAGISel : public SelectionDAGISel {
  const IA64Subtarget *Subtarget = nullptr;

public:
  IA64DAGToDAGISel(IA64TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<IA64Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *Node) override;

  bool selectCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                     const SDLoc &DL, SmallVectorImpl<SDValue> &Ops,
                     bool &IsImm);

#include "IA64GenDAGISel.inc"
};

class IA64DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;

  IA64DAGToDAGISelLegacy(IA64TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<IA64DAGToDAGISel>(TM, OptLevel)) {}

  StringRef getPassName() const override {
    return "IA64 DAG->DAG Pattern Instruction Selection";
  }
};

} // end anonymous namespace

char IA64DAGToDAGISelLegacy::ID = 0;

static IA64CC::CondCode getIA64CC(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:
    return IA64CC::EQ;
  case ISD::SETNE:
    return IA64CC::NE;
  case ISD::SETLT:
    return IA64CC::LT;
  case ISD::SETGE:
    return IA64CC::GE;
  case ISD::SETGT:
    return IA64CC::GT;
  case ISD::SETLE:
    return IA64CC::LE;
  case ISD::SETULT:
    return IA64CC::LTU;
  case ISD::SETUGE:
    return IA64CC::GEU;
  case ISD::SETUGT:
    return IA64CC::GTU;
  case ISD::SETULE:
    return IA64CC::LEU;
  default:
    llvm_unreachable("Not the condition of an integer compare");
  }
}

/// The operands that say what a compare compares: the condition, the left
/// side and the right, which is a constant if there is a compare that
/// takes it. They are the same in every instruction that has a compare in
/// it. Fails if the operands are not integers.
bool IA64DAGToDAGISel::selectCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                     const SDLoc &DL,
                                     SmallVectorImpl<SDValue> &Ops,
                                     bool &IsImm) {
  if (LHS.getValueType() != MVT::i64)
    return false;
  IA64CC::CondCode IACC = getIA64CC(CC);
  Ops.push_back(CurDAG->getTargetConstant(IACC, DL, MVT::i64));
  Ops.push_back(LHS);
  auto *C = dyn_cast<ConstantSDNode>(RHS);
  IsImm = C && IA64CC::isLegalImmediate(IACC, C->getSExtValue());
  if (IsImm)
    Ops.push_back(CurDAG->getTargetConstant(C->getSExtValue(), DL, MVT::i64));
  else
    Ops.push_back(RHS);
  return true;
}

void IA64DAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }

  // r0 reads as zero, so zero needs no instruction.
  if (Node->getOpcode() == ISD::Constant && isNullConstant(SDValue(Node, 0))) {
    SDValue Zero = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), SDLoc(Node),
                                          IA64::R0, MVT::i64);
    ReplaceNode(Node, Zero.getNode());
    return;
  }

  SDLoc DL(Node);
  switch (Node->getOpcode()) {
  default:
    break;
  case ISD::FrameIndex: {
    // A frame index is sp plus an offset, resolved once the frame is laid
    // out.
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    CurDAG->SelectNodeTo(Node, IA64::ADDS, MVT::i64,
                         CurDAG->getTargetConstant(0, DL, MVT::i64),
                         CurDAG->getTargetFrameIndex(FI, MVT::i64),
                         CurDAG->getRegister(IA64::P0, MVT::i1));
    return;
  }
  case ISD::SETCC: {
    SmallVector<SDValue, 4> Ops;
    bool IsImm;
    if (!selectCompare(Node->getOperand(0), Node->getOperand(1),
                       cast<CondCodeSDNode>(Node->getOperand(2))->get(), DL,
                       Ops, IsImm))
      break;
    CurDAG->SelectNodeTo(Node, IsImm ? IA64::SETCCi : IA64::SETCC, MVT::i64,
                         Ops);
    return;
  }
  case ISD::BR_CC: {
    SmallVector<SDValue, 5> Ops = {Node->getOperand(4)};
    bool IsImm;
    if (!selectCompare(Node->getOperand(2), Node->getOperand(3),
                       cast<CondCodeSDNode>(Node->getOperand(1))->get(), DL,
                       Ops, IsImm))
      break;
    Ops.push_back(Node->getOperand(0));
    CurDAG->SelectNodeTo(Node, IsImm ? IA64::BRCCi : IA64::BRCC, MVT::Other,
                         Ops);
    return;
  }
  case ISD::SELECT_CC: {
    if (Node->getValueType(0) != MVT::i64)
      break;
    SmallVector<SDValue, 6> Ops = {Node->getOperand(3), Node->getOperand(2)};
    bool IsImm;
    if (!selectCompare(Node->getOperand(0), Node->getOperand(1),
                       cast<CondCodeSDNode>(Node->getOperand(4))->get(), DL,
                       Ops, IsImm))
      break;
    CurDAG->SelectNodeTo(Node, IsImm ? IA64::SELECTi : IA64::SELECT, MVT::i64,
                         Ops);
    return;
  }
  }

  SelectCode(Node);
}

FunctionPass *llvm::createIA64ISelDag(IA64TargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new IA64DAGToDAGISelLegacy(TM, OptLevel);
}

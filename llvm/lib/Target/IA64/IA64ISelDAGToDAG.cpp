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

  SelectCode(Node);
}

FunctionPass *llvm::createIA64ISelDag(IA64TargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new IA64DAGToDAGISelLegacy(TM, OptLevel);
}

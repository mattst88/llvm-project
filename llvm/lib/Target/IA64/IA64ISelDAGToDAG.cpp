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
    bool Changed = SelectionDAGISel::runOnMachineFunction(MF);
    restoreGPAtLandingPads(MF);
    return Changed;
  }

  void restoreGPAtLandingPads(MachineFunction &MF);

  void Select(SDNode *Node) override;

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintCode,
                                    std::vector<SDValue> &OutOps) override;

  // Compare instruction forms, by the kind of compare operands.
  enum CompareForm { Register, Immediate, FloatingPoint };
  CompareForm selectCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                            const SDLoc &DL, SmallVectorImpl<SDValue> &Ops);

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

// A throwing call does not return, and GP holds whatever the throwing
// function had in it. Like the code after a call, a landing pad must restore
// its own function's GP before anything else.
void IA64DAGToDAGISel::restoreGPAtLandingPads(MachineFunction &MF) {
  if (none_of(MF, [](const MachineBasicBlock &MBB) { return MBB.isEHPad(); }))
    return;

  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  llvm::Register GPReg = MRI.getLiveInVirtReg(IA64::R1);
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

// A condition that does not specify the unordered result is treated as the
// one that is false for unordered operands.
static IA64CC::CondCode getIA64FPCC(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:
  case ISD::SETOEQ:
    return IA64CC::OEQ;
  case ISD::SETNE:
  case ISD::SETUNE:
    return IA64CC::UNE;
  case ISD::SETLT:
  case ISD::SETOLT:
    return IA64CC::OLT;
  case ISD::SETUGE:
    return IA64CC::UGE;
  case ISD::SETLE:
  case ISD::SETOLE:
    return IA64CC::OLE;
  case ISD::SETUGT:
    return IA64CC::UGT;
  case ISD::SETGT:
  case ISD::SETOGT:
    return IA64CC::OGT;
  case ISD::SETULE:
    return IA64CC::ULE;
  case ISD::SETGE:
  case ISD::SETOGE:
    return IA64CC::OGE;
  case ISD::SETULT:
    return IA64CC::ULT;
  case ISD::SETUO:
    return IA64CC::UO;
  case ISD::SETO:
    return IA64CC::ORD;
  default:
    llvm_unreachable("Not the condition of a floating-point compare");
  }
}

/// Push the compare operands: condition code, LHS, and RHS, which is an
/// immediate if the compare has an immediate form. These operands are common
/// to every instruction containing a compare; returns which of the three
/// forms applies.
IA64DAGToDAGISel::CompareForm
IA64DAGToDAGISel::selectCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                const SDLoc &DL,
                                SmallVectorImpl<SDValue> &Ops) {
  if (LHS.getValueType().isFloatingPoint()) {
    Ops.push_back(CurDAG->getTargetConstant(getIA64FPCC(CC), DL, MVT::i64));
    Ops.push_back(LHS);
    Ops.push_back(RHS);
    return FloatingPoint;
  }
  IA64CC::CondCode IACC = getIA64CC(CC);
  Ops.push_back(CurDAG->getTargetConstant(IACC, DL, MVT::i64));
  Ops.push_back(LHS);
  auto *C = dyn_cast<ConstantSDNode>(RHS);
  if (C && IA64CC::isLegalImmediate(IACC, C->getSExtValue())) {
    Ops.push_back(CurDAG->getTargetConstant(C->getSExtValue(), DL, MVT::i64));
    return Immediate;
  }
  Ops.push_back(RHS);
  return Register;
}

// Memory operands are a register address, whatever the constraint.
bool IA64DAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintCode,
    std::vector<SDValue> &OutOps) {
  OutOps.push_back(Op);
  return false;
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
  case ISD::PREFETCH: {
    // lfetch's hint is how many cache levels the line may skip, which grows
    // as the expected locality drops, and whether the line will be written.
    // The cache type (data or instruction) is not expressible.
    bool Write = Node->getConstantOperandVal(2);
    uint64_t Locality = Node->getConstantOperandVal(3);
    CurDAG->SelectNodeTo(
        Node, Write ? IA64::LFETCH_EXCL : IA64::LFETCH, MVT::Other,
        {Node->getOperand(1),
         CurDAG->getTargetConstant(3 - Locality, DL, MVT::i64),
         CurDAG->getRegister(IA64::P0, MVT::i1), Node->getOperand(0)});
    return;
  }
  case ISD::ConstantFP: {
    // f0 reads as 0.0 and f1 as 1.0. Other constants are materialized in a GR
    // and moved over with setf.
    const APFloat &Value = cast<ConstantFPSDNode>(Node)->getValueAPF();
    MVT VT = Node->getSimpleValueType(0);
    if (Value.isPosZero() || Value.isExactlyValue(1.0)) {
      SDValue Reg =
          CurDAG->getCopyFromReg(CurDAG->getEntryNode(), DL,
                                 Value.isPosZero() ? IA64::F0 : IA64::F1, VT);
      ReplaceNode(Node, Reg.getNode());
      return;
    }
    assert(VT != MVT::f80 && "an extended-precision constant in a register");
    SDValue P0 = CurDAG->getRegister(IA64::P0, MVT::i1);
    SDValue Bits = CurDAG->getTargetConstant(
        Value.bitcastToAPInt().getZExtValue(), DL, MVT::i64);
    SDNode *GR = CurDAG->getMachineNode(IA64::MOVL, DL, MVT::i64, Bits, P0);
    CurDAG->SelectNodeTo(Node, VT == MVT::f32 ? IA64::SETF_S : IA64::SETF_D, VT,
                         SDValue(GR, 0), P0);
    return;
  }
  case ISD::ATOMIC_FENCE: {
    // Atomic loads and stores already carry the ordering they need, so only a
    // seq_cst fence emits anything.
    SDValue Chain = Node->getOperand(0);
    auto Ordering = AtomicOrdering(Node->getConstantOperandVal(1));
    auto Scope = SyncScope::ID(Node->getConstantOperandVal(2));
    if (Ordering != AtomicOrdering::SequentiallyConsistent ||
        Scope == SyncScope::SingleThread) {
      ReplaceUses(SDValue(Node, 0), Chain);
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    CurDAG->SelectNodeTo(Node, IA64::MF, MVT::Other,
                         CurDAG->getRegister(IA64::P0, MVT::i1), Chain);
    return;
  }
  case ISD::ATOMIC_STORE: {
    // st.rel orders the store after preceding accesses. Ordering it before
    // subsequent loads as well (seq_cst) needs an mf.
    static const unsigned Opcodes[] = {IA64::ST1_REL, IA64::ST2_REL,
                                       IA64::ST4_REL, IA64::ST8_REL};
    auto *Atomic = cast<AtomicSDNode>(Node);
    SDValue P0 = CurDAG->getRegister(IA64::P0, MVT::i1);
    SDValue Ops[] = {Node->getOperand(2), Node->getOperand(1),
                     CurDAG->getTargetConstant(0, DL, MVT::i64), P0,
                     Node->getOperand(0)};
    unsigned Opcode = Opcodes[Log2_32(Atomic->getMemoryVT().getStoreSize())];
    MachineMemOperand *MMO = Atomic->getMemOperand();
    if (Atomic->getMergedOrdering() != AtomicOrdering::SequentiallyConsistent) {
      SDNode *Store = CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Ops);
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Store), {MMO});
      return;
    }
    SDNode *Store = CurDAG->getMachineNode(Opcode, DL, MVT::Other, Ops);
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(Store), {MMO});
    CurDAG->SelectNodeTo(Node, IA64::MF, MVT::Other, P0, SDValue(Store, 0));
    return;
  }
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_CMP_SWAP:
  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB: {
    // These have acquire semantics, and fetchadd can have release semantics
    // instead. An operation that needs both is preceded by an mf.
    auto *Atomic = cast<AtomicSDNode>(Node);
    unsigned Size = Atomic->getMemoryVT().getStoreSize();
    AtomicOrdering Ordering = Atomic->getMergedOrdering();
    SDValue P0 = CurDAG->getRegister(IA64::P0, MVT::i1);
    SDValue Hint = CurDAG->getTargetConstant(0, DL, MVT::i64);
    SDValue Chain = Node->getOperand(0);
    SDValue Ptr = Node->getOperand(1);
    unsigned Opcode;
    SmallVector<SDValue, 6> Ops = {Ptr};
    bool Release = isReleaseOrStronger(Ordering);
    if (Node->getOpcode() == ISD::ATOMIC_CMP_SWAP) {
      Opcode = IA64::CMPXCHG;
      Ops.append({Node->getOperand(2), Node->getOperand(3),
                  CurDAG->getTargetConstant(Size, DL, MVT::i64)});
    } else if (Node->getOpcode() == ISD::ATOMIC_SWAP) {
      static const unsigned Opcodes[] = {IA64::XCHG1, IA64::XCHG2, IA64::XCHG4,
                                         IA64::XCHG8};
      Opcode = Opcodes[Log2_32(Size)];
      Ops.append({Node->getOperand(2), Hint, P0});
    } else {
      auto *C = dyn_cast<ConstantSDNode>(Node->getOperand(2));
      if (!C || Size < 4)
        break;
      int64_t Inc = C->getSExtValue();
      if (Size == 4)
        Inc = SignExtend64<32>(Inc);
      if (Node->getOpcode() == ISD::ATOMIC_LOAD_SUB)
        Inc = -uint64_t(Inc);
      if (!IA64TargetLowering::isFetchAddIncrement(Inc))
        break;
      bool UseRel = Release && !isAcquireOrStronger(Ordering);
      if (Size == 4)
        Opcode = UseRel ? IA64::FETCHADD4_REL : IA64::FETCHADD4_ACQ;
      else
        Opcode = UseRel ? IA64::FETCHADD8_REL : IA64::FETCHADD8_ACQ;
      Release &= !UseRel;
      Ops.append({CurDAG->getTargetConstant(Inc, DL, MVT::i64), Hint, P0});
    }
    if (Release)
      Chain = SDValue(
          CurDAG->getMachineNode(IA64::MF, DL, MVT::Other, P0, Chain), 0);
    Ops.push_back(Chain);
    SDNode *New = CurDAG->SelectNodeTo(Node, Opcode, MVT::i64, MVT::Other, Ops);
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(New), {Atomic->getMemOperand()});
    return;
  }
  case ISD::SETCC: {
    static const unsigned Opcodes[] = {IA64::SETCC, IA64::SETCCi, IA64::SETCCf};
    SmallVector<SDValue, 4> Ops;
    CompareForm Form = selectCompare(
        Node->getOperand(0), Node->getOperand(1),
        cast<CondCodeSDNode>(Node->getOperand(2))->get(), DL, Ops);
    CurDAG->SelectNodeTo(Node, Opcodes[Form], MVT::i64, Ops);
    return;
  }
  case ISD::BR_CC: {
    static const unsigned Opcodes[] = {IA64::BRCC, IA64::BRCCi, IA64::BRCCf};
    SmallVector<SDValue, 5> Ops = {Node->getOperand(4)};
    CompareForm Form = selectCompare(
        Node->getOperand(2), Node->getOperand(3),
        cast<CondCodeSDNode>(Node->getOperand(1))->get(), DL, Ops);
    Ops.push_back(Node->getOperand(0));
    CurDAG->SelectNodeTo(Node, Opcodes[Form], MVT::Other, Ops);
    return;
  }
  case ISD::SELECT_CC: {
    static const unsigned Opcodes[][3] = {
        {IA64::SELECT, IA64::SELECTi, IA64::SELECTf},
        {IA64::SELECT_FR, IA64::SELECT_FRi, IA64::SELECT_FRf}};
    MVT VT = Node->getSimpleValueType(0);
    SmallVector<SDValue, 6> Ops = {Node->getOperand(3), Node->getOperand(2)};
    CompareForm Form = selectCompare(
        Node->getOperand(0), Node->getOperand(1),
        cast<CondCodeSDNode>(Node->getOperand(4))->get(), DL, Ops);
    CurDAG->SelectNodeTo(Node, Opcodes[VT.isFloatingPoint()][Form], VT, Ops);
    return;
  }
  }

  SelectCode(Node);
}

FunctionPass *llvm::createIA64ISelDag(IA64TargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new IA64DAGToDAGISelLegacy(TM, OptLevel);
}

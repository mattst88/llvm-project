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
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"

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
    restoreGPRightAfterCalls(MF);
    restoreGPAtLandingPads(MF);
    return Changed;
  }

  void restoreGPRightAfterCalls(MachineFunction &MF);
  void restoreGPAtLandingPads(MachineFunction &MF);

  void Select(SDNode *Node) override;
  void PreprocessISelDAG() override;
  void PostprocessISelDAG() override;

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintCode,
                                    std::vector<SDValue> &OutOps) override;

  // Compare instruction forms, by the kind of compare operands.
  enum CompareForm { Register, Immediate, FloatingPoint };
  bool selectMinMax(SDNode *Node);
  bool selectBitField(SDNode *Node);
  bool appendChainCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                          const SDLoc &DL, SmallVectorImpl<SDValue> &Ops,
                          unsigned First, bool &HasPlain);
  bool selectCompareChain(SDNode *Node);
  bool selectSelectChain(SDNode *Node);
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

// The GP restore after a returns_twice call is a separate copy node, and
// nothing in the DAG orders it before nodes that read GP: an address formed
// after the call could be scheduled before it and computed from the callee's
// GP. The copy does not read anything the call writes, so move it up to
// immediately after the call.
void IA64DAGToDAGISel::restoreGPRightAfterCalls(MachineFunction &MF) {
  unsigned FrameDestroy =
      Subtarget->getInstrInfo()->getCallFrameDestroyOpcode();
  const uint32_t *TwiceMask =
      Subtarget->getRegisterInfo()->getReturnsTwiceCallPreservedMask();
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator AfterCall = MBB.end();
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (MI.isCall()) {
        AfterCall = MBB.end();
        if (none_of(MI.operands(), [&](const MachineOperand &MO) {
              return MO.isRegMask() && MO.getRegMask() == TwiceMask;
            }))
          continue;
        AfterCall = std::next(MI.getIterator());
        if (AfterCall != MBB.end() && AfterCall->getOpcode() == FrameDestroy)
          ++AfterCall;
        continue;
      }
      if (AfterCall == MBB.end() || !MI.isCopy() ||
          MI.getOperand(0).getReg() != IA64::R1)
        continue;
      if (AfterCall != MI.getIterator())
        MBB.splice(AfterCall, &MBB, MI.getIterator());
      AfterCall = MBB.end();
    }
  }
}

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
  // A test of whether a single bit is set is a bit test.
  if ((CC == ISD::SETEQ || CC == ISD::SETNE) && isNullConstant(RHS) &&
      LHS.getOpcode() == ISD::AND && isa<ConstantSDNode>(LHS.getOperand(1)) &&
      LHS.getConstantOperandAPInt(1).isPowerOf2()) {
    IA64CC::CondCode IACC =
        CC == ISD::SETEQ ? IA64CC::BitClear : IA64CC::BitSet;
    Ops.push_back(CurDAG->getTargetConstant(IACC, DL, MVT::i64));
    Ops.push_back(LHS.getOperand(0));
    Ops.push_back(CurDAG->getTargetConstant(
        LHS.getConstantOperandAPInt(1).logBase2(), DL, MVT::i64));
    return Immediate;
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

// Nodes selected here for non-strict operations raise no observable FP
// exceptions, just like the ones selected by the tables.
static void setNoFPExcept(SDNode *Node) {
  SDNodeFlags Flags = Node->getFlags();
  Flags.setNoFPExcept(true);
  Node->setFlags(Flags);
}

// Append the operands of another compare to a chain of compares whose
// operands start at Ops[First]. The compares after the first must have
// parallel forms, which only integer compares do, so a compare without one
// goes first, and HasPlain records that the chain has one. Returns false,
// appending nothing, if the chain cannot take the compare.
bool IA64DAGToDAGISel::appendChainCompare(SDValue LHS, SDValue RHS,
                                          ISD::CondCode CC, const SDLoc &DL,
                                          SmallVectorImpl<SDValue> &Ops,
                                          unsigned First, bool &HasPlain) {
  unsigned Start = Ops.size();
  selectCompare(LHS, RHS, CC, DL, Ops);
  auto IACC = IA64CC::CondCode(Ops[Start]->getAsZExtVal());
  std::optional<int64_t> Imm;
  if (Ops[Start + 2].getOpcode() == ISD::TargetConstant)
    Imm = Ops[Start + 2]->getAsAPIntVal().getSExtValue();
  if (IA64CC::getParallelCompare(IACC, Imm))
    return true;
  if (HasPlain) {
    Ops.truncate(Start);
    return false;
  }
  HasPlain = true;
  std::rotate(Ops.begin() + First, Ops.begin() + Start, Ops.end());
  return true;
}

// Select an AND or OR of integer compares, through further ANDs or ORs of the
// same kind, as one compare followed by parallel compares that accumulate
// into its predicates. The combining nodes and compares must have no other
// users, or they would be computed twice.
bool IA64DAGToDAGISel::selectCompareChain(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();
  if ((Opcode != ISD::AND && Opcode != ISD::OR) ||
      Node->getValueType(0) != MVT::i64)
    return false;

  SmallVector<SDNode *, 4> Compares;
  SmallVector<SDNode *, 4> Worklist = {Node};
  while (!Worklist.empty()) {
    SDNode *N = Worklist.pop_back_val();
    for (SDValue Op : N->ops()) {
      if (!Op.hasOneUse())
        return false;
      if (Op.getOpcode() == Opcode)
        Worklist.push_back(Op.getNode());
      else if (Op.getOpcode() == ISD::SETCC)
        Compares.push_back(Op.getNode());
      else
        return false;
    }
  }

  SDLoc DL(Node);
  SmallVector<SDValue, 12> Ops;
  bool HasPlain = false;
  for (SDNode *Compare : Compares)
    if (!appendChainCompare(Compare->getOperand(0), Compare->getOperand(1),
                            cast<CondCodeSDNode>(Compare->getOperand(2))->get(),
                            DL, Ops, 0, HasPlain))
      return false;
  static const unsigned Opcodes[2][2] = {
      {IA64::SETCC_OR, IA64::SETCC_AND}, {IA64::SETCC_ORf, IA64::SETCC_ANDf}};
  bool FP = Ops[1].getValueType().isFloatingPoint();
  SDNode *New = CurDAG->SelectNodeTo(Node, Opcodes[FP][Opcode == ISD::AND],
                                     MVT::i64, Ops);
  if (FP)
    setNoFPExcept(New);
  return true;
}

// The DAG combiner turns a select on an AND of compares into a select on the
// first compare between a select on the second and the false value, and a
// select on an OR into one between the true value and a select on the
// second. Select such a nest of SELECT_CCs on integer compares, each inner
// one with no other users, as one select on a chain of compares. An inner
// select with its values the other way round tests the inverse condition,
// which for an FP compare holds for unordered operands if the original does
// not.
// The chain stops at a compare it cannot take, whose select becomes the value
// selected.
bool IA64DAGToDAGISel::selectSelectChain(SDNode *Node) {
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  SDValue True = Node->getOperand(2), False = Node->getOperand(3);
  auto IsInner = [&](SDValue V) {
    return V.getOpcode() == ISD::SELECT_CC && V.hasOneUse();
  };
  // In an AND chain the false value is common to every select and the chain
  // goes on through the true values; the other way round in an OR chain.
  bool And = IsInner(True) &&
             (True.getOperand(3) == False || True.getOperand(2) == False);
  if (!And && !(IsInner(False) &&
                (False.getOperand(2) == True || False.getOperand(3) == True)))
    return false;
  SDValue Common = And ? False : True;
  SDValue Next = And ? True : False;

  SmallVector<SDValue, 12> Ops = {False, True};
  bool HasPlain = false;
  if (!appendChainCompare(Node->getOperand(0), Node->getOperand(1),
                          cast<CondCodeSDNode>(Node->getOperand(4))->get(), DL,
                          Ops, 2, HasPlain))
    return false;
  unsigned Count = 1;
  while (IsInner(Next)) {
    // The value this select gives when its compare would end the chain
    // there, and the one it gives otherwise.
    SDValue Ends = Next.getOperand(And ? 3 : 2);
    SDValue Goes = Next.getOperand(And ? 2 : 3);
    ISD::CondCode CC = cast<CondCodeSDNode>(Next.getOperand(4))->get();
    if (Goes == Common) {
      std::swap(Ends, Goes);
      CC = ISD::getSetCCInverse(CC, Next.getOperand(0).getValueType());
    } else if (Ends != Common) {
      break;
    }
    if (!appendChainCompare(Next.getOperand(0), Next.getOperand(1), CC, DL, Ops,
                            2, HasPlain))
      break;
    ++Count;
    Next = Goes;
  }
  if (Count < 2)
    return false;
  Ops[And ? 1 : 0] = Next;

  static const unsigned Opcodes[2][2][2] = {
      {{IA64::SELECT_OR, IA64::SELECT_AND},
       {IA64::SELECT_ORf, IA64::SELECT_ANDf}},
      {{IA64::SELECT_FR_OR, IA64::SELECT_FR_AND},
       {IA64::SELECT_FR_ORf, IA64::SELECT_FR_ANDf}}};
  bool FP = Ops[3].getValueType().isFloatingPoint();
  SDNode *New =
      CurDAG->SelectNodeTo(Node, Opcodes[VT.isFloatingPoint()][FP][And], VT, Ops);
  if (FP)
    setNoFPExcept(New);
  return true;
}

// Memory operands are a register address, whatever the constraint.
bool IA64DAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintCode,
    std::vector<SDValue> &OutOps) {
  OutOps.push_back(Op);
  return false;
}

// Whether Value is a shift by a constant, and the shift amount.
static bool isShiftBy(SDValue Value, unsigned Opcode, unsigned &Amount) {
  if (Value.getOpcode() != Opcode)
    return false;
  const auto *Count = dyn_cast<ConstantSDNode>(Value.getOperand(1));
  if (!Count || Count->getZExtValue() > 63)
    return false;
  Amount = Count->getZExtValue();
  return true;
}

// Whether Value is an AND with a constant mask.
static bool isMaskedBy(SDValue Value, uint64_t &Mask) {
  if (Value.getOpcode() != ISD::AND)
    return false;
  const auto *Constant = dyn_cast<ConstantSDNode>(Value.getOperand(1));
  if (!Constant)
    return false;
  Mask = Constant->getZExtValue();
  return true;
}

// Select bit-field instructions: extr/extr.u extract a field to the low bits,
// sign- or zero-extended; dep.z deposits the low bits as a field into zero;
// dep deposits them into another register. Shift-and-mask sequences with a
// contiguous mask map to these and need no mask constant in a register.
bool IA64DAGToDAGISel::selectBitField(SDNode *Node) {
  if (Node->getValueType(0) != MVT::i64)
    return false;

  SDLoc DL(Node);
  SDValue P0 = CurDAG->getRegister(IA64::P0, MVT::i1);
  auto Field = [&](unsigned Opcode, SDValue Source, unsigned Pos,
                   unsigned Len) {
    CurDAG->SelectNodeTo(Node, Opcode, MVT::i64,
                         {Source, CurDAG->getTargetConstant(Pos, DL, MVT::i64),
                          CurDAG->getTargetConstant(Len, DL, MVT::i64), P0});
    return true;
  };
  // Set or clear every bit of a field.
  auto Fill = [&](bool Ones, SDValue Source, uint64_t Bits) {
    CurDAG->SelectNodeTo(
        Node, IA64::DEPi, MVT::i64,
        {CurDAG->getSignedTargetConstant(Ones ? -1 : 0, DL, MVT::i64), Source,
         CurDAG->getTargetConstant(llvm::countr_zero(Bits), DL, MVT::i64),
         CurDAG->getTargetConstant(llvm::popcount(Bits), DL, MVT::i64), P0});
    return true;
  };

  SDValue N0 = Node->getOperand(0);
  unsigned Amount, Inner;
  uint64_t Mask;
  switch (Node->getOpcode()) {
  default:
    return false;
  case ISD::AND: {
    if (!isMaskedBy(SDValue(Node, 0), Mask))
      return false;
    if (isMask_64(Mask)) {
      // The low bits of a right shift are a field of the shifted value.
      unsigned Len = llvm::countr_one(Mask);
      if (isShiftBy(N0, ISD::SRL, Amount))
        return Field(IA64::EXTR_U, N0.getOperand(0), Amount,
                     std::min(Len, 64 - Amount));
      // 8, 16 and 32 bits have dedicated zero-extend instructions, and a
      // small mask fits an immediate.
      if (isInt<8>(Mask) || Len == 8 || Len == 16 || Len == 32)
        return false;
      return Field(IA64::EXTR_U, N0, 0, Len);
    }
    // A contiguous run kept from a left shift, starting at the shift amount,
    // is a dep.z at that position.
    if (isShiftBy(N0, ISD::SHL, Amount)) {
      uint64_t Kept = Mask & (~0ULL << Amount);
      if (isShiftedMask_64(Kept) && unsigned(llvm::countr_zero(Kept)) == Amount)
        return Field(IA64::DEP_Z, N0.getOperand(0), Amount,
                     llvm::popcount(Kept));
    }
    if (isInt<8>(Mask) || !isShiftedMask_64(~Mask))
      return false;
    return Fill(false, N0, ~Mask);
  }
  case ISD::SHL: {
    // A masked value shifted left, when the mask is a contiguous low run or
    // covers every bit the shift keeps.
    if (!isShiftBy(SDValue(Node, 0), ISD::SHL, Amount) || !isMaskedBy(N0, Mask))
      return false;
    uint64_t Kept = Mask << Amount;
    if (!isShiftedMask_64(Kept) || unsigned(llvm::countr_zero(Kept)) != Amount)
      return false;
    return Field(IA64::DEP_Z, N0.getOperand(0), Amount, llvm::popcount(Kept));
  }
  case ISD::SRL: {
    if (!isShiftBy(SDValue(Node, 0), ISD::SRL, Amount))
      return false;
    // A left shift followed by a larger right shift is a zero-extended field.
    if (isShiftBy(N0, ISD::SHL, Inner) && Inner <= Amount)
      return Field(IA64::EXTR_U, N0.getOperand(0), Amount - Inner, 64 - Amount);
    if (isMaskedBy(N0, Mask) && isMask_64(Mask >> Amount))
      return Field(IA64::EXTR_U, N0.getOperand(0), Amount,
                   llvm::countr_one(Mask >> Amount));
    return false;
  }
  case ISD::SRA: {
    // The same shifts with an arithmetic right shift give a sign-extended
    // field.
    if (!isShiftBy(SDValue(Node, 0), ISD::SRA, Amount))
      return false;
    if (isShiftBy(N0, ISD::SHL, Inner) && Inner <= Amount)
      return Field(IA64::EXTR, N0.getOperand(0), Amount - Inner, 64 - Amount);
    if (N0.getOpcode() == ISD::SIGN_EXTEND_INREG) {
      unsigned Width =
          cast<VTSDNode>(N0.getOperand(1))->getVT().getSizeInBits();
      if (Amount < Width)
        return Field(IA64::EXTR, N0.getOperand(0), Amount, Width - Amount);
    }
    return false;
  }
  case ISD::SIGN_EXTEND_INREG: {
    unsigned Width =
        cast<VTSDNode>(Node->getOperand(1))->getVT().getSizeInBits();
    if ((isShiftBy(N0, ISD::SRL, Amount) || isShiftBy(N0, ISD::SRA, Amount)) &&
        Amount + Width <= 64)
      return Field(IA64::EXTR, N0.getOperand(0), Amount, Width);
    return false;
  }
  case ISD::OR: {
    SDValue N1 = Node->getOperand(1);
    if (const auto *Constant = dyn_cast<ConstantSDNode>(N1)) {
      uint64_t Bits = Constant->getZExtValue();
      if (isInt<8>(Bits) || !isShiftedMask_64(Bits))
        return false;
      return Fill(true, N0, Bits);
    }
    // A register with a contiguous run of up to 16 bits cleared, ORed with a
    // value that is zero outside that run, is a dep of the value.
    for (bool Swap : {false, true}) {
      SDValue Into = Swap ? N1 : N0;
      SDValue Value = Swap ? N0 : N1;
      if (!isMaskedBy(Into, Mask) || !isShiftedMask_64(~Mask))
        continue;
      uint64_t Bits = ~Mask;
      unsigned Pos = llvm::countr_zero(Bits);
      unsigned Len = llvm::popcount(Bits);
      if (Len > 16 || !CurDAG->MaskedValueIsZero(Value, APInt(64, Mask)))
        continue;
      // dep takes the value from the low bits of its source register, and
      // only as many bits as the field is wide.
      uint64_t Covers;
      if (isMaskedBy(Value, Covers) && (Covers & Bits) == Bits)
        Value = Value.getOperand(0);
      if (Pos != 0) {
        if (!isShiftBy(Value, ISD::SHL, Amount) || Amount != Pos)
          continue;
        Value = Value.getOperand(0);
      }
      if (isMaskedBy(Value, Covers) &&
          (Covers & maskTrailingOnes<uint64_t>(Len)) ==
              maskTrailingOnes<uint64_t>(Len))
        Value = Value.getOperand(0);
      CurDAG->SelectNodeTo(Node, IA64::DEP, MVT::i64,
                           {Value, Into.getOperand(0),
                            CurDAG->getTargetConstant(Pos, DL, MVT::i64),
                            CurDAG->getTargetConstant(Len, DL, MVT::i64), P0});
      return true;
    }
    return false;
  }
  }
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
  case ISD::AND:
  case ISD::OR:
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
  case ISD::SIGN_EXTEND_INREG:
    if (selectCompareChain(Node) || selectBitField(Node))
      return;
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
  case ISD::FDIV: {
    // The division sequence needs its own registers for intermediate steps.
    MVT VT = Node->getSimpleValueType(0);
    if (VT != MVT::f32 && VT != MVT::f64)
      break;
    SDNode *Div =
        CurDAG->getMachineNode(VT == MVT::f32 ? IA64::FDIV_S : IA64::FDIV_D, DL,
                               {VT, MVT::f80, MVT::f80, MVT::f80},
                               {Node->getOperand(0), Node->getOperand(1)});
    setNoFPExcept(Div);
    ReplaceNode(Node, Div);
    return;
  }
  case ISD::STRICT_FDIV: {
    // The same, but chained with other operations that may raise exceptions.
    MVT VT = Node->getSimpleValueType(0);
    if (VT != MVT::f32 && VT != MVT::f64)
      break;
    SDNode *Div = CurDAG->getMachineNode(
        VT == MVT::f32 ? IA64::FDIV_S : IA64::FDIV_D, DL,
        {VT, MVT::f80, MVT::f80, MVT::f80, MVT::Other},
        {Node->getOperand(1), Node->getOperand(2), Node->getOperand(0)});
    Div->setFlags(Node->getFlags());
    ReplaceUses(SDValue(Node, 0), SDValue(Div, 0));
    ReplaceUses(SDValue(Node, 1), SDValue(Div, 4));
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS: {
    // The relational compares (lt, le, ...) raise invalid on unordered
    // operands; eq and ord/unord raise it only for signaling NaNs. Use the
    // quiet or signaling variant when the strict node requires the other
    // behavior.
    IA64CC::CondCode CC =
        getIA64FPCC(cast<CondCodeSDNode>(Node->getOperand(3))->get());
    bool Signals = CC >= IA64CC::OLT && CC <= IA64CC::ULT;
    unsigned Opcode = IA64::SETCCf;
    if (Node->getOpcode() == ISD::STRICT_FSETCC && Signals)
      Opcode = IA64::SETCCfq;
    else if (Node->getOpcode() == ISD::STRICT_FSETCCS && !Signals)
      Opcode = IA64::SETCCfs;
    SDNode *Compare = CurDAG->getMachineNode(
        Opcode, DL, {MVT::i64, MVT::Other},
        {CurDAG->getTargetConstant(CC, DL, MVT::i64), Node->getOperand(1),
         Node->getOperand(2), Node->getOperand(0)});
    Compare->setFlags(Node->getFlags());
    ReplaceUses(SDValue(Node, 0), SDValue(Compare, 0));
    ReplaceUses(SDValue(Node, 1), SDValue(Compare, 1));
    CurDAG->RemoveDeadNode(Node);
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
  case ISD::LOAD:
  case ISD::STORE: {
    // Post-increment forms. Narrow loads zero-extend, and narrow stores write
    // the low bits.
    const auto *Mem = cast<LSBaseSDNode>(Node);
    if (Mem->getAddressingMode() != ISD::POST_INC)
      break;
    bool IsLoad = Node->getOpcode() == ISD::LOAD;
    MVT MemVT = Mem->getMemoryVT().getSimpleVT();
    SDValue Offset = Mem->getOffset();
    bool IsImm = isa<ConstantSDNode>(Offset);
    if (IsImm)
      Offset = CurDAG->getSignedTargetConstant(
          cast<ConstantSDNode>(Offset)->getSExtValue(), DL, MVT::i64);
    unsigned Opcode;
    switch (MemVT.SimpleTy) {
    default:
      llvm_unreachable("an access that cannot add to its address");
#define ACCESS(VT, LOAD, STORE)                                                \
  case MVT::VT:                                                                \
    Opcode = !IsLoad ? IA64::STORE##_i                                         \
             : IsImm ? IA64::LOAD##_i                                          \
                     : IA64::LOAD##_r;                                         \
    break;
      ACCESS(i8, LD1, ST1)
      ACCESS(i16, LD2, ST2)
      ACCESS(i32, LD4, ST4)
      ACCESS(i64, LD8, ST8)
      ACCESS(f32, LDFS, STFS)
      ACCESS(f64, LDFD, STFD)
      ACCESS(f80, LDFE, STFE)
#undef ACCESS
    }
    SDValue Hint = CurDAG->getTargetConstant(0, DL, MVT::i64);
    SDValue P0 = CurDAG->getRegister(IA64::P0, MVT::i1);
    MachineSDNode *Access;
    if (IsLoad) {
      assert(cast<LoadSDNode>(Node)->getExtensionType() != ISD::SEXTLOAD &&
             "no load extends the sign");
      Access = CurDAG->getMachineNode(
          Opcode, DL, Node->getValueType(0), MVT::i64, MVT::Other,
          {Mem->getBasePtr(), Offset, Hint, P0, Mem->getChain()});
    } else {
      Access = CurDAG->getMachineNode(Opcode, DL, MVT::i64, MVT::Other,
                                      {Mem->getBasePtr(),
                                       cast<StoreSDNode>(Node)->getValue(),
                                       Offset, Hint, P0, Mem->getChain()});
    }
    CurDAG->setNodeMemRefs(Access, {Mem->getMemOperand()});
    ReplaceNode(Node, Access);
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
    SDNode *New = CurDAG->SelectNodeTo(Node, Opcodes[Form], MVT::i64, Ops);
    if (Form == FloatingPoint)
      setNoFPExcept(New);
    return;
  }
  case ISD::BR_CC: {
    static const unsigned Opcodes[] = {IA64::BRCC, IA64::BRCCi, IA64::BRCCf};
    SmallVector<SDValue, 5> Ops = {Node->getOperand(4)};
    CompareForm Form = selectCompare(
        Node->getOperand(2), Node->getOperand(3),
        cast<CondCodeSDNode>(Node->getOperand(1))->get(), DL, Ops);
    Ops.push_back(Node->getOperand(0));
    SDNode *New = CurDAG->SelectNodeTo(Node, Opcodes[Form], MVT::Other, Ops);
    if (Form == FloatingPoint)
      setNoFPExcept(New);
    return;
  }
  case ISD::SELECT_CC: {
    static const unsigned Opcodes[][3] = {
        {IA64::SELECT, IA64::SELECTi, IA64::SELECTf},
        {IA64::SELECT_FR, IA64::SELECT_FRi, IA64::SELECT_FRf}};
    MVT VT = Node->getSimpleValueType(0);
    if (selectMinMax(Node) || selectSelectChain(Node))
      return;
    SmallVector<SDValue, 6> Ops = {Node->getOperand(3), Node->getOperand(2)};
    CompareForm Form = selectCompare(
        Node->getOperand(0), Node->getOperand(1),
        cast<CondCodeSDNode>(Node->getOperand(4))->get(), DL, Ops);
    SDNode *New = CurDAG->SelectNodeTo(
        Node, Opcodes[VT.isFloatingPoint()][Form], VT, Ops);
    if (Form == FloatingPoint)
      setNoFPExcept(New);
    return;
  }
  }

  SelectCode(Node);
}

// A sign-extended field shifted right differs from the same field shifted
// arithmetically only in the top bits, which are zero in one and copies of
// the sign in the other. When every user is an AND whose other operand has
// those bits clear, the combiner prefers the logical shift, but only the
// arithmetic one is a single extr.
void IA64DAGToDAGISel::PreprocessISelDAG() {
  bool Changed = false;
  for (SDNode &Node : make_early_inc_range(CurDAG->allnodes())) {
    unsigned Amount;
    if (Node.getValueType(0) != MVT::i64 ||
        !isShiftBy(SDValue(&Node, 0), ISD::SRL, Amount) || Amount == 0)
      continue;
    SDValue Field = Node.getOperand(0);
    if (Field.getOpcode() != ISD::SIGN_EXTEND_INREG ||
        Amount >= cast<VTSDNode>(Field.getOperand(1))->getVT().getSizeInBits())
      continue;
    APInt Top = APInt::getHighBitsSet(64, Amount);
    auto Masks = [&](const SDUse &U) {
      if (U.getUser()->getOpcode() != ISD::AND)
        return false;
      SDValue Other = U.getUser()->getOperand(1 - U.getOperandNo());
      return Top.isSubsetOf(CurDAG->computeKnownBits(Other).Zero);
    };
    if (!all_of(Node.uses(), Masks))
      continue;
    SDValue Shift = CurDAG->getNode(ISD::SRA, SDLoc(&Node), MVT::i64, Field,
                                    Node.getOperand(1));
    CurDAG->ReplaceAllUsesOfValueWith(SDValue(&Node, 0), Shift);
    Changed = true;
  }
  if (Changed)
    CurDAG->RemoveDeadNodes();
}

// An LTOFF load of an address from the linkage table can be relaxed by the
// linker when the target is within reach of GP, if it knows the symbol: the
// add that formed the entry address then forms the symbol address, and the
// load becomes a move. The linker must be free to rewrite both instructions,
// so the add must have a single use.
void IA64DAGToDAGISel::PostprocessISelDAG() {
  bool Changed = false;
  for (SDNode &Node : make_early_inc_range(CurDAG->allnodes())) {
    if (!Node.isMachineOpcode() || Node.getMachineOpcode() != IA64::LD8)
      continue;
    SDNode *Add = Node.getOperand(0).getNode();
    if (!Add->isMachineOpcode() || Add->getMachineOpcode() != IA64::ADDL ||
        !Add->hasOneUse())
      continue;
    SDLoc DL(&Node);
    SDValue Entry = Add->getOperand(0);
    std::function<SDValue(unsigned)> Symbol;
    if (const auto *GA = dyn_cast<GlobalAddressSDNode>(Entry))
      Symbol = [=](unsigned Flags) {
        return CurDAG->getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                              GA->getOffset(), Flags);
      };
    else if (const auto *JT = dyn_cast<JumpTableSDNode>(Entry))
      Symbol = [=](unsigned Flags) {
        return CurDAG->getTargetJumpTable(JT->getIndex(), MVT::i64, Flags);
      };
    else if (const auto *BA = dyn_cast<BlockAddressSDNode>(Entry))
      Symbol = [=](unsigned Flags) {
        return CurDAG->getTargetBlockAddress(BA->getBlockAddress(), MVT::i64,
                                             BA->getOffset(), Flags);
      };
    else
      continue;
    if (Symbol(IA64::S_LTOFF) != Entry)
      continue;
    SmallVector<SDValue, 4> AddOps(Add->ops());
    AddOps[0] = Symbol(IA64::S_LTOFFX);
    SDNode *NewAdd = CurDAG->getMachineNode(IA64::ADDL, SDLoc(Add),
                                            Add->getVTList(), AddOps);
    SmallVector<SDValue, 4> LoadOps(Node.ops());
    LoadOps[0] = SDValue(NewAdd, 0);
    LoadOps[1] = Symbol(IA64::S_None);
    MachineSDNode *NewLoad =
        CurDAG->getMachineNode(IA64::LD8_MOV, DL, Node.getVTList(), LoadOps);
    CurDAG->setNodeMemRefs(NewLoad, cast<MachineSDNode>(Node).memoperands());
    ReplaceUses(&Node, NewLoad);
    Changed = true;
  }
  if (Changed)
    CurDAG->RemoveDeadNodes();
}

// fmin is "f2 < f3 ? f2 : f3" and fmax is "f3 < f2 ? f2 : f3", so each
// returns its second operand for unordered or equal inputs (including
// zeroes of either sign). They therefore match a select on a compare that is
// false for equal and unordered inputs, or on its inverse with the select
// operands swapped.
bool IA64DAGToDAGISel::selectMinMax(SDNode *Node) {
  SDValue Lesser = Node->getOperand(0), Greater = Node->getOperand(1);
  SDValue True = Node->getOperand(2), False = Node->getOperand(3);
  MVT VT = Node->getSimpleValueType(0);
  if (!VT.isFloatingPoint() || Lesser.getValueType() != VT)
    return false;

  // Canonicalize to "Lesser < Greater ? True : False".
  switch (cast<CondCodeSDNode>(Node->getOperand(4))->get()) {
  case ISD::SETOLT:
  case ISD::SETLT:
    break;
  case ISD::SETOGT:
  case ISD::SETGT:
    std::swap(Lesser, Greater);
    break;
  case ISD::SETULE:
  case ISD::SETLE:
    std::swap(Lesser, Greater);
    std::swap(True, False);
    break;
  case ISD::SETUGE:
  case ISD::SETGE:
    std::swap(True, False);
    break;
  default:
    return false;
  }

  unsigned Opcode;
  if (True == Lesser && False == Greater)
    Opcode = IA64::FMIN;
  else if (True == Greater && False == Lesser)
    Opcode = IA64::FMAX;
  else
    return false;
  SDLoc DL(Node);
  setNoFPExcept(CurDAG->SelectNodeTo(
      Node, Opcode, VT,
      {True, False, CurDAG->getTargetConstant(0, DL, MVT::i64),
       CurDAG->getRegister(IA64::P0, MVT::i1)}));
  return true;
}

FunctionPass *llvm::createIA64ISelDag(IA64TargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new IA64DAGToDAGISelLegacy(TM, OptLevel);
}

//===-- IA64ExclusivePredicates.cpp - Predicates that exclude each other --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ExclusivePredicates.h"
#include "IA64InstrInfo.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
    EnableMutation("ia64-exclusive-predicate-scheduling", cl::Hidden,
                   cl::init(true),
                   cl::desc("Let instructions under predicates that exclude "
                            "each other run in either order"));

// Compares that write the complement of their first target to the second.
// The .unc forms write both even under a false qualifying predicate,
// clearing them.
static bool isExclusiveCompare(unsigned Opcode, bool &Unc) {
  switch (Opcode) {
  default:
    return false;
  case IA64::CMP_LT:
  case IA64::CMP_LTi:
  case IA64::CMP_LTU:
  case IA64::CMP_LTUi:
  case IA64::CMP_EQ:
  case IA64::CMP_EQi:
  case IA64::CMP4_LT:
  case IA64::CMP4_LTi:
  case IA64::CMP4_LTU:
  case IA64::CMP4_LTUi:
  case IA64::CMP4_EQ:
  case IA64::CMP4_EQi:
  case IA64::TBIT_Z:
  case IA64::TNAT_Z:
  case IA64::FCMP_EQ:
  case IA64::FCMP_LT:
  case IA64::FCMP_LE:
  case IA64::FCMP_UNORD:
  case IA64::FCLASS_M:
    Unc = false;
    return true;
  case IA64::CMP_LT_UNC:
  case IA64::CMP_LT_UNCi:
  case IA64::CMP_LTU_UNC:
  case IA64::CMP_LTU_UNCi:
  case IA64::CMP_EQ_UNC:
  case IA64::CMP_EQ_UNCi:
  case IA64::CMP4_LT_UNC:
  case IA64::CMP4_LT_UNCi:
  case IA64::CMP4_LTU_UNC:
  case IA64::CMP4_LTU_UNCi:
  case IA64::CMP4_EQ_UNC:
  case IA64::CMP4_EQ_UNCi:
  case IA64::TBIT_Z_UNC:
  case IA64::TNAT_Z_UNC:
  case IA64::FCMP_EQ_UNC:
  case IA64::FCMP_LT_UNC:
  case IA64::FCMP_LE_UNC:
  case IA64::FCMP_UNORD_UNC:
  case IA64::FCLASS_M_UNC:
    Unc = true;
    return true;
  }
}

IA64ExclusivePredicates::IA64ExclusivePredicates(const TargetRegisterInfo &TRI)
    : Entries(TRI.getNumRegs()) {}

void IA64ExclusivePredicates::clear() {
  std::fill(Entries.begin(), Entries.end(), Entry());
}

void IA64ExclusivePredicates::step(const MachineInstr &MI) {
  // These write predicates without naming them.
  if (MI.isCall() || MI.isInlineAsm() || MI.getOpcode() == IA64::MOV_PR ||
      MI.getOpcode() == IA64::MOV_PR_ROT) {
    clear();
    return;
  }
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
      continue;
    Entry &E = Entries[MO.getReg().id()];
    if (E.Partner)
      Entries[E.Partner.id()] = Entry();
    E = Entry();
  }
  // A compare under a false predicate other than .unc writes nothing.
  bool Unc;
  if (!isExclusiveCompare(MI.getOpcode(), Unc))
    return;
  int QPIdx = MI.findFirstPredOperandIdx();
  if (!Unc && QPIdx >= 0 && MI.getOperand(QPIdx).getReg() != IA64::P0)
    return;
  MCRegister P1 = MI.getOperand(0).getReg(), P2 = MI.getOperand(1).getReg();
  if (P1 == IA64::P0 || P2 == IA64::P0 || P1 == P2)
    return;
  ++NumCompares;
  Entries[P1.id()] = {P2, NumCompares};
  Entries[P2.id()] = {P1, NumCompares};
}

MCRegister
IA64ExclusivePredicates::qualifyingPredicate(const MachineInstr &MI) {
  bool Unc;
  int QPIdx = MI.findFirstPredOperandIdx();
  if (QPIdx < 0 || MI.isCall() || MI.isBranch() || MI.isReturn() ||
      MI.isInlineAsm() ||
      IA64II::getUnit(MI.getDesc().TSFlags) == IA64II::UnitB ||
      (isExclusiveCompare(MI.getOpcode(), Unc) && Unc))
    return IA64::P0;
  return MI.getOperand(QPIdx).getReg();
}

bool IA64ExclusivePredicates::writesComplement(const MachineInstr &MI) {
  bool Unc;
  int QPIdx = MI.findFirstPredOperandIdx();
  return isExclusiveCompare(MI.getOpcode(), Unc) && QPIdx >= 0 &&
         MI.getOperand(QPIdx).getReg() == IA64::P0;
}

namespace {

class ExclusivePredicatesMutation : public ScheduleDAGMutation {
public:
  void apply(ScheduleDAGInstrs *DAG) override;
};

} // end anonymous namespace

void ExclusivePredicatesMutation::apply(ScheduleDAGInstrs *DAG) {
  if (!EnableMutation || DAG->SUnits.empty())
    return;
  MachineBasicBlock &MBB = *DAG->begin()->getParent();
  IA64ExclusivePredicates Exclusive(*DAG->TRI);

  // The qualifying predicate of each instruction of the region whose
  // predicate excludes another, and the compare that made the pair.
  struct Qualifier {
    MCRegister QP;
    unsigned Compare;
  };
  DenseMap<const MachineInstr *, Qualifier> Qualifiers;
  for (MachineInstr &MI : make_range(MBB.begin(), DAG->end())) {
    MCRegister QP = IA64ExclusivePredicates::qualifyingPredicate(MI);
    if (QP != IA64::P0 && Exclusive.partner(QP) &&
        !MI.hasUnmodeledSideEffects() && !MI.hasOrderedMemoryRef())
      Qualifiers[&MI] = {QP, Exclusive.compare(QP)};
    Exclusive.step(MI);
  }
  if (Qualifiers.empty())
    return;

  // Whether at most one of A and B takes effect, so that an edge between
  // them for Reg can go. An edge for one of the predicates themselves stays:
  // an instruction under one that writes the other ends the pair, and the
  // instructions before it must read the predicate first.
  auto ExcludeEachOther = [&](const SUnit *A, const SUnit *B, Register Reg) {
    if (A->isBoundaryNode() || B->isBoundaryNode())
      return false;
    auto IA = Qualifiers.find(A->getInstr()),
         IB = Qualifiers.find(B->getInstr());
    if (IA == Qualifiers.end() || IB == Qualifiers.end() ||
        IA->second.Compare != IB->second.Compare ||
        IA->second.QP == IB->second.QP)
      return false;
    return !Reg || (!DAG->TRI->regsOverlap(Reg, IA->second.QP) &&
                    !DAG->TRI->regsOverlap(Reg, IB->second.QP));
  };

  // An edge from X to Y may stand for others that X hides: if X writes a
  // register, Y has edges only to X, not to the accesses before X, and the
  // accesses after Y have edges only to Y. So in place of each dropped
  // edge, the edges from X's predecessors to Y and from X to Y's successors
  // that concern the same register or memory are added, or dropped in turn.
  // Those come from the graph as built, since the edges that link them may
  // themselves be dropped.
  struct Edge {
    SUnit *X, *Y;
    SDep::Kind Kind;
    Register Reg;
  };
  auto Writes = [](SDep::Kind K, bool Head) {
    // Data: the head writes, the tail reads; anti: the reverse; output:
    // both write.
    return K == SDep::Output || (K == SDep::Data) == Head;
  };
  // The kind of an edge from an access that writes or reads to one that
  // writes or reads, if any.
  auto KindOf = [](bool HeadWrites,
                   bool TailWrites) -> std::optional<SDep::Kind> {
    if (HeadWrites && TailWrites)
      return SDep::Output;
    if (HeadWrites)
      return SDep::Data;
    if (TailWrites)
      return SDep::Anti;
    return std::nullopt;
  };
  auto Concerns = [&](const SDep &D, const Edge &E) {
    if (E.Kind == SDep::Order)
      return D.getKind() == SDep::Order && !D.isArtificial();
    return D.getKind() != SDep::Order &&
           DAG->TRI->regsOverlap(D.getReg(), E.Reg);
  };

  std::vector<SmallVector<SDep, 4>> Preds, Succs;
  for (const SUnit &SU : DAG->SUnits) {
    Preds.emplace_back(SU.Preds.begin(), SU.Preds.end());
    Succs.emplace_back(SU.Succs.begin(), SU.Succs.end());
  }

  SmallVector<Edge, 16> Work;
  SmallSet<std::tuple<SUnit *, SUnit *, unsigned, unsigned>, 16> Seen;
  auto Push = [&](SUnit *X, SUnit *Y, SDep::Kind K, Register R) {
    if (Seen.insert({X, Y, K, R.id()}).second)
      Work.push_back({X, Y, K, R});
  };
  for (SUnit &SU : DAG->SUnits) {
    SmallVector<SDep, 4> Dropped;
    for (const SDep &Pred : SU.Preds)
      if (!Pred.isArtificial() && !Pred.isWeak() &&
          ExcludeEachOther(&SU, Pred.getSUnit(),
                           Pred.getKind() == SDep::Order ? Register()
                                                         : Pred.getReg()))
        Dropped.push_back(Pred);
    for (const SDep &Pred : Dropped) {
      SU.removePred(Pred);
      Push(Pred.getSUnit(), &SU, Pred.getKind(),
           Pred.getKind() == SDep::Order ? Register() : Pred.getReg());
    }
  }

  auto Add = [&](SUnit *From, SUnit *To, const Edge &E, bool FromWrites,
                 bool ToWrites, unsigned Latency) {
    if (From == To || From->isBoundaryNode() || To->isBoundaryNode())
      return;
    SDep::Kind K = SDep::Order;
    if (E.Kind != SDep::Order) {
      std::optional<SDep::Kind> Kind = KindOf(FromWrites, ToWrites);
      if (!Kind)
        return;
      K = *Kind;
    }
    if (ExcludeEachOther(From, To, E.Reg)) {
      Push(From, To, K, E.Reg);
      return;
    }
    SDep D =
        K == SDep::Order ? SDep(From, SDep::Barrier) : SDep(From, K, E.Reg);
    if (K == SDep::Data) {
      const MachineInstr *Def = From->getInstr(), *Use = To->getInstr();
      int DefIdx = Def->findRegisterDefOperandIdx(E.Reg, DAG->TRI, false, true);
      int UseIdx = Use->findRegisterUseOperandIdx(E.Reg, DAG->TRI);
      Latency = DefIdx >= 0 && UseIdx >= 0
                    ? DAG->getSchedModel()->computeOperandLatency(Def, DefIdx,
                                                                  Use, UseIdx)
                    : Latency;
    }
    D.setLatency(Latency);
    To->addPred(D);
  };
  while (!Work.empty()) {
    Edge E = Work.pop_back_val();
    bool XWrites = E.Kind == SDep::Order || Writes(E.Kind, true);
    bool YWrites = E.Kind == SDep::Order || Writes(E.Kind, false);
    for (const SDep &D : Preds[E.X->NodeNum])
      if (Concerns(D, E))
        Add(D.getSUnit(), E.Y, E, Writes(D.getKind(), true), YWrites,
            D.getLatency());
    for (const SDep &D : Succs[E.Y->NodeNum])
      if (Concerns(D, E))
        Add(E.X, D.getSUnit(), E, XWrites, Writes(D.getKind(), false),
            D.getLatency());
  }
}

std::unique_ptr<ScheduleDAGMutation>
llvm::createIA64ExclusivePredicatesMutation() {
  return std::make_unique<ExclusivePredicatesMutation>();
}

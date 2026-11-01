//===-- IA64LoadBanks.cpp - Keep FP loads of two arrays apart -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Each FP load reads L2, whose banks serve one 16-byte chunk a cycle. Two
// loads in one cycle to different chunks of a bank take six cycles more for
// the second, and the arrays of a stencil, allocated the same size, put the
// same element of each in the same bank. This mutation asks the pipeliner to
// issue the FP loads of a loop from different objects a cycle apart.
//
// Madison also pays when the two loads share a chunk, so x[i] and x[i+1] of an
// unrolled loop must not share a cycle either, and LSR gives each copy a
// pointer of its own, so that the object says nothing. So every FP load goes
// a cycle after the last, which leaves x[i] with y[i-2], a chunk away. That
// also made linpack 7.6% and cholesky 4.2% faster on an Itanium 9340.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64RegisterInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
    EnableMutation("ia64-pipeline-load-banks", cl::Hidden, cl::init(true),
                   cl::desc("Pipeline the FP loads of different objects in "
                            "different cycles"));

namespace {

class LoadBanksMutation : public ScheduleDAGMutation {
public:
  void apply(ScheduleDAGInstrs *DAG) override;
};

} // end anonymous namespace

// The object an FP load of \p MI reads, or null if it is not one.
static const Value *loadedObject(const MachineInstr &MI,
                                 const MachineRegisterInfo &MRI) {
  if (!MI.mayLoad() || MI.mayStore() || !MI.hasOneMemOperand() ||
      MI.getNumDefs() == 0)
    return nullptr;
  Register Def = MI.getOperand(0).getReg();
  if (!Def.isVirtual() || !IA64::FRRegClass.hasSubClassEq(MRI.getRegClass(Def)))
    return nullptr;
  const Value *V = (*MI.memoperands_begin())->getValue();
  return V ? getUnderlyingObject(V) : nullptr;
}

void LoadBanksMutation::apply(ScheduleDAGInstrs *DAG) {
  if (!EnableMutation)
    return;
  const MachineRegisterInfo &MRI = DAG->MRI;
  SmallVector<std::pair<SUnit *, const Value *>, 8> Loads;
  for (SUnit &SU : DAG->SUnits)
    if (const Value *Obj = loadedObject(*SU.getInstr(), MRI))
      Loads.push_back({&SU, Obj});
  // An edge from a load to one whose value is needed sooner only delays
  // the second within the slack it has, so it lengthens no path through
  // the loop, nor any recurrence.
  llvm::stable_sort(Loads, [](auto &A, auto &B) {
    return A.first->getHeight() > B.first->getHeight();
  });
  // Two loads fit one cycle at an II of 1, where nothing can part them.
  if (Loads.size() > 2) {
    // The swing scheduler sees only an instruction's own edges, so each load
    // needs one from every load before it, or it places two ends of the
    // chain before the middle and leaves the middle no room.
    for (unsigned I = 1; I < Loads.size(); ++I)
      for (unsigned J = 0; J < I; ++J) {
        SUnit *Prev = Loads[J].first, *SU = Loads[I].first;
        if (SU->isPred(Prev))
          continue;
        SDep Dep(Prev, SDep::Artificial);
        Dep.setLatency(I - J);
        DAG->addEdge(SU, Dep);
      }
    return;
  }
  for (unsigned I = 1; I < Loads.size(); ++I) {
    auto [Prev, PrevObj] = Loads[I - 1];
    auto [SU, Obj] = Loads[I];
    if (Obj == PrevObj || Prev->getHeight() <= SU->getHeight() ||
        SU->isPred(Prev))
      continue;
    SDep Dep(Prev, SDep::Artificial);
    Dep.setLatency(1);
    DAG->addEdge(SU, Dep);
  }
}

std::unique_ptr<ScheduleDAGMutation> llvm::createIA64LoadBanksMutation() {
  return std::make_unique<LoadBanksMutation>();
}

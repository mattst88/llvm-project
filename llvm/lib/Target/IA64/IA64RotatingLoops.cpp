//===-- IA64RotatingLoops.cpp - Kernel-only software pipelining -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Expands a modulo schedule into its kernel alone, as IA-64 was designed to
// run it. Each instruction runs under the predicate of its stage, p16 for
// stage 0, p17 for stage 1 and so on, and br.ctop counts the loop down in
// ar.lc and then ar.ec, shifting a 1 into p16 while iterations are left to
// start and a 0 once they are not. So the first passes of the kernel are its
// prologue and the last S - 1 its epilogue, for any trip count.
//
// br.ctop also rotates r32 up to the sor'th register, f32-f127 and p16-p63:
// what one pass of the kernel writes to r32 the next reads as r33. A value
// that a later stage reads, or the next iteration through a phi, lives in
// rotating registers: its definition writes its base register, and a use k
// passes later reads base + k. The preheader puts a phi's initial value
// where the first iteration reads it, and the block the loop exits to
// copies values used after the loop out of the rotating registers and then
// clears the rotation with clrrrb.
//
// A value that only its own stage reads stays a virtual register, as does a
// post-increment address and the phi it is tied to, when only their stage
// uses them: the increment, predicated off, leaves the address as it was.
//
// The rotating registers are reserved for the whole function once a loop
// uses them, so nothing else is allocated to them.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ModuloSchedule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"

using namespace llvm;

#define DEBUG_TYPE "pipeliner"

STATISTIC(NumRotating, "Number of loops pipelined with rotating registers");
DEBUG_COUNTER(RotatingCounter, "ia64-rotating",
              "Controls which loops are pipelined with rotating registers");

static cl::opt<unsigned> RotatingMaxStages(
    "ia64-rotating-max-stages", cl::Hidden, cl::init(10),
    cl::desc("The last stage, counting from 0, that a loop pipelined with "
             "rotating registers may have"));

// A rotating loop runs its kernel once more for each stage after the first
// to drain, which a loop entered for only an iteration or two pays for, and
// the default expander's two-stage loops cost little.
static cl::opt<unsigned> RotatingMinStages(
    "ia64-rotating-min-stages", cl::Hidden, cl::init(3),
    cl::desc("The fewest stages a loop pipelined with rotating registers "
             "may have"));

// Rotating general registers leave the rest of the 88 locals to the rest of
// the function.
static const unsigned MaxRotatingGRs = 64;

namespace {

// A value that lives in rotating registers.
struct RotatingValue {
  enum { GR, FR, PR } Kind = GR;
  int Stage = 0;
  // The offsets from Base of the registers it is read from or initialized
  // in.
  int Lo = 0, Hi = 0;
  int Base = 0;
  // The phi it is the loop value of, and the phi's initial value.
  Register Phi;
  Register Init;
  bool LiveOut = false;
};

class RotatingExpander {
  MachineFunction &MF;
  const IA64InstrInfo &TII;
  const TargetRegisterInfo &TRI;
  MachineRegisterInfo &MRI;
  MachineBasicBlock *BB;
  MachineBasicBlock *Preheader = nullptr;
  MachineBasicBlock *Exit = nullptr;
  // Null before the loop is scheduled, when only what does not depend on
  // the schedule is checked.
  ModuloSchedule *MS;
  unsigned NumStages = 0;

  // Phi results and their loop values.
  DenseMap<Register, Register> LoopValueOf;
  DenseMap<Register, MachineInstr *> PhiOf;
  // Values defined in the loop that live in rotating registers.
  MapVector<Register, RotatingValue> Rotating;
  // Post-increment results and the phis they are tied to, which stay in
  // virtual registers.
  DenseSet<Register> Static;
  unsigned NumGRs = 0, NumFRs = 0, NumPRs = 0;

  int stage(const MachineInstr &MI) const {
    return MS->getStage(const_cast<MachineInstr *>(&MI));
  }
  bool inLoop(const MachineInstr &MI) const { return MI.getParent() == BB; }
  // Whether MI runs regardless of its stage's predicate.
  bool isUnguarded(const MachineInstr &MI) const;
  static bool isPure(const MachineInstr &MI) {
    return !MI.mayLoadOrStore() && !MI.hasUnmodeledSideEffects() &&
           !MI.isCall() && !MI.isBranch() && !MI.isInlineAsm();
  }
  bool checkInstruction(const MachineInstr &MI) const;
  bool classify();
  Register name(Register Reg, int Offset) const {
    const RotatingValue &V = Rotating.find(Reg)->second;
    switch (V.Kind) {
    case RotatingValue::GR:
      return IA64::R32 + V.Base + Offset;
    case RotatingValue::FR:
      return IA64::F32 + V.Base + Offset;
    case RotatingValue::PR:
      // Above the predicates of the stages.
      return IA64::P16 + NumStages + V.Base + Offset;
    }
    llvm_unreachable("unknown register kind");
  }
  void addRotatingOperands(MachineInstrBuilder &MIB, bool Uses);
  // Copy Src to the rotating register Dst in Stage.
  void guardedCopy(MachineBasicBlock::iterator I, const DebugLoc &DL,
                   Register Dst, Register Src, int Stage) {
    Register QP = IA64::P16 + Stage;
    if (IA64::FRRegClass.contains(Dst))
      BuildMI(*BB, I, DL, TII.get(IA64::FMERGE_S), Dst)
          .addReg(Src)
          .addReg(Src)
          .addReg(QP);
    else
      BuildMI(*BB, I, DL, TII.get(IA64::ADDS), Dst)
          .addImm(0)
          .addReg(Src)
          .addReg(QP);
  }

public:
  RotatingExpander(MachineBasicBlock *BB, ModuloSchedule *MS)
      : MF(*BB->getParent()),
        TII(*MF.getSubtarget<IA64Subtarget>().getInstrInfo()),
        TRI(*MF.getSubtarget().getRegisterInfo()), MRI(MF.getRegInfo()), BB(BB),
        MS(MS) {}

  bool canExpand();
  void expand(LiveIntervals &LIS);
};

} // end anonymous namespace

bool RotatingExpander::isUnguarded(const MachineInstr &MI) const {
  int QP = MI.findFirstPredOperandIdx();
  return QP < 0 || MI.getOperand(QP).getReg() != IA64::P0;
}

// What does not depend on the schedule: MI can run predicated by its stage,
// or harmlessly when its stage is not running.
bool RotatingExpander::checkInstruction(const MachineInstr &MI) const {
  if (MI.isCall() || MI.isInlineAsm() || MI.isDebugInstr())
    return MI.isDebugInstr();
  if (MI.isCopy()) {
    // A copy may become a predicated move, which stays within a register
    // file.
    auto File = [&](const MachineOperand &MO) -> const TargetRegisterClass * {
      for (const TargetRegisterClass *RC :
           {&IA64::GRRegClass, &IA64::FRRegClass, &IA64::PRRegClass})
        if (MO.getReg().isVirtual()
                ? RC->hasSubClassEq(MRI.getRegClass(MO.getReg()))
                : RC->contains(MO.getReg()))
          return RC;
      return nullptr;
    };
    if (MI.getOperand(0).getSubReg() || MI.getOperand(1).getSubReg() ||
        !File(MI.getOperand(0)) ||
        File(MI.getOperand(0)) != File(MI.getOperand(1)))
      return false;
  }
  int QP = MI.findFirstPredOperandIdx();
  if (QP < 0) {
    // Copies and the other pseudos without a predicate run in every pass.
    if (!isPure(MI))
      return false;
  } else {
    Register P = MI.getOperand(QP).getReg();
    if (P != IA64::P0) {
      // A predicate the loop computes is stale when its stage is not
      // running, so what it guards runs too.
      if (!P.isVirtual() || MRI.getVRegDef(P)->getParent() != BB || !isPure(MI))
        return false;
    }
  }
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg.isPhysical()) {
      // The rotating registers are the loop's.
      if ((Reg >= IA64::R32 && Reg <= IA64::R127) ||
          (Reg >= IA64::F32 && Reg <= IA64::F127) ||
          (Reg >= IA64::P16 && Reg <= IA64::P63) || Reg == IA64::AR65 ||
          Reg == IA64::AR66)
        return false;
      if (MO.isDef() && !MRI.isReserved(Reg))
        return false;
      continue;
    }
    if (!MO.isDef())
      continue;
    const TargetRegisterClass *RC = MRI.getRegClass(Reg);
    if (!IA64::GRRegClass.hasSubClassEq(RC) &&
        !IA64::FRRegClass.hasSubClassEq(RC) &&
        !IA64::PRValueRegClass.hasSubClassEq(RC))
      return false;
    // A phi's loop value must not be written by a pass whose stage is not
    // running: the first passes would overwrite the phi's initial value.
    for (const MachineInstr &Use : MRI.use_nodbg_instructions(Reg))
      if (Use.isPHI() && inLoop(Use) && isUnguarded(MI) &&
          !(MI.isCopy() && IA64::GRRegClass.hasSubClassEq(RC)) &&
          !(MI.isCopy() && IA64::FRRegClass.hasSubClassEq(RC)))
        return false;
  }
  return true;
}

// Sort the values defined in the loop into those that stay in virtual
// registers and those that rotate, and give the rotating ones registers.
bool RotatingExpander::classify() {
  for (MachineInstr &Phi : BB->phis()) {
    Register Init, Loop;
    for (unsigned I = 1; I != Phi.getNumOperands(); I += 2)
      (Phi.getOperand(I + 1).getMBB() == BB ? Loop : Init) =
          Phi.getOperand(I).getReg();
    Register Res = Phi.getOperand(0).getReg();
    const TargetRegisterClass *RC = MRI.getRegClass(Res);
    if (!IA64::GRRegClass.hasSubClassEq(RC) &&
        !IA64::FRRegClass.hasSubClassEq(RC))
      return false;
    LoopValueOf[Res] = Loop;
    PhiOf[Loop] = &Phi;
  }
  if (!MS)
    return true;

  // A post-increment tied to its phi stays in a virtual register when only
  // its own stage uses the address before and after.
  for (auto [Res, Loop] : LoopValueOf) {
    MachineInstr *Def = MRI.getVRegDef(Loop);
    unsigned DefIdx = Def->findRegisterDefOperandIdx(Loop, /*TRI=*/nullptr);
    unsigned UseIdx;
    if (!Def->isRegTiedToUseOperand(DefIdx, &UseIdx) ||
        Def->getOperand(UseIdx).getReg() != Res || isUnguarded(*Def))
      continue;
    int S = stage(*Def);
    auto SameStage = [&](Register Reg) {
      return all_of(MRI.use_nodbg_instructions(Reg), [&](MachineInstr &Use) {
        return !inLoop(Use) || Use.isPHI() || stage(Use) == S;
      });
    };
    if (SameStage(Res) && SameStage(Loop)) {
      Static.insert(Res);
      Static.insert(Loop);
    }
  }

  for (MachineInstr *MI : MS->getInstructions()) {
    if (MI->isPHI())
      continue;
    int S = stage(*MI);
    for (const MachineOperand &MO : MI->defs()) {
      if (!MO.isReg() || !MO.getReg().isVirtual())
        continue;
      Register Reg = MO.getReg();
      if (Static.contains(Reg))
        continue;
      RotatingValue V;
      V.Stage = S;
      const TargetRegisterClass *RC = MRI.getRegClass(Reg);
      if (IA64::FRRegClass.hasSubClassEq(RC))
        V.Kind = RotatingValue::FR;
      else if (IA64::PRValueRegClass.hasSubClassEq(RC))
        V.Kind = RotatingValue::PR;
      bool Local = true;
      for (MachineOperand &Use : MRI.use_nodbg_operands(Reg)) {
        MachineInstr &User = *Use.getParent();
        if (!inLoop(User)) {
          V.LiveOut = true;
          V.Hi = std::max<int>(V.Hi, NumStages - S);
          Local = false;
        } else if (User.isPHI()) {
          Register Res = User.getOperand(0).getReg();
          V.Phi = Res;
          for (unsigned I = 1; I != User.getNumOperands(); I += 2)
            if (User.getOperand(I + 1).getMBB() != BB)
              V.Init = User.getOperand(I).getReg();
          Local = false;
          for (MachineInstr &PhiUser : MRI.use_nodbg_instructions(Res)) {
            int Offset = stage(PhiUser) - S + 1;
            if (Offset < 0)
              return false;
            V.Hi = std::max(V.Hi, Offset);
          }
          if (!MRI.use_nodbg_empty(Res))
            V.Lo = std::min(0, 1 - S);
        } else {
          int Offset = stage(User) - S;
          if (Offset < 0)
            return false;
          if (Offset)
            Local = false;
          V.Hi = std::max(V.Hi, Offset);
        }
      }
      if (Local)
        continue;
      // A predicate is only computed and used in the loop.
      if (V.Kind == RotatingValue::PR && (V.LiveOut || V.Phi || MI->isCopy()))
        return false;
      Rotating[Reg] = V;
    }
  }

  // A result tied to the last read of a rotating input can be written to
  // the register the input has rotated into by then, where it would
  // otherwise start as a copy of it. Its registers then follow the
  // input's.
  DenseMap<Register, Register> Follower;
  DenseSet<Register> Followers;
  DenseMap<const MachineInstr *, unsigned> Position;
  for (MachineInstr *MI : MS->getInstructions())
    Position[MI] = Position.size();
  for (MachineInstr *MI : MS->getInstructions()) {
    if (MI->isPHI())
      continue;
    int S = stage(*MI);
    for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
      const MachineOperand &Def = MI->getOperand(I);
      unsigned UseIdx;
      if (!Def.isReg() || !Def.isDef() ||
          !MI->isRegTiedToUseOperand(I, &UseIdx))
        continue;
      Register D = Def.getReg(), U = MI->getOperand(UseIdx).getReg();
      auto DI = Rotating.find(D), UI = Rotating.find(U);
      if (DI == Rotating.end() || UI == Rotating.end() || Follower.count(U) ||
          Followers.contains(D))
        continue;
      const RotatingValue &DV = DI->second, &UV = UI->second;
      if (DV.Kind != UV.Kind || DV.Lo != 0 || DV.Phi || UV.Phi || UV.LiveOut ||
          UV.Hi != S - UV.Stage)
        continue;
      // Nothing in the same pass may read the input after the result
      // overwrites it.
      if (any_of(MRI.use_nodbg_instructions(U), [&](MachineInstr &User) {
            return &User != MI && inLoop(User) && stage(User) == S &&
                   Position.lookup(&User) > Position.lookup(MI);
          }))
        continue;
      Follower[U] = D;
      Followers.insert(D);
    }
  }
  for (auto &[Reg, V] : Rotating) {
    if (Followers.contains(Reg))
      continue;
    unsigned &Num = V.Kind == RotatingValue::FR   ? NumFRs
                    : V.Kind == RotatingValue::PR ? NumPRs
                                                  : NumGRs;
    V.Base = Num - V.Lo;
    Num += V.Hi - V.Lo + 1;
    for (Register Prev = Reg; Follower.count(Prev);) {
      Register Next = Follower[Prev];
      RotatingValue &NV = Rotating.find(Next)->second;
      const RotatingValue &PV = Rotating.find(Prev)->second;
      NV.Base = PV.Base + PV.Hi;
      Num += NV.Hi;
      Prev = Next;
    }
  }
  if (NumGRs > MaxRotatingGRs || NumFRs > 96 || NumStages + NumPRs > 48)
    return false;

  // A phi result read by the stage of its loop value's definition, from
  // before that definition, must be read before the definition overwrites
  // the register in the same pass. The scheduler orders them so, but check.
  // And whatever runs unguarded must not write what a later pass reads from
  // a register the phi initialized.
  for (auto &[Reg, V] : Rotating)
    if (V.Phi && !V.Init)
      return false;
  return true;
}

bool RotatingExpander::canExpand() {
  if (MF.hasInlineAsm())
    return false;
  MachineBasicBlock::iterator Term = BB->getFirstTerminator();
  if (Term == BB->end() || Term->getOpcode() != IA64::BRLOOP ||
      BB->pred_size() != 2 || BB->succ_size() != 2)
    return false;
  for (MachineBasicBlock *P : BB->predecessors())
    if (P != BB)
      Preheader = P;
  for (MachineBasicBlock *S : BB->successors())
    if (S != BB)
      Exit = S;
  if (!Preheader || !Exit || Preheader->succ_size() != 1)
    return false;
  // The preheader sets ar.lc to one less than the trip count.
  bool SetsLC = false;
  for (MachineInstr &MI : reverse(*Preheader))
    if (MI.definesRegister(IA64::AR65, /*TRI=*/nullptr)) {
      SetsLC = MI.getOpcode() == IA64::SET_LC;
      break;
    }
  if (!SetsLC)
    return false;
  for (MachineInstr &MI : *BB)
    if (!MI.isPHI() && !MI.isTerminator() && !checkInstruction(MI))
      return false;
  // The phis are as the MVE expander wants them.
  for (MachineInstr &Phi : BB->phis())
    for (MachineInstr &Use :
         MRI.use_nodbg_instructions(Phi.getOperand(0).getReg()))
      if (!inLoop(Use) || Use.isPHI())
        return false;
  DenseSet<Register> LoopValues;
  for (MachineInstr &Phi : BB->phis())
    for (unsigned I = 1; I != Phi.getNumOperands(); I += 2) {
      if (Phi.getOperand(I + 1).getMBB() != BB)
        continue;
      Register Loop = Phi.getOperand(I).getReg();
      if (!Loop.isVirtual() || MRI.getVRegDef(Loop)->getParent() != BB ||
          MRI.getVRegDef(Loop)->isPHI() || !LoopValues.insert(Loop).second)
        return false;
    }
  if (MS) {
    if (MS->getNumStages() - 1 > (int)RotatingMaxStages)
      return false;
    NumStages = MS->getNumStages();
  }
  return classify();
}

void RotatingExpander::addRotatingOperands(MachineInstrBuilder &MIB,
                                           bool Uses) {
  // br.ctop and clrrrb rename all of f32-f127 and p16-p63, so what else
  // lives in them does not survive the loop.
  for (unsigned I = 0; I != NumGRs; ++I) {
    MIB.addReg(IA64::R32 + I, RegState::ImplicitDefine);
    if (Uses)
      MIB.addReg(IA64::R32 + I, RegState::Implicit);
  }
  for (unsigned I = 0; I != 96; ++I) {
    bool Used = I < NumFRs;
    MIB.addReg(IA64::F32 + I, Used ? RegState::ImplicitDefine
                                   : RegState::ImplicitDefine | RegState::Dead);
    if (Uses && Used)
      MIB.addReg(IA64::F32 + I, RegState::Implicit);
  }
  for (unsigned I = 0; I != 48; ++I) {
    MIB.addReg(IA64::P16 + I, RegState::ImplicitDefine);
    if (Uses && I < NumStages + NumPRs)
      MIB.addReg(IA64::P16 + I, RegState::Implicit);
  }
}

void RotatingExpander::expand(LiveIntervals &LIS) {
  LLVM_DEBUG(dbgs() << "Rotating expansion of " << printMBBReference(*BB)
                    << ": " << NumStages << " stages, " << NumGRs << " GRs, "
                    << NumFRs << " FRs, " << NumPRs << " PRs\n");
  DebugLoc DL = BB->findBranchDebugLoc();
  SmallVector<Register, 32> Touched;
  for (MachineInstr &MI : *BB) {
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.getReg().isVirtual())
        Touched.push_back(MO.getReg());
    LIS.RemoveMachineInstrFromMaps(MI);
  }

  // The registers whose names the function's loops rotate.
  auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  FI->addRotating(alignTo(NumGRs, 8), NumFRs);
  MRI.freezeReservedRegs();

  // The phis whose values the loop reads, before their uses are renamed.
  DenseSet<Register> UsedPhis;
  for (MachineInstr &Phi : BB->phis())
    if (!MRI.use_nodbg_empty(Phi.getOperand(0).getReg()))
      UsedPhis.insert(Phi.getOperand(0).getReg());

  // Rewrite the operands of the kernel, in schedule order.
  SmallVector<MachineInstr *, 64> Order;
  for (MachineInstr *MI : MS->getInstructions())
    if (!MI->isPHI())
      Order.push_back(MI);
  for (MachineInstr *MI : Order) {
    MI->removeFromParent();
    BB->insert(BB->getFirstTerminator(), MI);
  }
  for (MachineInstr &MI : make_early_inc_range(*BB))
    if (MI.isDebugInstr() || MI.isTerminator())
      MI.eraseFromParent();

  for (MachineInstr *MI : Order) {
    int S = stage(*MI);
    for (MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.getReg().isVirtual())
        continue;
      Register Reg = MO.getReg();
      if (MO.isDef()) {
        if (Rotating.count(Reg)) {
          MO.setReg(name(Reg, 0));
          MO.setIsDead(false);
        }
        continue;
      }
      MO.setIsKill(false);
      if (Rotating.count(Reg)) {
        MO.setReg(name(Reg, S - Rotating[Reg].Stage));
      } else if (auto It = LoopValueOf.find(Reg);
                 It != LoopValueOf.end() && Rotating.count(It->second)) {
        Register Loop = It->second;
        MO.setReg(name(Loop, S - Rotating[Loop].Stage + 1));
      }
    }
    // A result tied to an input starts as a copy of the input: a rotating
    // result in its own register, a virtual one in a new virtual register.
    for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
      MachineOperand &Def = MI->getOperand(I);
      unsigned UseIdx;
      if (!Def.isReg() || !Def.isDef() ||
          !MI->isRegTiedToUseOperand(I, &UseIdx))
        continue;
      MachineOperand &Use = MI->getOperand(UseIdx);
      if (Use.getReg() == Def.getReg())
        continue;
      if (Def.getReg().isVirtual()) {
        if (Use.getReg().isVirtual())
          continue;
        Register Copy =
            MRI.createVirtualRegister(MRI.getRegClass(Def.getReg()));
        BuildMI(*BB, MI, DL, TII.get(IA64::COPY), Copy).addReg(Use.getReg());
        Touched.push_back(Copy);
        Use.setReg(Copy);
        continue;
      }
      guardedCopy(MI, DL, Def.getReg(), Use.getReg(), S);
      Use.setReg(Def.getReg());
    }
    int QP = MI->findFirstPredOperandIdx();
    if (QP >= 0 && MI->getOperand(QP).getReg() == IA64::P0) {
      MI->getOperand(QP).setReg(IA64::P16 + S);
    } else if (MI->isCopy() && MI->getOperand(0).getReg().isPhysical()) {
      // A copy to a rotating register runs only in its stage.
      guardedCopy(MI, MI->getDebugLoc(), MI->getOperand(0).getReg(),
                  MI->getOperand(1).getReg(), S);
      MI->eraseFromParent();
    }
  }

  // The phis that rotate start in the preheader.
  MachineBasicBlock::iterator PI = Preheader->getFirstTerminator();
  SmallVector<MachineInstr *, 16> NewMIs;
  for (MachineInstr &Phi : make_early_inc_range(BB->phis())) {
    Register Res = Phi.getOperand(0).getReg();
    if (Static.contains(Res))
      continue;
    const RotatingValue &V = Rotating[LoopValueOf[Res]];
    if (UsedPhis.contains(Res))
      NewMIs.push_back(BuildMI(*Preheader, PI, DL, TII.get(IA64::COPY),
                               name(LoopValueOf[Res], 1 - V.Stage))
                           .addReg(V.Init));
    Phi.eraseFromParent();
  }
  NewMIs.push_back(
      BuildMI(*Preheader, PI, DL, TII.get(IA64::MOV_ARi), IA64::AR66)
          .addImm(NumStages)
          .addReg(IA64::P0));
  MachineInstrBuilder Rot =
      BuildMI(*Preheader, PI, DL, TII.get(IA64::MOV_PR_ROT))
          .addImm(1 << 16)
          .addReg(IA64::P0);
  for (unsigned Reg = IA64::P16; Reg <= IA64::P63; ++Reg)
    Rot.addReg(Reg, RegState::ImplicitDefine);
  NewMIs.push_back(Rot);

  // The kernel branches back to itself and falls through to a new exit
  // block.
  MachineInstrBuilder Br = BuildMI(*BB, BB->end(), DL, TII.get(IA64::BR_CTOP))
                               .addMBB(BB)
                               .addImm(IA64II::WhetherSPTK)
                               .addImm(0)
                               .addImm(0);
  addRotatingOperands(Br, /*Uses=*/true);
  MachineBasicBlock *NewExit = MF.CreateMachineBasicBlock(BB->getBasicBlock());
  MF.insert(std::next(BB->getIterator()), NewExit);
  LIS.insertMBBInMaps(NewExit);
  BB->replaceSuccessor(Exit, NewExit);
  NewExit->addSuccessor(Exit);
  Exit->replacePhiUsesWith(BB, NewExit);
  for (auto &[Reg, V] : Rotating) {
    if (!V.LiveOut)
      continue;
    BuildMI(*NewExit, NewExit->end(), DL, TII.get(IA64::COPY), Reg)
        .addReg(name(Reg, NumStages - V.Stage));
  }
  MachineInstrBuilder Clear =
      BuildMI(*NewExit, NewExit->end(), DL, TII.get(IA64::CLRRRB));
  addRotatingOperands(Clear, /*Uses=*/false);
  TII.insertUnconditionalBranch(*NewExit, Exit, DL);

  // Bring the slot indexes and live intervals up to date.
  for (MachineInstr &MI : *BB)
    LIS.InsertMachineInstrInMaps(MI);
  for (MachineInstr *MI : NewMIs)
    LIS.InsertMachineInstrInMaps(*MI);
  for (MachineInstr &MI : *NewExit)
    LIS.InsertMachineInstrInMaps(MI);
  for (Register Reg : Touched) {
    if (LIS.hasInterval(Reg))
      LIS.removeInterval(Reg);
    MRI.clearKillFlags(Reg);
  }
  for (Register Reg : Touched)
    if (!LIS.hasInterval(Reg) && !MRI.reg_nodbg_empty(Reg))
      LIS.createAndComputeVirtRegInterval(Reg);
  // alloc gives the function one rotating region, as large as its largest
  // loop needs, and every br.ctop and clrrrb renames all of it, so what else
  // lives in it, such as an input argument, does not survive any loop.
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != IA64::BR_CTOP && MI.getOpcode() != IA64::CLRRRB)
        continue;
      for (unsigned I = 0; I != FI->getRotatingGRs(); ++I)
        if (!MI.definesRegister(IA64::R32 + I, &TRI))
          MI.addOperand(MachineOperand::CreateReg(
              IA64::R32 + I, /*isDef=*/true, /*isImp=*/true,
              /*isKill=*/false, /*isDead=*/true));
    }
  for (unsigned Reg = IA64::R32; Reg <= IA64::R127; ++Reg)
    LIS.removeAllRegUnitsForPhysReg(Reg);
  for (unsigned Reg = IA64::F32; Reg <= IA64::F127; ++Reg)
    LIS.removeAllRegUnitsForPhysReg(Reg);
  for (unsigned Reg = IA64::P16; Reg <= IA64::P63; ++Reg)
    LIS.removeAllRegUnitsForPhysReg(Reg);
  LIS.removeAllRegUnitsForPhysReg(IA64::AR65);
  LIS.removeAllRegUnitsForPhysReg(IA64::AR66);
  ++NumRotating;
}

// A loop that may run no more iterations than a rotating schedule may have
// stages would spend more passes of its kernel filling and draining than
// running, so it is left to the default expander.
unsigned llvm::getIA64RotatingMaxStages(MachineBasicBlock *Loop,
                                        std::optional<uint64_t> MaxTripCount) {
  if (MaxTripCount && *MaxTripCount <= RotatingMaxStages)
    return 0;
  RotatingExpander E(Loop, nullptr);
  return E.canExpand() ? (unsigned)RotatingMaxStages : 0;
}

bool llvm::expandIA64RotatingSchedule(ModuloSchedule &MS, LiveIntervals &LIS) {
  if ((unsigned)MS.getNumStages() < RotatingMinStages)
    return false;
  RotatingExpander E(MS.getLoop()->getTopBlock(), &MS);
  if (!E.canExpand() || !DebugCounter::shouldExecute(RotatingCounter))
    return false;
  E.expand(LIS);
  return true;
}

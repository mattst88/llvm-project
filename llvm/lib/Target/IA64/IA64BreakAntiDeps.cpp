//===-- IA64BreakAntiDeps.cpp - Rename registers reused within a block ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The register allocator gives a value the register another value was last
// read from just before, so that the write of the second cannot issue in a
// group before the read of the first, even where nothing else orders them.
// The post-RA scheduler does not rename registers. IA64BreakAntiDeps does
// it for it: a general register written where the block reads or writes it
// before, whose value is read only in the block, moves to a scratch register
// that nothing in the block holds over the same span, preferably one the
// block refers to nowhere near.
//
// Only plain writes move, along with their plain reads, which for a
// predicated write must be under the same predicate. The registers of a
// speculative load and its check stay together, since the check holds only
// where they are the same.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-break-anti-deps"

STATISTIC(NumRenamed, "Number of values moved to another register");

static cl::opt<bool>
    EnableRenaming("ia64-anti-dep-breaking", cl::init(true), cl::Hidden,
                   cl::desc("Rename general registers reused within a block"));

namespace {

class IA64BreakAntiDeps : public MachineFunctionPass {
public:
  static char ID;

  IA64BreakAntiDeps() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "IA64 anti-dependence breaking";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const IA64InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  /// The scratch registers a value may move to.
  SmallVector<MCPhysReg, 24> Scratch;

  bool runOnBlock(MachineBasicBlock &MBB);
};

} // end anonymous namespace

char IA64BreakAntiDeps::ID = 0;

INITIALIZE_PASS(IA64BreakAntiDeps, DEBUG_TYPE, "IA64 anti-dependence breaking",
                false, false)

FunctionPass *llvm::createIA64BreakAntiDepsPass() {
  return new IA64BreakAntiDeps();
}

/// Whether the general registers \p MI reads may be renamed.
static bool hasRenamableReads(const MachineInstr &MI) {
  return !MI.isPseudo() && !MI.isCall() && !MI.isInlineAsm() &&
         !MI.hasUnmodeledSideEffects() && !MI.isBundled() &&
         !MI.getFlag(MachineInstr::FrameSetup) &&
         !MI.getFlag(MachineInstr::FrameDestroy);
}

/// The predicate register that guards \p MI, p0 where nothing does.
static Register predicateOf(const MachineInstr &MI) {
  int Idx = MI.findFirstPredOperandIdx();
  return Idx >= 0 ? MI.getOperand(Idx).getReg() : Register(IA64::P0);
}

/// Whether \p MI refers to \p Reg, a call's clobber among the references.
static bool refersTo(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [&](const MachineOperand &MO) {
    return (MO.isReg() && MO.getReg() == Reg) ||
           (MO.isRegMask() && MO.clobbersPhysReg(Reg));
  });
}

/// Whether Instrs[\p To] reads a register that a chain of register reads and
/// writes from Instrs[\p From] computes.
static bool dependsOn(ArrayRef<MachineInstr *> Instrs, unsigned From,
                      unsigned To, const TargetRegisterInfo &TRI) {
  SmallVector<Register, 8> Computed;
  auto Reads = [&](const MachineInstr &MI) {
    return any_of(Computed, [&](Register R) {
      return MI.readsRegister(R, &TRI);
    });
  };
  for (unsigned I = From; I != To; ++I) {
    const MachineInstr &MI = *Instrs[I];
    if (I != From && !Reads(MI))
      continue;
    for (const MachineOperand &MO : MI.all_defs())
      if (MO.getReg())
        Computed.push_back(MO.getReg());
  }
  return Reads(*Instrs[To]);
}

bool IA64BreakAntiDeps::runOnBlock(MachineBasicBlock &MBB) {
  SmallVector<MachineInstr *, 64> Instrs;
  for (MachineInstr &MI : MBB)
    if (!MI.isDebugInstr())
      Instrs.push_back(&MI);
  unsigned N = Instrs.size();
  if (N < 2)
    return false;

  // Live[I] has bit S set where Scratch[S] is live after Instrs[I], and
  // Refs[S] lists the instructions that refer to it.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  LivePhysRegs LiveOut(*TRI);
  LiveOut.addLiveOuts(MBB);
  SmallVector<uint32_t, 64> Live(N, 0);
  SmallVector<SmallVector<unsigned, 8>, 24> Refs(Scratch.size());
  LivePhysRegs LPR(*TRI);
  LPR.addLiveOuts(MBB);
  for (unsigned I = N; I-- != 0;) {
    for (auto [S, Reg] : enumerate(Scratch)) {
      if (!LPR.available(MRI, Reg))
        Live[I] |= 1u << S;
      if (refersTo(*Instrs[I], Reg))
        Refs[S].push_back(I);
    }
    LPR.stepBackward(*Instrs[I]);
  }
  uint32_t LiveIn = 0;
  for (auto [S, Reg] : enumerate(Scratch))
    if (!LPR.available(MRI, Reg))
      LiveIn |= 1u << S;

  // Move the value Instrs[From] writes in DefMO, read by Uses up to
  // Instrs[Last], to the free scratch register the block refers to farthest
  // from the span.
  auto Rename = [&](unsigned From, unsigned Last, MachineOperand &DefMO,
                    ArrayRef<MachineOperand *> Uses) {
    Register Reg = DefMO.getReg();
    // Some operands take only some general registers.
    auto Fits = [&](MCPhysReg New) {
      auto Takes = [&](const MachineOperand *Op) {
        const TargetRegisterClass *RC = Op->getParent()->getRegClassConstraint(
            Op->getOperandNo(), TII, TRI);
        return !RC || RC->contains(New);
      };
      return Takes(&DefMO) && all_of(Uses, Takes);
    };
    uint32_t LiveBefore = From ? Live[From - 1] : LiveIn;
    int Best = -1;
    unsigned BestGap = 0;
    for (auto [S, New] : enumerate(Scratch)) {
      if (New == Reg || (LiveBefore & (1u << S)) || !Fits(New) ||
          any_of(Refs[S], [&](unsigned R) { return R >= From && R <= Last; }))
        continue;
      bool Free = true;
      for (unsigned J = From; J <= Last && Free; ++J)
        Free = !(Live[J] & (1u << S));
      if (!Free)
        continue;
      unsigned Gap = N + 1;
      for (unsigned R : Refs[S])
        Gap = std::min(Gap, R < From ? From - R : R - Last);
      if (Best < 0 || Gap > BestGap) {
        Best = S;
        BestGap = Gap;
      }
    }
    if (Best < 0)
      return false;
    MCPhysReg New = Scratch[Best];
    MachineInstr &Def = *Instrs[From];
    LLVM_DEBUG(dbgs() << "Renaming " << printReg(Reg, TRI) << " to "
                      << printReg(New, TRI) << " in " << Def);
    DefMO.setReg(New);
    for (MachineOperand *Use : Uses)
      Use->setReg(New);
    // Debug values of the value follow it.
    for (MachineInstr &MI : make_range(std::next(Def.getIterator()),
                                       std::next(Instrs[Last]->getIterator())))
      if (MI.isDebugInstr())
        for (MachineOperand &Op : MI.debug_operands())
          if (Op.isReg() && Op.getReg() == Reg)
            Op.setReg(New);
    for (unsigned J = From; J != Last; ++J)
      Live[J] |= 1u << Best;
    for (unsigned J = From; J <= Last; ++J)
      if (refersTo(*Instrs[J], New))
        Refs[Best].push_back(J);
    ++NumRenamed;
    return true;
  };

  // The last instruction before Instrs[D] that refers to Reg, plus one, if
  // the write there could issue earlier for not reusing the register.
  auto PrevRef = [&](unsigned D, Register Reg) -> unsigned {
    unsigned Prev = D;
    while (Prev != 0 && !refersTo(*Instrs[Prev - 1], Reg))
      --Prev;
    if (Prev == 0 || Instrs[Prev - 1]->isCall() ||
        dependsOn(Instrs, Prev - 1, D, *TRI))
      return 0;
    return Prev;
  };

  bool Changed = false;
  for (unsigned D = 1; D != N; ++D) {
    MachineInstr &Def = *Instrs[D];
    if (!hasRenamableReads(Def))
      continue;
    // A predicated write moves only with reads under the same predicate,
    // which see the value only where the write took place.
    Register Pred = predicateOf(Def);
    for (MachineOperand &MO : Def.explicit_operands()) {
      if (!MO.isReg() || !MO.isDef() || MO.isTied() || MO.isDead())
        continue;
      Register Reg = MO.getReg();
      if (!IA64::GRRegClass.contains(Reg) || MRI.isReserved(Reg) ||
          Def.readsRegister(Reg, TRI) ||
          count_if(Def.operands(), [&](const MachineOperand &Op) {
            return Op.isReg() && Op.getReg() == Reg;
          }) != 1)
        continue;
      // There is something to gain only if the block names the register
      // before, other than in a call, which nothing moves across anyway, and
      // the write does not already wait for that instruction through what it
      // reads.
      unsigned Prev = PrevRef(D, Reg);
      if (!Prev)
        continue;
      // The reads of the value, up to the next write of the register, which
      // must replace it whole.
      SmallVector<MachineOperand *, 4> Uses;
      bool Ok = true;
      unsigned Last = D, I = D + 1;
      for (; Ok && I != N; ++I) {
        MachineInstr &MI = *Instrs[I];
        if (Pred != IA64::P0 && MI.modifiesRegister(Pred, TRI))
          Ok = false;
        if (!refersTo(MI, Reg))
          continue;
        bool Writes = false;
        for (MachineOperand &Op : MI.operands()) {
          if (!Op.isReg() || Op.getReg() != Reg)
            continue;
          if (Op.isDef()) {
            Writes = true;
            if (Op.isImplicit() || Op.isTied() || predicateOf(MI) != Pred)
              Ok = false;
          } else if (Op.isImplicit() || Op.isTied() || Op.isUndef() ||
                     !hasRenamableReads(MI) ||
                     (Pred != IA64::P0 && predicateOf(MI) != Pred)) {
            Ok = false;
          } else {
            Uses.push_back(&Op);
            Last = I;
          }
        }
        if (Writes)
          break;
      }
      // A value the block leaves in the register for its successors stays.
      if (Ok && !Uses.empty() && !(I == N && !LiveOut.available(MRI, Reg)) &&
          Rename(D, Last, MO, Uses)) {
        Changed = true;
        continue;
      }
      // Otherwise the values the block wrote in the register before, one at
      // a time back from the write, can move out of its way, as long as
      // nothing reads them past the write.
      if (Pred != IA64::P0)
        continue;
      for (unsigned Tries = 0; Prev && Tries != 4; ++Tries) {
        unsigned Q = Prev - 1;
        MachineInstr &Reader = *Instrs[Q];
        if (Reader.modifiesRegister(Reg, TRI))
          break;
        unsigned E = Q;
        while (E != 0 && !Instrs[E - 1]->modifiesRegister(Reg, TRI))
          --E;
        if (E-- == 0)
          break;
        MachineInstr &Earlier = *Instrs[E];
        if (!hasRenamableReads(Earlier) || predicateOf(Earlier) != IA64::P0)
          break;
        MachineOperand *EarlierMO = nullptr;
        unsigned NumDefs = 0;
        for (MachineOperand &Op : Earlier.operands())
          if (Op.isReg() && Op.isDef() && Op.getReg() == Reg) {
            ++NumDefs;
            if (!Op.isImplicit() && !Op.isTied() && !Op.isDead())
              EarlierMO = &Op;
          }
        if (NumDefs != 1 || !EarlierMO)
          break;
        SmallVector<MachineOperand *, 4> EarlierUses;
        bool Renamable = true;
        for (unsigned K = E + 1; K <= Q && Renamable; ++K) {
          MachineInstr &MI = *Instrs[K];
          for (MachineOperand &Op : MI.operands()) {
            if (Op.isRegMask() && Op.clobbersPhysReg(Reg))
              Renamable = false;
            if (!Op.isReg() || Op.getReg() != Reg)
              continue;
            if (Op.isDef() || Op.isImplicit() || Op.isTied() || Op.isUndef() ||
                !hasRenamableReads(MI))
              Renamable = false;
            else
              EarlierUses.push_back(&Op);
          }
        }
        if (!Renamable || EarlierUses.empty() ||
            !Rename(E, Q, *EarlierMO, EarlierUses))
          break;
        Changed = true;
        Prev = PrevRef(D, Reg);
      }
    }
  }
  return Changed;
}

bool IA64BreakAntiDeps::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableRenaming || skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  // The static general registers a call may clobber; those of the register
  // stack belong to the frame that alloc sized.
  const uint32_t *Preserved =
      TRI->getCallPreservedMask(MF, MF.getFunction().getCallingConv());
  Scratch.clear();
  for (MCPhysReg Reg : IA64::GRRegClass) {
    if (Reg >= IA64::R32 && Reg <= IA64::R127)
      continue;
    if (!MRI.isReserved(Reg) && MRI.isAllocatable(Reg) &&
        MachineOperand::clobbersPhysReg(Preserved, Reg))
      Scratch.push_back(Reg);
  }
  assert(Scratch.size() <= 32 && "too many scratch registers for a mask");
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    Changed |= runOnBlock(MBB);
  return Changed;
}

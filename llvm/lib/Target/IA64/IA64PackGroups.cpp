//===-- IA64PackGroups.cpp - Fit instruction groups to bundles ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The assembler packs instructions into bundles in the order it gets them,
// choosing each template as GNU as does: the first that holds the most of the
// next three instructions (see IA64ELFStreamer.cpp). That is often not the
// template that issues fastest. Six A-unit instructions, for example, fill
// two MII bundles, which need four I-unit ports where Itanium 2 has two, so
// the group takes two cycles; two MMI bundles would take one.
//
// Itanium 2 disperses at most two bundles per cycle, and a cycle also ends at
// a stop or when an instruction finds no free port of its unit. This pass
// replays the assembler's packing and that dispersal, and improves on them
// in two ways:
//
//   - It reorders each instruction group. Any order is valid as long as no
//     register read moves after a write of the register, and accesses to
//     memory keep their order. It moves one instruction at a time while
//     that lowers the cycles, then the bundles, that the group and its
//     neighbors take.
//   - It chooses the template of each bundle by the cost of the next two
//     groups, and where that differs from the assembler's choice it has the
//     asm printer emit a template directive.
//
// A label starts a bundle, so the replay restarts at each block that has
// one. Blocks with anything the replay does not model are left alone.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-pack-groups"
#define PASS_NAME "IA64 instruction group packing"

STATISTIC(NumMoved, "Number of instructions moved within their group");
STATISTIC(NumTemplates, "Number of bundle templates chosen");

static cl::opt<bool> EnablePacking("ia64-packing", cl::Hidden, cl::init(true),
                                   cl::desc("Order instruction groups and "
                                            "choose bundle templates"));

namespace {

class IA64PackGroups : public MachineFunctionPass {
public:
  static char ID;

  IA64PackGroups() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

/// An instruction as the packing sees it.
struct Item {
  MachineInstr *MI;
  /// The units of the slots that can hold it, as for IA64::getSlotUnits.
  unsigned Units;
  bool LastSlot;
  bool IsLoad;
  bool IsStore;
  /// Whether its group ends after it.
  bool Stop;
  /// The template of a directive before it, or -1.
  int Template = -1;
};

/// The dispersal of bundles into issue cycles, and its cost so far.
struct Dispersal {
  unsigned Cycles = 0;
  unsigned Bundles = 0;
  unsigned BundlesNow = 0;
  unsigned Ports[8] = {};
  unsigned Loads = 0;
  unsigned Stores = 0;
  bool EndCycle = true;

  /// Start a cycle that already holds \p Open bundles.
  void newCycle(unsigned Open) {
    ++Cycles;
    BundlesNow = Open;
    std::fill(std::begin(Ports), std::end(Ports), 0);
    Loads = Stores = 0;
    EndCycle = false;
  }

  void startBundle() {
    ++Bundles;
    if (EndCycle || BundlesNow == 2)
      newCycle(0);
    ++BundlesNow;
  }

  /// Issue \p It in a slot of \p Unit. After a stop, or without a free
  /// port, it waits for the next cycle, and the rest of its bundle with it.
  void issue(const Item &It, IA64II::Unit Unit) {
    if (EndCycle)
      newCycle(1);
    unsigned Port = Unit == IA64II::UnitL ? IA64II::UnitI : Unit;
    unsigned Limit = Port == IA64II::UnitM ? 4 : Port == IA64II::UnitB ? 3 : 2;
    if (Ports[Port] == Limit || (It.IsLoad && Loads == 2) ||
        (It.IsStore && Stores == 2))
      newCycle(1);
    ++Ports[Port];
    Loads += It.IsLoad;
    Stores += It.IsStore;
    EndCycle = It.Stop;
  }

  bool operator<(const Dispersal &O) const {
    return std::tie(Cycles, Bundles) < std::tie(O.Cycles, O.Bundles);
  }
};

} // end anonymous namespace

char IA64PackGroups::ID = 0;

INITIALIZE_PASS(IA64PackGroups, DEBUG_TYPE, PASS_NAME, false, false)

/// The largest instruction group that is reordered.
constexpr unsigned MaxGroupSize = 24;

constexpr unsigned MII = 0, MISemiI = 1, MMI = 4, MSemiMI = 5;

static bool slotTakes(IA64II::Unit Slot, unsigned Units) {
  return (Units & 1u << Slot) != 0;
}

/// The template that the assembler chooses for a bundle starting at
/// \p Items, or ~0u if none holds the first instruction.
static unsigned assemblerTemplate(ArrayRef<Item> Items) {
  unsigned Index = ~0u, Best = 0;
  for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
    const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
    if (!T.Name || T.MidStop != -1)
      continue;
    unsigned Slot = 0, N = 0;
    for (const Item &It : Items.take_front(3)) {
      while (Slot != 3 && !slotTakes(T.Slots[Slot], It.Units))
        ++Slot;
      if (Slot == 3)
        break;
      ++N;
      Slot += T.Slots[Slot] == IA64II::UnitL ? 2 : 1;
    }
    if (N > Best) {
      Best = N;
      Index = I;
    }
  }
  return Index;
}

/// Pack the bundle that starts at \p Items as IA64ELFStreamer::emitBundle
/// does, and disperse it into \p D. Return how many instructions it holds,
/// or 0 if the first does not fit. A directive on the first instruction
/// gives the template, and one on a later instruction ends the bundle.
static unsigned packBundle(ArrayRef<Item> Items, Dispersal &D) {
  bool Explicit = Items[0].Template != -1;
  unsigned Index = Explicit ? Items[0].Template : assemblerTemplate(Items);
  if (Index == ~0u)
    return 0;
  D.startBundle();
  unsigned Taken = 0;
  auto AtEnd = [&] {
    return Taken == Items.size() || Items[Taken].Template != -1;
  };
  for (unsigned Slot = 0; Slot != 3 && (Taken == 0 || !AtEnd()); ++Slot) {
    const Item &It = Items[Taken];
    IA64II::Unit Unit = IA64II::BundleTemplates[Index].Slots[Slot];
    if (!slotTakes(Unit, It.Units) || (Slot != 2 && It.LastSlot))
      continue;
    D.issue(It, Unit);
    if (Unit == IA64II::UnitL)
      ++Slot;
    ++Taken;
    if (!It.Stop)
      continue;
    // Where the template has a form with a stop there, the bundle goes on.
    if (AtEnd() || Slot == 2)
      break;
    if (!Explicit && Slot == 0 && Items[Taken].LastSlot)
      Slot = 1;
    const IA64II::BundleTemplate &T = IA64II::BundleTemplates[Index];
    if (Explicit) {
      if (Slot == 0 && Index == MMI) {
        Index = MSemiMI;
      } else if (Index == MII) {
        Index = MISemiI;
        Slot = 1;
      } else {
        break;
      }
    } else if (Slot == 0 && T.Slots[0] == IA64II::UnitM) {
      Index = MSemiMI;
    } else if (Slot == 1 && T.Slots[1] == IA64II::UnitI && T.MidStop == -1) {
      Index = MISemiI;
    } else {
      break;
    }
  }
  if (Taken == 0)
    --D.Bundles;
  return Taken;
}

/// Choose the template of each bundle of \p Items, which start a bundle, by
/// the cost of the bundles up to the end of the next two groups, and set a
/// directive where the choice is not the assembler's. Return the dispersal of
/// the whole.
static Dispersal plan(MutableArrayRef<Item> Items) {
  for (Item &It : Items)
    It.Template = -1;
  Dispersal D;
  size_t Next = 0;
  while (Next != Items.size()) {
    size_t Horizon = Next;
    for (unsigned Stops = 0; Horizon != Items.size() && Stops != 2;)
      Stops += Items[Horizon++].Stop;
    // The cost of the instructions before the horizon if this bundle has the
    // given template. Every choice packs the same ones, so that one that
    // fills its last bundle further does not look worse.
    auto Try = [&](int Template, Dispersal &Out) {
      Out = D;
      Items[Next].Template = Template;
      size_t I = Next;
      do {
        unsigned Taken = packBundle(Items.slice(I, Horizon - I), Out);
        if (Taken == 0)
          return false;
        I += Taken;
      } while (I < Horizon);
      return true;
    };
    int Best = -1;
    Dispersal BestCost;
    if (!Try(-1, BestCost)) {
      Best = -2;
      BestCost.Cycles = ~0u;
    }
    unsigned Assembler = assemblerTemplate(Items.drop_front(Next));
    for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
      const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
      if (!T.Name || T.MidStop != -1 || I == Assembler)
        continue;
      Dispersal Cost;
      if (Try(I, Cost) && Cost < BestCost) {
        Best = I;
        BestCost = Cost;
      }
    }
    assert(Best != -2 && "no template holds the instruction");
    Items[Next].Template = Best;
    Next += packBundle(Items.drop_front(Next), D);
  }
  return D;
}

/// The slot units of \p MI, or 0 if the packing does not model it.
static unsigned getUnits(const MachineInstr &MI, const MCInstrInfo &MCII,
                         const MCRegisterInfo &MRI) {
  switch (MI.getOpcode()) {
  case IA64::BRUNCOND:
    return 1u << IA64II::UnitB;
  case IA64::BRLONG:
    return 1u << IA64II::UnitL;
  case IA64::ALLOC:
    return 1u << IA64II::UnitM;
  // These become more than one instruction, or the assembler places them by
  // rules of their own.
  case IA64::BRIND:
  case IA64::FLUSHRS:
  case IA64::LOADRS:
  case IA64::HINT:
  case IA64::CHK_S:
  case IA64::COVER:
  case IA64::CLRRRB:
  case IA64::CLRRRB_PR:
  case IA64::RFI:
  case IA64::BSW_0:
  case IA64::BSW_1:
  case IA64::ITC_D:
  case IA64::ITC_I:
  case IA64::PTC_G:
  case IA64::PTC_GA:
    return 0;
  }
  // Only the AR operand of a mov to or from an AR selects the unit. A pseudo
  // that the asm printer expands has none.
  MCInst Inst;
  Inst.setOpcode(MI.getOpcode());
  for (const MachineOperand &MO : MI.explicit_operands())
    Inst.addOperand(MO.isReg() ? MCOperand::createReg(MO.getReg())
                               : MCOperand::createImm(0));
  return IA64::getSlotUnits(Inst, MCII, MRI);
}

/// Whether \p A, which comes first in its group, must stay before \p B.
static bool mustPrecede(const MachineInstr &A, const MachineInstr &B,
                        const TargetRegisterInfo &TRI) {
  // A read must not move after a write of its register, and neither may a
  // write of a register that is also written or read later. Only branches
  // read registers written in their own group, and they stay last.
  for (const MachineOperand &MA : A.operands()) {
    if (!MA.isReg() || !MA.getReg())
      continue;
    for (const MachineOperand &MB : B.operands())
      if (MB.isReg() && MB.getReg() && (MA.isDef() || MB.isDef()) &&
          TRI.regsOverlap(MA.getReg(), MB.getReg()))
        return true;
  }
  // Accesses to memory take effect in program order within a group.
  return A.mayLoadOrStore() && B.mayLoadOrStore() &&
         (A.mayStore() || B.mayStore() || A.hasOrderedMemoryRef() ||
          B.hasOrderedMemoryRef());
}

/// Whether \p MI keeps its place among the instructions of its group.
static bool isFixed(const MachineInstr &MI) {
  return MI.isBranch() || MI.isCall() || MI.isReturn() || MI.isTerminator() ||
         MI.hasUnmodeledSideEffects();
}

/// Whether \p MI is in the prologue or the epilogue, whose unwind directives
/// count instructions.
static bool pinsGroup(const MachineInstr &MI) {
  return MI.getFlag(MachineInstr::FrameSetup) ||
         MI.getFlag(MachineInstr::FrameDestroy) || MI.getHeapAllocMarker() ||
         MI.getPCSections() || MI.getMMRAMetadata();
}

/// Whether the asm printer emits a label or an alignment before \p MBB,
/// either of which starts a bundle. This follows
/// AsmPrinter::isBlockOnlyReachableByFallthrough.
static bool startsBundle(const MachineBasicBlock &MBB) {
  if (MBB.isEntryBlock() || MBB.getAlignment() > Align(1) ||
      MBB.hasAddressTaken() || MBB.isEHPad() || MBB.hasLabelMustBeEmitted() ||
      MBB.isBeginSection() || MBB.pred_size() != 1)
    return true;
  const MachineBasicBlock &Pred = **MBB.pred_begin();
  if (!Pred.isLayoutSuccessor(&MBB))
    return true;
  for (const MachineInstr &MI : Pred.terminators()) {
    if (!MI.isBranch() || MI.isIndirectBranch())
      return true;
    for (const MachineOperand &MO : MI.operands())
      if (MO.isMBB() && MO.getMBB() == &MBB)
        return true;
  }
  return false;
}

bool IA64PackGroups::runOnMachineFunction(MachineFunction &MF) {
  auto &Templates = MF.getInfo<IA64MachineFunctionInfo>()->getTemplates();
  Templates.clear();
  if (!EnablePacking || skipFunction(MF.getFunction()))
    return false;
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  const MCRegisterInfo &MRI = TRI;
  const MCInstrInfo &MCII = *STI.getInstrInfo();
  bool Changed = false;

  // The instructions since the last start of a bundle. After a block that
  // is not modeled, the assembler may pack them with the instructions before,
  // which only makes the plan less accurate.
  SmallVector<Item> Run;
  auto PlanRun = [&] {
    if (Run.empty())
      return;
    plan(Run);
    for (const Item &It : Run)
      if (It.Template != -1) {
        Templates[It.MI] = It.Template;
        ++NumTemplates;
      }
    Run.clear();
  };

  for (MachineBasicBlock &MBB : MF) {
    if (startsBundle(MBB))
      PlanRun();

    // The block as items, and its groups as ranges of them that may be
    // reordered. Instructions that emit no code move with the one they
    // follow.
    SmallVector<Item> Items;
    SmallVector<SmallVector<MachineInstr *, 1>> Attached;
    SmallVector<MachineInstr *, 1> Leading;
    SmallVector<std::pair<unsigned, unsigned>> Groups;
    unsigned GroupStart = 0;
    bool Movable = true;
    bool Opaque = false;
    auto EndGroup = [&] {
      if (Movable)
        Groups.push_back({GroupStart, Items.size()});
      GroupStart = Items.size();
      Movable = true;
    };
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.getOpcode() == TargetOpcode::KILL ||
          MI.getOpcode() == TargetOpcode::IMPLICIT_DEF) {
        (Items.empty() ? Leading : Attached.back()).push_back(&MI);
        continue;
      }
      // Anything else may emit a label, or code the packing does not know.
      unsigned Units = MI.isMetaInstruction() || MI.isInlineAsm() ||
                               MI.getPreInstrSymbol() || MI.getPostInstrSymbol()
                           ? 0
                           : getUnits(MI, MCII, MRI);
      if (Units == 0) {
        LLVM_DEBUG(dbgs() << "Not modeled in " << printMBBReference(MBB) << ": "
                          << MI);
        Opaque = true;
        break;
      }
      // The assembler ends the group before an alloc.
      if (MI.getOpcode() == IA64::ALLOC) {
        if (!Items.empty() && !Items.back().Stop) {
          Items.back().Stop = true;
          EndGroup();
        } else if (Items.empty() && !Run.empty()) {
          Run.back().Stop = true;
        }
      }
      bool Stop = MI.getAsmPrinterFlag(IA64::StopAfter);
      Items.push_back({&MI, Units,
                       (MI.getDesc().TSFlags & IA64II::LastSlot) != 0,
                       MI.mayLoad(), MI.mayStore(), Stop});
      Attached.emplace_back();
      Movable &= !pinsGroup(MI);
      if (Stop)
        EndGroup();
    }
    if (Opaque) {
      PlanRun();
      continue;
    }
    if (Movable && GroupStart + 1 < Items.size())
      Groups.push_back({GroupStart, Items.size()});

    // The cost of the group before G, G, and the group after.
    auto CostAround = [&](unsigned G) {
      unsigned Begin = Groups[G].first, End = Groups[G].second;
      if (Begin != 0)
        --Begin;
      while (Begin != 0 && !Items[Begin - 1].Stop)
        --Begin;
      while (End != Items.size() && !Items[End].Stop)
        ++End;
      End = std::min<unsigned>(End + 1, Items.size());
      SmallVector<Item, 16> Window(Items.begin() + Begin, Items.begin() + End);
      return plan(Window);
    };

    bool Reordered = false;
    for (unsigned G = 0; G != Groups.size(); ++G) {
      auto [Begin, End] = Groups[G];
      // The search takes time cubic in the size of the group.
      if (End - Begin < 2 || End - Begin > MaxGroupSize)
        continue;
      // The stop goes with whatever ends up last.
      bool Stop = Items[End - 1].Stop;
      Items[End - 1].Stop = false;
      auto Rotate = [&](unsigned From, unsigned To) {
        auto Move = [&](auto &V) {
          if (From < To)
            std::rotate(V.begin() + From, V.begin() + From + 1,
                        V.begin() + To + 1);
          else
            std::rotate(V.begin() + To, V.begin() + From, V.begin() + From + 1);
        };
        Move(Items);
        Move(Attached);
      };
      auto Evaluate = [&] {
        Items[End - 1].Stop = Stop;
        Dispersal Cost = CostAround(G);
        Items[End - 1].Stop = false;
        return Cost;
      };
      Dispersal Current = Evaluate();
      bool Improved = true;
      for (unsigned Round = 0; Improved && Round != 4; ++Round) {
        Improved = false;
        for (unsigned From = Begin; From != End; ++From) {
          const MachineInstr &Moved = *Items[From].MI;
          if (isFixed(Moved))
            continue;
          for (unsigned To = Begin; To != End; ++To) {
            if (To == From)
              continue;
            // A move passes every instruction in between.
            bool Legal = true;
            if (To < From) {
              for (unsigned I = To; I != From && Legal; ++I)
                Legal = !isFixed(*Items[I].MI) &&
                        !mustPrecede(*Items[I].MI, Moved, TRI);
            } else {
              for (unsigned I = From + 1; I != To + 1 && Legal; ++I)
                Legal = !isFixed(*Items[I].MI) &&
                        !mustPrecede(Moved, *Items[I].MI, TRI);
            }
            if (!Legal)
              continue;
            Rotate(From, To);
            Dispersal Cost = Evaluate();
            if (Cost < Current) {
              Current = Cost;
              Improved = Reordered = true;
              ++NumMoved;
              break;
            }
            Rotate(To, From);
          }
        }
      }
      Items[End - 1].Stop = Stop;
    }

    if (Reordered) {
      // Rebuild the block in the new order, with the stop after each group.
      auto Append = [&](MachineInstr *MI) { MBB.splice(MBB.end(), &MBB, MI); };
      for (MachineInstr *MI : Leading)
        Append(MI);
      for (auto [It, Rest] : zip(Items, Attached)) {
        It.MI->clearAsmPrinterFlag(IA64::StopAfter);
        if (It.Stop)
          It.MI->setAsmPrinterFlag(IA64::StopAfter);
        Append(It.MI);
        for (MachineInstr *MI : Rest)
          Append(MI);
      }
      // The last read of a register may now be another one.
      if (MF.getProperties().hasTracksLiveness())
        recomputeLivenessFlags(MBB);
      Changed = true;
    }
    Run.append(Items.begin(), Items.end());
  }
  PlanRun();
  return Changed || !Templates.empty();
}

FunctionPass *llvm::createIA64PackGroupsPass() { return new IA64PackGroups(); }

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
// in three ways:
//
//   - It reorders each instruction group. Any order is valid as long as no
//     register read moves after a write of the register, and accesses to
//     memory keep their order. It moves one instruction at a time while
//     that lowers the cycles, then the bundles, that the group and its
//     neighbors take.
//   - It moves an instruction that only I0 executes into the group before
//     or after its own, where that has a free I0 port. Two of them in a
//     group take two cycles.
//   - It chooses the template of each bundle, together with that of the
//     bundle after it, by the cost of the next two groups, and where that
//     differs from the assembler's choice it has the asm printer emit a
//     template directive.
//
// The I slots of a cycle's bundles take ports I0 and I1 in order, so an
// instruction that only I0 executes must be the first of its cycle on an I
// port, or it and the rest of its bundle wait for the next.
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
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-pack-groups"
#define PASS_NAME "IA64 instruction group packing"

STATISTIC(NumMoved, "Number of instructions moved within their group");
STATISTIC(NumCrossed, "Number of instructions moved to another group");
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
  /// Whether only port I0 executes it.
  bool I0Only;
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
        (It.IsStore && Stores == 2) || (It.I0Only && Ports[Port] != 0))
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
/// directive where the choice is not the assembler's. Each bundle is chosen
/// together with the next, since a group of two bundles may issue in one
/// cycle only if both change, as MII,MII to MMI,MMI. Return the dispersal of
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
    // The templates to try for a bundle starting at \p At: the assembler's,
    // as -1, and the others that it does not choose.
    auto Choices = [&](size_t At) {
      SmallVector<int, 16> Result = {-1};
      unsigned Assembler = assemblerTemplate(Items.drop_front(At));
      for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
        const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
        if (T.Name && T.MidStop == -1 && I != Assembler)
          Result.push_back(I);
      }
      return Result;
    };
    // A choice for the next bundle too must do strictly better than leaving
    // it to the assembler.
    int Best = -2;
    Dispersal BestCost;
    BestCost.Cycles = ~0u;
    for (bool Pairs : {false, true}) {
      for (int First : Choices(Next)) {
        Items[Next].Template = First;
        Dispersal Bundle = D;
        size_t Second = Next + packBundle(Items.drop_front(Next), Bundle);
        if (Second == Next || (Pairs && Second >= Horizon))
          continue;
        SmallVector<int, 16> Seconds = {-1};
        if (Pairs)
          Seconds = Choices(Second);
        for (int Then : Seconds) {
          if (Pairs && Then == -1)
            continue;
          if (Pairs)
            Items[Second].Template = Then;
          Dispersal Cost;
          if (Try(First, Cost) && Cost < BestCost) {
            Best = First;
            BestCost = Cost;
          }
        }
        if (Pairs)
          Items[Second].Template = -1;
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

/// The index of the scheduling model's port I0, or 0 if it has none.
static unsigned getPortI0(const TargetSchedModel &SM) {
  if (!SM.hasInstrSchedModel())
    return 0;
  const MCSchedModel &M = *SM.getMCSchedModel();
  for (unsigned I = 1; I != M.getNumProcResourceKinds(); ++I)
    if (StringRef(M.getProcResource(I)->Name) == "I2PortI0")
      return I;
  return 0;
}

/// Whether the scheduling model has \p MI use port \p PortI0.
static bool usesOnlyI0(const MachineInstr &MI, const TargetSchedModel &SM,
                       unsigned PortI0) {
  if (!PortI0)
    return false;
  const MCSchedClassDesc *SC = SM.resolveSchedClass(&MI);
  if (!SC->isValid())
    return false;
  return any_of(
      make_range(SM.getWriteProcResBegin(SC), SM.getWriteProcResEnd(SC)),
      [&](const MCWriteProcResEntry &E) {
        return E.ProcResourceIdx == PortI0 && E.ReleaseAtCycle;
      });
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
  // There is no red zone: memory below sp may be overwritten at any
  // instruction, by a signal handler, so an access keeps its order with a
  // write of sp.
  if ((A.mayLoadOrStore() && B.modifiesRegister(IA64::R12, &TRI)) ||
      (B.mayLoadOrStore() && A.modifiesRegister(IA64::R12, &TRI)))
    return true;
  // Accesses to memory take effect in program order within a group.
  return A.mayLoadOrStore() && B.mayLoadOrStore() &&
         (A.mayStore() || B.mayStore() || A.hasOrderedMemoryRef() ||
          B.hasOrderedMemoryRef());
}

/// Whether \p MI copies between a general register and a branch register,
/// ar.pfs, ar.lc or ar.ec. Those are marked as having side effects, but
/// what reads or writes them names them as an operand.
static bool isRegisterMove(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case IA64::MOV_BR:
  case IA64::MOV_FROM_BR:
  case IA64::MOV_AR_RESTORE:
    return true;
  case IA64::MOV_AR:
  case IA64::MOV_FROM_AR: {
    Register AR =
        MI.getOperand(MI.getOpcode() == IA64::MOV_AR ? 0 : 1).getReg();
    return AR == IA64::AR64 || AR == IA64::AR65 || AR == IA64::AR66;
  }
  default:
    return false;
  }
}

/// Whether \p MI keeps its place among the instructions of its group.
static bool isFixed(const MachineInstr &MI) {
  return MI.isBranch() || MI.isCall() || MI.isReturn() || MI.isTerminator() ||
         (MI.hasUnmodeledSideEffects() && !isRegisterMove(MI));
}

/// Whether \p B, in a group after \p A, may not share A's group: B reads or
/// writes a register that A writes.
static bool dependsOn(const MachineInstr &B, const MachineInstr &A,
                      const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MA : A.operands()) {
    if (!MA.isReg() || !MA.getReg() || !MA.isDef())
      continue;
    for (const MachineOperand &MB : B.operands())
      if (MB.isReg() && MB.getReg() &&
          TRI.regsOverlap(MA.getReg(), MB.getReg()))
        return true;
  }
  return false;
}

/// Whether \p It may move to the group before or after its own: an
/// instruction that only I0 executes and that touches only registers.
static bool crossesGroups(const Item &It) {
  const MachineInstr &MI = *It.MI;
  return It.I0Only && !isFixed(MI) && !MI.mayLoadOrStore() &&
         !MI.modifiesRegister(IA64::R12, nullptr);
}

/// Whether \p MI is in the prologue or the epilogue, whose unwind directives
/// count instructions.
static bool pinsGroup(const MachineInstr &MI) {
  return MI.getHeapAllocMarker() || MI.getPCSections() || MI.getMMRAMetadata();
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
  TargetSchedModel SM;
  SM.init(&STI);
  unsigned PortI0 = getPortI0(SM);
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
      Items.push_back(
          {&MI, Units, (MI.getDesc().TSFlags & IA64II::LastSlot) != 0,
           MI.mayLoad(), MI.mayStore(), usesOnlyI0(MI, SM, PortI0), Stop});
      Attached.emplace_back();
      Movable &= !pinsGroup(MI);
      if (Stop)
        EndGroup();
    }
    if (Opaque) {
      PlanRun();
      continue;
    }
    if (Movable && GroupStart < Items.size())
      Groups.push_back({GroupStart, Items.size()});

    // The cost of the instructions from Begin to End, and of the groups
    // before and after them.
    auto CostAround = [&](unsigned Begin, unsigned End) {
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

    bool Reordered = false;
    auto ReorderGroup = [&](unsigned G) {
      auto [Begin, End] = Groups[G];
      // The search takes time cubic in the size of the group.
      if (End - Begin < 2 || End - Begin > MaxGroupSize)
        return;
      // The stop goes with whatever ends up last.
      bool Stop = Items[End - 1].Stop;
      Items[End - 1].Stop = false;
      auto Evaluate = [&] {
        Items[End - 1].Stop = Stop;
        Dispersal Cost = CostAround(Begin, End);
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
    };
    for (unsigned G = 0; G != Groups.size(); ++G)
      ReorderGroup(G);

    // Two instructions that only I0 executes take two cycles in one group,
    // and a group whose instructions wait for a load may hold several. Move
    // one into the group before or after, in the same block, where nothing
    // there keeps it out and that costs less.
    for (unsigned G = 0; G + 1 < Groups.size(); ++G) {
      unsigned Begin = Groups[G].first, Mid = Groups[G].second;
      unsigned End = Groups[G + 1].second;
      if (Groups[G + 1].first != Mid)
        continue;
      Dispersal Current = CostAround(Begin, End);
      // Move the instruction at From to To, with the first group ending
      // before NewMid, and keep that if it costs less.
      auto Try = [&](unsigned From, unsigned To, unsigned NewMid) {
        bool FirstStop = Items[Mid - 1].Stop, SecondStop = Items[End - 1].Stop;
        Rotate(From, To);
        Items[Mid - 1].Stop = false;
        Items[End - 1].Stop = SecondStop;
        Items[NewMid - 1].Stop = FirstStop;
        Dispersal Cost = CostAround(Begin, End);
        if (Cost < Current) {
          Current = Cost;
          Mid = Groups[G].second = Groups[G + 1].first = NewMid;
          Reordered = true;
          ++NumCrossed;
          return true;
        }
        Items[NewMid - 1].Stop = false;
        Items[End - 1].Stop = false;
        Rotate(To, From);
        Items[Mid - 1].Stop = FirstStop;
        Items[End - 1].Stop = SecondStop;
        return false;
      };
      bool Moved = false;
      // Down, to the start of the second group.
      for (unsigned From = Begin; From != Mid && !Moved && Mid - Begin > 1;
           ++From) {
        if (!crossesGroups(Items[From]))
          continue;
        const MachineInstr &MI = *Items[From].MI;
        bool Legal = Items[Mid].MI->getOpcode() != IA64::ALLOC;
        for (unsigned I = From + 1; I != Mid && Legal; ++I)
          Legal = !isFixed(*Items[I].MI) && !mustPrecede(MI, *Items[I].MI, TRI);
        for (unsigned I = Mid; I != End && Legal; ++I)
          Legal = !dependsOn(*Items[I].MI, MI, TRI);
        // A result that takes more than a cycle would come later to the
        // group after.
        for (unsigned I = End; I != Items.size() && Legal; ++I) {
          Legal = SM.computeInstrLatency(&MI) <= 1 ||
                  !dependsOn(*Items[I].MI, MI, TRI);
          if (Items[I].Stop)
            break;
        }
        if (Legal)
          Moved = Try(From, Mid - 1, Mid - 1);
      }
      // Up, to the end of the first group.
      for (unsigned From = Mid; From != End && !Moved && End - Mid > 1;
           ++From) {
        if (!crossesGroups(Items[From]))
          continue;
        const MachineInstr &MI = *Items[From].MI;
        bool Legal = true;
        for (unsigned I = Mid; I != From && Legal; ++I)
          Legal = !isFixed(*Items[I].MI) && !mustPrecede(*Items[I].MI, MI, TRI);
        for (unsigned I = Begin; I != Mid && Legal; ++I)
          Legal = !isFixed(*Items[I].MI) && !dependsOn(MI, *Items[I].MI, TRI);
        // Nor may an operand come later to it.
        for (unsigned I = Begin; I != 0 && Legal; --I) {
          if (I != Begin && Items[I - 1].Stop)
            break;
          Legal = SM.computeInstrLatency(Items[I - 1].MI) <= 1 ||
                  !dependsOn(MI, *Items[I - 1].MI, TRI);
        }
        if (Legal)
          Moved = Try(From, Mid, Mid + 1);
      }
      if (Moved) {
        ReorderGroup(G);
        ReorderGroup(G + 1);
      }
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

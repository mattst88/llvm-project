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
//   - It moves an instruction into the group before or after its own where
//     that costs less. Two instructions that only I0 executes take two
//     cycles in one group, as do three loads or three stores.
//   - It chooses the template of each bundle, together with that of the
//     bundle after it, by the cost of the next two groups, and where that
//     differs from the assembler's choice it has the asm printer emit a
//     template directive.
//
// The I slots of a cycle's bundles take ports I0 and I1 in order, so an
// instruction that only I0 executes must be the first of its cycle on an I
// port, or it and the rest of its bundle wait for the next. Likewise stores,
// setf and getf take ports M2 and M3 in order, and getf only M2, so a getf
// must be the first of them in its cycle.
//
// A label starts a bundle, so the replay restarts at each block that has
// one. Blocks with anything the replay does not model are left alone.
//
// A software-pipelined kernel is costed as a pass after one in the same
// order, since it reads in rotated registers what the pass before wrote.
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
#include <map>

using namespace llvm;

#define DEBUG_TYPE "ia64-pack-groups"
#define PASS_NAME "IA64 instruction group packing"

STATISTIC(NumMoved, "Number of instructions moved within their group");
STATISTIC(NumSeeded, "Number of groups reordered from an order that fills "
                     "bundles");
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
  /// Whether it takes port M2 or M3, as stores, setf and getf do.
  bool IsStore;
  /// Whether only port I0 executes it.
  bool I0Only;
  /// Whether only port M2 executes it.
  bool M2Only;
  /// Whether it is a conditional branch, after which the code that runs may
  /// end.
  bool IsExit;
  /// Whether its group ends after it.
  bool Stop;
  /// The template of a directive before it, or -1.
  int Template = -1;
  /// The registers it reads, with their operands.
  SmallVector<std::pair<Register, unsigned>, 4> Reads;
  /// The registers it writes whose value may take more than a cycle to
  /// reach what reads it, with their operands and the most cycles it takes.
  struct SlowDef {
    Register Reg;
    unsigned Op;
    unsigned Latency;
  };
  SmallVector<SlowDef, 2> SlowDefs;
  const TargetSchedModel *SM;
};

/// The dispersal of bundles into issue cycles, and its cost so far.
struct Dispersal {
  unsigned Cycles = 0;
  /// The sum of the cycles in which conditional branches issue.
  unsigned ExitCycles = 0;
  unsigned Bundles = 0;
  unsigned BundlesNow = 0;
  unsigned Ports[8] = {};
  unsigned Loads = 0;
  unsigned Stores = 0;
  bool EndCycle = true;
  /// The results that take more than a cycle and may not be ready yet: the
  /// register, the cycle that wrote it, when it is ready after that for
  /// all that may read it, and the operand that wrote it.
  struct Pending {
    Register Reg;
    unsigned Issued;
    unsigned Latency;
    const MachineInstr *MI;
    unsigned Op;
  };
  SmallVector<Pending, 8> Results;

  /// Start a cycle that already holds \p Open bundles.
  void newCycle(unsigned Open) {
    ++Cycles;
    BundlesNow = Open;
    std::fill(std::begin(Ports), std::end(Ports), 0);
    Loads = Stores = 0;
    EndCycle = false;
    erase_if(Results,
             [&](const Pending &P) { return P.Issued + P.Latency <= Cycles; });
  }

  /// An instruction that reads a result before it is ready stalls the
  /// whole of its cycle, along with what issued in it before. A result
  /// that takes one cycle is ready for every later group, and one in its
  /// own group reads it under a predicate exclusive with the write's.
  void wait(const Item &It) {
    unsigned Ready = Cycles;
    for (const Pending &P : Results)
      for (auto [Reg, Op] : It.Reads) {
        if (Reg != P.Reg)
          continue;
        unsigned Latency =
            It.SM->computeOperandLatency(P.MI, P.Op, It.MI, Op);
        if (Latency > 1)
          Ready = std::max(Ready, P.Issued + Latency);
      }
    unsigned Stall = Ready - Cycles;
    if (!Stall)
      return;
    for (Pending &P : Results)
      if (P.Issued == Cycles)
        P.Issued += Stall;
    Cycles += Stall;
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
        (It.IsStore && Stores == 2) || (It.I0Only && Ports[Port] != 0) ||
        (It.M2Only && Stores != 0))
      newCycle(1);
    wait(It);
    ++Ports[Port];
    Loads += It.IsLoad;
    Stores += It.IsStore;
    for (const Item::SlowDef &D : It.SlowDefs)
      Results.push_back({D.Reg, Cycles, D.Latency, It.MI, D.Op});
    ExitCycles += It.IsExit ? Cycles : 0;
    EndCycle = It.Stop;
  }

  /// Whether the next bundle starts a cycle. Where the group continues into
  /// the next block, which starts a bundle, that bundle can otherwise issue
  /// with the last.
  bool full() const { return EndCycle || BundlesNow == 2; }

  /// Of two that take as many cycles, the one whose conditional branches
  /// issue sooner costs less where a branch is taken.
  bool operator<(const Dispersal &O) const {
    return std::make_tuple(Cycles, ExitCycles, Bundles, full()) <
           std::make_tuple(O.Cycles, O.ExitCycles, O.Bundles, O.full());
  }

  bool operator==(const Dispersal &O) const {
    return Cycles == O.Cycles && ExitCycles == O.ExitCycles &&
           Bundles == O.Bundles && BundlesNow == O.BundlesNow &&
           std::equal(std::begin(Ports), std::end(Ports),
                      std::begin(O.Ports)) &&
           Loads == O.Loads && Stores == O.Stores && EndCycle == O.EndCycle &&
           equal(Results, O.Results, [](const Pending &A, const Pending &B) {
             return A.Reg == B.Reg && A.Issued == B.Issued &&
                    A.Latency == B.Latency && A.MI == B.MI && A.Op == B.Op;
           });
  }

  /// A cost that no other is less than.
  static Dispersal worst() {
    Dispersal D;
    D.Cycles = ~0u;
    return D;
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

/// The slot units that a bundle template can have, as bits of Item::Units
/// shifted down to start at bit 0.
constexpr unsigned SlotUnitShift = IA64II::UnitI, SlotUnitBits = 6;

/// The template that the assembler chooses for a bundle that starts with
/// instructions of the given units, or ~0u if none holds the first. An
/// instruction that no slot takes ends the bundle as the end of the
/// instructions does.
static unsigned assemblerTemplate(ArrayRef<unsigned> Units) {
  unsigned Index = ~0u, Best = 0;
  for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
    const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
    if (!T.Name || T.MidStop != -1)
      continue;
    unsigned Slot = 0, N = 0;
    for (unsigned U : Units) {
      while (Slot != 3 && !slotTakes(T.Slots[Slot], U))
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

/// The template that the assembler chooses for a bundle starting at
/// \p Items, or ~0u if none holds the first instruction. It depends only
/// on the slot units of the first three, so it is looked up in a table of
/// every choice.
static unsigned assemblerTemplate(ArrayRef<Item> Items) {
  constexpr unsigned Mask = (1u << SlotUnitBits) - 1;
  static const std::vector<uint8_t> Table = [] {
    std::vector<uint8_t> Table(1u << 3 * SlotUnitBits);
    for (unsigned Key = 0; Key != Table.size(); ++Key) {
      unsigned Units[3];
      for (unsigned K = 0; K != 3; ++K)
        Units[K] = (Key >> K * SlotUnitBits & Mask) << SlotUnitShift;
      Table[Key] = assemblerTemplate(Units);
    }
    return Table;
  }();
  unsigned Key = 0;
  for (auto [K, It] : enumerate(Items.take_front(3)))
    Key |= (It.Units >> SlotUnitShift & Mask) << K * SlotUnitBits;
  uint8_t Index = Table[Key];
  return Index == uint8_t(~0u) ? ~0u : Index;
}

/// Pack the bundle that starts at \p Items as IA64ELFStreamer::emitBundle
/// does, and disperse it into \p D. Return how many instructions it holds,
/// or 0 if the first does not fit. A directive on the first instruction
/// gives the template, and one on a later instruction ends the bundle.
/// \p Assembler is the template that the assembler chooses.
static unsigned packBundle(ArrayRef<Item> Items, Dispersal &D,
                           unsigned Assembler) {
  bool Explicit = Items[0].Template != -1;
  unsigned Index = Explicit ? Items[0].Template : Assembler;
  if (Index == ~0u)
    return 0;
  D.startBundle();
  unsigned Taken = 0;
  bool Branch = false;
  // Whether a stop splits the bundle and nothing follows it there yet.
  bool Split = false;
  auto AtEnd = [&] {
    return Taken == Items.size() || Items[Taken].Template != -1;
  };
  for (unsigned Slot = 0; Slot != 3 && (Taken == 0 || !AtEnd()); ++Slot) {
    const Item &It = Items[Taken];
    IA64II::Unit Unit = IA64II::BundleTemplates[Index].Slots[Slot];
    if (!slotTakes(Unit, It.Units) || (Slot != 2 && It.LastSlot))
      continue;
    D.issue(It, Unit);
    Split = false;
    Branch |= Unit == IA64II::UnitB;
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
    Split = true;
  }
  if (Taken == 0)
    --D.Bundles;
  // The rest of a split bundle takes a place in the next cycle, if only
  // with nops.
  else if (Split)
    D.newCycle(1);
  // Itanium 2 issues nothing after an MBB or BBB bundle in its cycle, nor
  // after an MIB, MFB or MMB whose B slot holds a branch.
  else if (Branch || IA64II::BundleTemplates[Index].Slots[1] == IA64II::UnitB)
    D.EndCycle = true;
  return Taken;
}

/// Choose the template of each bundle of \p Items, which start a bundle, by
/// the cost of the bundles up to the end of the next two groups, and set a
/// directive where the choice is not the assembler's. Each bundle is chosen
/// together with the next, since a group of two bundles may issue in one
/// cycle only if both change, as MII,MII to MMI,MMI. \p D is the dispersal
/// of what comes before. Return the dispersal of the whole.
static Dispersal plan(MutableArrayRef<Item> Items, Dispersal D) {
  // The assembler's template for each bundle, which depends only on its
  // first three instructions.
  SmallVector<unsigned, 64> Assembler;
  for (size_t I = 0; I != Items.size(); ++I) {
    Items[I].Template = -1;
    Assembler.push_back(assemblerTemplate(Items.drop_front(I)));
  }
  // Pack the bundle that starts at \p I, of the instructions before \p End.
  auto Pack = [&](size_t I, size_t End, Dispersal &Out) {
    ArrayRef<Item> Bundle = Items.slice(I, End - I);
    return packBundle(Bundle, Out,
                      End - I >= 3 ? Assembler[I] : assemblerTemplate(Bundle));
  };
  size_t Next = 0;
  while (Next != Items.size()) {
    size_t Horizon = Next;
    for (unsigned Stops = 0; Horizon != Items.size() && Stops != 2;)
      Stops += Items[Horizon++].Stop;
    // The bundles after those with a directive are the assembler's, so two
    // choices that leave the same dispersal at the same place go on to cost
    // the same. Tails holds what those bundles cost from where they start.
    struct Tail {
      size_t At;
      Dispersal In, Out;
      bool Fits;
    };
    SmallVector<Tail, 16> Tails;
    // Add to \p Out the cost of the bundles from \p I to the horizon.
    auto Rest = [&](size_t I, Dispersal &Out) {
      while (I < Horizon) {
        if (Items[I].Template == -1) {
          for (const Tail &T : Tails) {
            if (T.At == I && T.In == Out) {
              Out = T.Out;
              return T.Fits;
            }
          }
          Tails.push_back({I, Out, Out, false});
          size_t J = I;
          do {
            unsigned Taken = Pack(J, Horizon, Out);
            if (Taken == 0)
              return false;
            J += Taken;
          } while (J < Horizon);
          Tails.back().Out = Out;
          Tails.back().Fits = true;
          return true;
        }
        unsigned Taken = Pack(I, Horizon, Out);
        if (Taken == 0)
          return false;
        I += Taken;
      }
      return true;
    };
    // The cost of the instructions before the horizon if this bundle has the
    // given template. Every choice packs the same ones, so that one that
    // fills its last bundle further does not look worse.
    auto Try = [&](int Template, Dispersal &Out) {
      Out = D;
      Items[Next].Template = Template;
      unsigned Taken = Pack(Next, Horizon, Out);
      return Taken != 0 && Rest(Next + Taken, Out);
    };
    // The templates to try for a bundle starting at \p At: the assembler's,
    // as -1, and the others that it does not choose.
    auto Choices = [&](size_t At) {
      SmallVector<int, 16> Result = {-1};
      for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
        const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
        if (T.Name && T.MidStop == -1 && I != Assembler[At])
          Result.push_back(I);
      }
      return Result;
    };
    // A choice for the next bundle too must do strictly better than leaving
    // it to the assembler. Thens holds the least that a choice for the next
    // bundle costs, by where that starts.
    struct Then {
      size_t At;
      Dispersal In, Least;
    };
    SmallVector<Then, 4> Thens;
    int Best = -2;
    Dispersal BestCost = Dispersal::worst();
    for (bool Pairs : {false, true}) {
      for (int First : Choices(Next)) {
        Items[Next].Template = First;
        Dispersal Bundle = D;
        size_t Second = Next + Pack(Next, Items.size(), Bundle);
        if (Second == Next || (Pairs && Second >= Horizon))
          continue;
        if (!Pairs) {
          Dispersal Cost;
          if (Try(First, Cost) && Cost < BestCost) {
            Best = First;
            BestCost = Cost;
          }
          continue;
        }
        auto Seen = find_if(Thens, [&](const Then &T) {
          return T.At == Second && T.In == Bundle;
        });
        if (Seen == Thens.end()) {
          // This bundle packs the same whatever the next one's template is,
          // since only that it has a directive ends this one.
          SmallVector<int, 16> Seconds = Choices(Second);
          Items[Second].Template = Seconds[1];
          Dispersal Head = D;
          unsigned Taken = Pack(Next, Horizon, Head);
          Dispersal Least = Dispersal::worst();
          for (int Template : drop_begin(Seconds)) {
            Items[Second].Template = Template;
            Dispersal Cost = Head;
            if (Taken != 0 && Rest(Next + Taken, Cost) && Cost < Least)
              Least = Cost;
          }
          Items[Second].Template = -1;
          Thens.push_back({Second, Bundle, Least});
          Seen = std::prev(Thens.end());
        }
        if (Seen->Least < BestCost) {
          Best = First;
          BestCost = Seen->Least;
        }
      }
    }
    assert(Best != -2 && "no template holds the instruction");
    Items[Next].Template = Best;
    Next += Pack(Next, Items.size(), D);
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

/// The index of the scheduling model's resource \p Name, or 0 if it has
/// none.
static unsigned getPort(const TargetSchedModel &SM, StringRef Name) {
  if (!SM.hasInstrSchedModel())
    return 0;
  const MCSchedModel &M = *SM.getMCSchedModel();
  for (unsigned I = 1; I != M.getNumProcResourceKinds(); ++I)
    if (M.getProcResource(I)->Name == Name)
      return I;
  return 0;
}

/// Whether \p MI is a prefetch, which is marked as storing but takes only a
/// load's port.
static bool isPrefetch(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case IA64::LFETCH:
  case IA64::LFETCH_r:
  case IA64::LFETCH_i:
  case IA64::LFETCH_EXCL:
  case IA64::LFETCH_EXCL_r:
  case IA64::LFETCH_EXCL_i:
  case IA64::LFETCH_FAULT:
  case IA64::LFETCH_FAULT_r:
  case IA64::LFETCH_FAULT_i:
  case IA64::LFETCH_FAULT_EXCL:
  case IA64::LFETCH_FAULT_EXCL_r:
  case IA64::LFETCH_FAULT_EXCL_i:
  case IA64::LFETCH_COUNT:
    return true;
  default:
    return false;
  }
}

/// Whether the scheduling model has \p MI use the resource \p Port.
static bool usesPort(const MachineInstr &MI, const TargetSchedModel &SM,
                     unsigned Port) {
  if (!Port)
    return false;
  const MCSchedClassDesc *SC = SM.resolveSchedClass(&MI);
  if (!SC->isValid())
    return false;
  return any_of(
      make_range(SM.getWriteProcResBegin(SC), SM.getWriteProcResEnd(SC)),
      [&](const MCWriteProcResEntry &E) {
        return E.ProcResourceIdx == Port && E.ReleaseAtCycle;
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

/// An order of \p Group that fills bundles: each next bundle takes the
/// template whose slots hold the most of the instructions that may come
/// next, those that fit only one unit counting twice, and each slot the
/// first of them that fits the fewest units. Of templates that hold as
/// many, it takes the one that leaves the fewest bundles at least to hold
/// the instructions that fit only one unit.
static SmallVector<unsigned, 16> fillingOrder(ArrayRef<Item> Group,
                                              const TargetRegisterInfo &TRI) {
  unsigned N = Group.size();
  SmallVector<bool, 16> Placed(N, false);
  auto Ready = [&](unsigned K) {
    for (unsigned J = 0; J != K; ++J)
      if (!Placed[J] && mustPrecede(*Group[J].MI, *Group[K].MI, TRI))
        return false;
    return true;
  };
  // The bundles that the instructions not yet placed, which fit only one
  // unit, need at least: a bundle has one F slot, at most two I or two M
  // slots, and three B slots.
  auto LeastBundles = [&] {
    unsigned F = 0, I = 0, M = 0, B = 0;
    for (unsigned K = 0; K != N; ++K) {
      if (Placed[K])
        continue;
      unsigned Units = Group[K].Units;
      F += Units == 1u << IA64II::UnitF;
      I += Units == 1u << IA64II::UnitI;
      M += Units == 1u << IA64II::UnitM;
      B += Units == 1u << IA64II::UnitB;
    }
    return std::max({F, divideCeil(I, 2), divideCeil(M, 2), divideCeil(B, 3)});
  };
  SmallVector<unsigned, 16> Order;
  while (Order.size() != N) {
    SmallVector<unsigned, 3> Best;
    unsigned BestScore = 0, BestLeast = 0;
    for (const IA64II::BundleTemplate &T : IA64II::BundleTemplates) {
      if (!T.Name || T.MidStop != -1)
        continue;
      SmallVector<unsigned, 3> Taken;
      unsigned Score = 0;
      for (unsigned Slot = 0; Slot < 3; ++Slot) {
        int Pick = -1;
        for (unsigned K = 0; K != N; ++K) {
          if (Placed[K] || !slotTakes(T.Slots[Slot], Group[K].Units) ||
              (Slot != 2 && Group[K].LastSlot) || !Ready(K))
            continue;
          if (Pick < 0 ||
              popcount(Group[K].Units) < popcount(Group[Pick].Units))
            Pick = K;
        }
        if (Pick < 0)
          continue;
        Placed[Pick] = true;
        Taken.push_back(Pick);
        Score += popcount(Group[Pick].Units) == 1 ? 2 : 1;
        if (T.Slots[Slot] == IA64II::UnitL)
          ++Slot;
      }
      unsigned Least = LeastBundles();
      for (unsigned K : Taken)
        Placed[K] = false;
      if (Score > BestScore || (Score == BestScore && Least < BestLeast)) {
        BestScore = Score;
        BestLeast = Least;
        Best = Taken;
      }
    }
    if (Best.empty())
      return {};
    for (unsigned K : Best) {
      Placed[K] = true;
      Order.push_back(K);
    }
  }
  return Order;
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

/// Whether \p It may move to the group before or after its own: neither a
/// volatile or atomic access nor a write of sp.
static bool crossesGroups(const Item &It) {
  const MachineInstr &MI = *It.MI;
  return !isFixed(MI) && !(MI.mayLoadOrStore() && MI.hasOrderedMemoryRef()) &&
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
/// The name that \p Reg has after br.ctop rotates the registers, in a function
/// that rotates \p GRs general registers.
static Register rotated(Register Reg, unsigned GRs) {
  auto Next = [&](unsigned First, unsigned Size) -> Register {
    unsigned I = Reg - First;
    return I < Size ? Register(First + (I + 1) % Size) : Reg;
  };
  if (Reg >= IA64::R32 && Reg < IA64::R32 + GRs)
    return Next(IA64::R32, GRs);
  if (Reg >= IA64::F32 && Reg <= IA64::F127)
    return Next(IA64::F32, 96);
  if (Reg >= IA64::P16 && Reg <= IA64::P63)
    return Next(IA64::P16, 48);
  return Reg;
}

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
  unsigned PortI0 = getPort(SM, "I2PortI0");
  unsigned PortM2 = getPort(SM, "I2PortM2");
  unsigned PortM23 = getPort(SM, "I2PortM23");
  bool Changed = false;

  // The instructions since the last start of a bundle. After a block that
  // is not modeled, the assembler may pack them with the instructions before,
  // which only makes the plan less accurate.
  SmallVector<Item> Run;
  // The dispersal before the run. A group left open where a block falls
  // through to a label continues in the block's first bundle.
  Dispersal Before;
  auto PlanRun = [&] {
    if (Run.empty())
      return;
    Before = plan(Run, Before);
    for (const Item &It : Run)
      if (It.Template != -1) {
        Templates[It.MI] = It.Template;
        ++NumTemplates;
      }
    Run.clear();
  };

  auto emitsNothing = [](const MachineInstr &MI) {
    return MI.isDebugInstr() || MI.getOpcode() == TargetOpcode::KILL ||
           MI.getOpcode() == TargetOpcode::IMPLICIT_DEF;
  };
  // Anything else may emit a label, or code the packing does not know.
  auto UnitsOf = [&](const MachineInstr &MI) -> unsigned {
    return MI.isMetaInstruction() || MI.isInlineAsm() ||
                   MI.getPreInstrSymbol() || MI.getPostInstrSymbol()
               ? 0
               : getUnits(MI, MCII, MRI);
  };
  // The most cycles that the result of operand \p Op of \p MI takes to
  // reach an ALU or a later read in its block. Only a result that some read
  // waits more than a cycle for joins a dispersal's pending results, which
  // planning copies and compares often; most reads wait no longer than an
  // ALU does.
  auto SlowestRead = [&](const MachineInstr &MI, unsigned Op) {
    Register Reg = MI.getOperand(Op).getReg();
    unsigned Latency = SM.computeOperandLatency(&MI, Op, nullptr, 0);
    unsigned Scanned = 0;
    for (const MachineInstr &Use : make_range(
             std::next(MI.getIterator()), MI.getParent()->instr_end())) {
      if (++Scanned > 2 * MaxGroupSize)
        break;
      for (const MachineOperand &MO : Use.explicit_operands())
        if (MO.isReg() && MO.readsReg() && MO.getReg() == Reg)
          Latency = std::max(Latency, SM.computeOperandLatency(
                                          &MI, Op, &Use, MO.getOperandNo()));
      if (Use.modifiesRegister(Reg, &TRI))
        break;
    }
    return Latency;
  };
  auto MakeItem = [&](MachineInstr &MI, unsigned Units) -> Item {
    Item It{&MI,
            Units,
            (MI.getDesc().TSFlags & IA64II::LastSlot) != 0,
            MI.mayLoad(),
            (MI.mayStore() && !isPrefetch(MI)) ||
                usesPort(MI, SM, PortM23) || usesPort(MI, SM, PortM2),
            usesPort(MI, SM, PortI0),
            usesPort(MI, SM, PortM2),
            MI.isConditionalBranch(),
            MI.getAsmPrinterFlag(IA64::StopAfter)};
    It.SM = &SM;
    for (const MachineOperand &MO : MI.explicit_operands()) {
      if (!MO.isReg() || !MO.getReg() || MO.getReg() == IA64::P0)
        continue;
      unsigned Op = MO.getOperandNo();
      if (MO.readsReg()) {
        It.Reads.push_back({MO.getReg(), Op});
      } else if (MO.isDef() && !MO.isDead()) {
        unsigned Latency = SlowestRead(MI, Op);
        if (Latency > 1)
          It.SlowDefs.push_back({MO.getReg(), Op, Latency});
      }
    }
    return It;
  };

  // Whether the instruction group open at the end of the last block may
  // continue into this one.
  bool OpenBefore = false;
  for (MachineBasicBlock &MBB : MF) {
    if (startsBundle(MBB)) {
      PlanRun();
      MachineBasicBlock *Prev = MBB.getPrevNode();
      if (!Prev || !Prev->isSuccessor(&MBB) || !Prev->canFallThrough())
        Before = Dispersal();
    }

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
      if (emitsNothing(MI)) {
        (Items.empty() ? Leading : Attached.back()).push_back(&MI);
        continue;
      }
      unsigned Units = UnitsOf(MI);
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
      Items.push_back(MakeItem(MI, Units));
      Attached.emplace_back();
      Movable &= !pinsGroup(MI);
      if (Stop)
        EndGroup();
    }
    if (Opaque) {
      PlanRun();
      Before = Dispersal();
      OpenBefore = true;
      continue;
    }
    if (Movable && GroupStart < Items.size())
      Groups.push_back({GroupStart, Items.size()});

    // Where the block's first group continues one of the block before, the
    // start of that group, which the block's first group shares cycles with.
    unsigned OpenStart = Run.size();
    if (OpenBefore && !Run.empty()) {
      OpenStart = Run.size() - 1;
      while (OpenStart != 0 && !Run[OpenStart - 1].Stop)
        --OpenStart;
    }
    // Where the block's last group continues into the next one, the next
    // block's instructions in that group, up to NextMid, and in the group
    // after. Next is empty where they are not known.
    SmallVector<Item> Next;
    unsigned NextMid = 0;
    // Whether Next holds the whole of the group after.
    bool NextAfter = false;
    MachineBasicBlock *NextMBB = MBB.getNextNode();
    if (!Items.empty() && !Items.back().Stop && NextMBB &&
        !startsBundle(*NextMBB)) {
      for (MachineInstr &MI : *NextMBB) {
        if (emitsNothing(MI))
          continue;
        unsigned Units = UnitsOf(MI);
        if (Units == 0 || MI.getOpcode() == IA64::ALLOC)
          break;
        Next.push_back(MakeItem(MI, Units));
        if (!Next.back().Stop)
          continue;
        if (NextMid) {
          NextAfter = true;
          break;
        }
        NextMid = Next.size();
      }
      if (!NextMid)
        Next.clear();
    }
    // A software-pipelined kernel reads, in rotated registers, results that
    // its last pass wrote, and some of them late in it. Its cost is that of
    // a pass after one in the same order.
    bool Kernel = MBB.isSuccessor(&MBB) &&
                  any_of(MBB.terminators(), [](const MachineInstr &MI) {
                    return MI.getOpcode() == IA64::BR_CTOP;
                  });
    unsigned GRs = MF.getInfo<IA64MachineFunctionInfo>()->getRotatingGRs();
    // The cost of the instructions from Begin to End, and of the groups
    // before and after them.
    auto CostAround = [&](unsigned Begin, unsigned End) {
      if (Kernel) {
        SmallVector<Item, 32> Window(Items.begin(), Items.end());
        for (Item &It : Window) {
          for (auto &[Reg, Op] : It.Reads)
            Reg = rotated(Reg, GRs);
          for (Item::SlowDef &D : It.SlowDefs)
            D.Reg = rotated(D.Reg, GRs);
        }
        Window.append(Items.begin(), Items.end());
        return plan(Window, Dispersal());
      }
      if (Begin != 0)
        --Begin;
      while (Begin != 0 && !Items[Begin - 1].Stop)
        --Begin;
      while (End != Items.size() && !Items[End].Stop)
        ++End;
      End = std::min<unsigned>(End + 1, Items.size());
      SmallVector<Item, 16> Window;
      if (Begin == 0)
        Window.append(Run.begin() + OpenStart, Run.end());
      Window.append(Items.begin() + Begin, Items.begin() + End);
      if (End == Items.size())
        Window.append(Next.begin(), Next.begin() + NextMid);
      return plan(Window, Dispersal());
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
      // The search comes back to orders it has costed.
      std::map<SmallVector<const MachineInstr *, 16>, Dispersal> Costed;
      auto Evaluate = [&] {
        SmallVector<const MachineInstr *, 16> Order;
        for (unsigned I = Begin; I != End; ++I)
          Order.push_back(Items[I].MI);
        auto [It, New] = Costed.try_emplace(Order);
        if (New) {
          Items[End - 1].Stop = Stop;
          It->second = CostAround(Begin, End);
          Items[End - 1].Stop = false;
        }
        return It->second;
      };
      // Move one instruction at a time while that costs less.
      auto Climb = [&](Dispersal Current) {
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
                Improved = true;
                ++NumMoved;
                break;
              }
              Rotate(To, From);
            }
          }
        }
        return Current;
      };
      SmallVector<Item, 16> Original(Items.begin() + Begin,
                                     Items.begin() + End);
      SmallVector<SmallVector<MachineInstr *, 1>, 16> OriginalAttached(
          Attached.begin() + Begin, Attached.begin() + End);
      auto Changed = [&] {
        return any_of(seq(Begin, End), [&](unsigned I) {
          return Items[I].MI != Original[I - Begin].MI;
        });
      };
      Dispersal Current = Climb(Evaluate());
      // Moving one instruction at a time cannot fill a bundle that takes
      // moving two. Search again from an order that fills bundles, and keep
      // that where it ends up costing less. The instructions from the first
      // that keeps its place stay.
      unsigned FixedFrom = Begin;
      while (FixedFrom != End && !isFixed(*Items[FixedFrom].MI))
        ++FixedFrom;
      SmallVector<unsigned, 16> Order;
      if (FixedFrom - Begin > 2)
        Order =
            fillingOrder(ArrayRef(Original).take_front(FixedFrom - Begin), TRI);
      if (!Order.empty()) {
        SmallVector<Item, 16> Climbed(Items.begin() + Begin,
                                      Items.begin() + End);
        SmallVector<SmallVector<MachineInstr *, 1>, 16> ClimbedAttached(
            Attached.begin() + Begin, Attached.begin() + End);
        std::copy(Original.begin(), Original.end(), Items.begin() + Begin);
        std::copy(OriginalAttached.begin(), OriginalAttached.end(),
                  Attached.begin() + Begin);
        for (auto [P, K] : enumerate(Order)) {
          Items[Begin + P] = Original[K];
          Attached[Begin + P] = OriginalAttached[K];
        }
        Dispersal Seeded = Climb(Evaluate());
        if (Seeded < Current) {
          Current = Seeded;
          ++NumSeeded;
        } else {
          std::copy(Climbed.begin(), Climbed.end(), Items.begin() + Begin);
          std::copy(ClimbedAttached.begin(), ClimbedAttached.end(),
                    Attached.begin() + Begin);
        }
      }
      Reordered |= Changed();
      Items[End - 1].Stop = Stop;
    };
    for (unsigned G = 0; G != Groups.size(); ++G)
      ReorderGroup(G);

    // Two instructions that only I0 executes take two cycles in one group,
    // as do three loads or three stores, and a group whose instructions wait
    // for a load may hold several. Move one into the group before or after,
    // in the same block, where nothing there keeps it out and that costs
    // less.
    for (unsigned G = 0; G + 1 < Groups.size(); ++G) {
      unsigned Begin = Groups[G].first, Mid = Groups[G].second;
      unsigned End = Groups[G + 1].second;
      if (Groups[G + 1].first != Mid)
        continue;
      Dispersal Current = CostAround(Begin, End);
      // Move the instruction at From to To, with the first group ending
      // before NewMid, and keep that if it costs less.
      // The first group may move entirely into the second where it
      // continues one of the block before, which then ends instead.
      auto StopBefore = [&](unsigned NewMid) -> bool & {
        return NewMid ? Items[NewMid - 1].Stop : Run.back().Stop;
      };
      auto Try = [&](unsigned From, unsigned To, unsigned NewMid) {
        bool FirstStop = Items[Mid - 1].Stop, SecondStop = Items[End - 1].Stop;
        Rotate(From, To);
        Items[Mid - 1].Stop = false;
        Items[End - 1].Stop = SecondStop;
        StopBefore(NewMid) = FirstStop;
        Dispersal Cost = CostAround(Begin, End);
        if (Cost < Current) {
          Current = Cost;
          Mid = Groups[G].second = Groups[G + 1].first = NewMid;
          if (NewMid == 0)
            Run.back().MI->setAsmPrinterFlag(IA64::StopAfter);
          Reordered = true;
          ++NumCrossed;
          return true;
        }
        StopBefore(NewMid) = false;
        Items[End - 1].Stop = false;
        Rotate(To, From);
        Items[Mid - 1].Stop = FirstStop;
        Items[End - 1].Stop = SecondStop;
        return false;
      };
      auto StartsGroup = [&](unsigned B, unsigned E) {
        return any_of(make_range(Items.begin() + B, Items.begin() + E),
                      [](const Item &It) {
                        return It.MI->getAsmPrinterFlag(IA64::StartsGroup);
                      });
      };
      bool Moved = false;
      // A group that continues from the block before or into the block after
      // has instructions this does not see, unless they are in Next.
      bool OpenAfter = !Items.back().Stop;
      bool IntoNext = OpenAfter && End == Items.size();
      bool Empties = Begin == 0 && OpenStart != Run.size() && !Run.back().Stop;
      // Down, to the start of the second group.
      for (unsigned From = Begin;
           From != Mid && !Moved && (Mid - Begin > 1 || Empties) &&
           !(IntoNext && Next.empty());
           ++From) {
        if (!crossesGroups(Items[From]))
          continue;
        const MachineInstr &MI = *Items[From].MI;
        // Nor into a group that must start after the one it leaves.
        bool Legal =
            Items[Mid].MI->getOpcode() != IA64::ALLOC && !StartsGroup(Mid, End);
        for (unsigned I = From + 1; I != Mid && Legal; ++I)
          Legal = !isFixed(*Items[I].MI) && !mustPrecede(MI, *Items[I].MI, TRI);
        for (unsigned I = Mid; I != End && Legal; ++I)
          Legal = !dependsOn(*Items[I].MI, MI, TRI);
        bool Quick = SM.computeInstrLatency(&MI) <= 1;
        // A result that takes more than a cycle would come later to the
        // group after.
        for (unsigned I = End; I != Items.size() && Legal; ++I) {
          Legal = Quick || !dependsOn(*Items[I].MI, MI, TRI);
          if (Items[I].Stop)
            break;
        }
        if (IntoNext) {
          for (unsigned I = 0; I != NextMid && Legal; ++I)
            Legal = !dependsOn(*Next[I].MI, MI, TRI);
          Legal &= Quick || NextAfter;
          for (unsigned I = NextMid; I != Next.size() && Legal; ++I)
            Legal = Quick || !dependsOn(*Next[I].MI, MI, TRI);
        }
        if (Legal)
          Moved = Try(From, Mid - 1, Mid - 1);
      }
      // Up, to the end of the first group.
      for (unsigned From = Mid; From != End && !Moved && End - Mid > 1 &&
                                !(OpenBefore && Begin == 0);
           ++From) {
        if (!crossesGroups(Items[From]))
          continue;
        const MachineInstr &MI = *Items[From].MI;
        bool Legal = !MI.getAsmPrinterFlag(IA64::StartsGroup);
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
    if (!Items.empty())
      OpenBefore = !Items.back().Stop;
    Run.append(Items.begin(), Items.end());
  }
  PlanRun();
  return Changed || !Templates.empty();
}

FunctionPass *llvm::createIA64PackGroupsPass() { return new IA64PackGroups(); }

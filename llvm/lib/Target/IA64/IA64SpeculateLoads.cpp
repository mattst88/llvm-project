//===-- IA64SpeculateLoads.cpp - Issue loads early with ld.sa -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A load waits for the branches that guard it and the stores that may write
// what it reads. ld.sa issues it before both: a fault it would take is
// deferred, and the ALAT records its address, which a later store to the
// same place removes. ld.c.clr then stands where the load was. It leaves the
// register as ld.sa wrote it if the ALAT still has the entry for it, and
// otherwise loads again, so a deferred fault or an intervening store costs
// only the second load.
//
// IA64SpeculateLoads moves a load up through the blocks that lead only to its
// own, as long as it would still be used most of the times it is issued,
// taking along the few instructions that compute its address. Each pair
// costs an M slot more than the load, which pays only if the load would
// otherwise wait for memory, so the pass moves only loads whose address comes
// from another load: those are the ones that miss the cache. Of the loads in
// a block from near the same address, only the first moves, since a miss
// brings in the line the others read. Before register allocation the pair is
// SPEC_LD and CHECK_LD, with the check's result tied to the early load's.
//
// Other loads stay, but the instructions that compute their address move up
// in the same way, if the load is reached often enough. Integer operations
// cannot fault, and in the earlier blocks they take slots that would be left
// empty, so the address is ready once the branches before the load resolve.
//
// The check finds its entry by register number, so it is sound only if the
// register it reads is the one ld.sa wrote, with no other write between.
// Register allocation may break that by copying or spilling the value, and
// a spill of a value whose load was deferred would itself fault. Once no
// pass can change the code between them, IA64ExpandSpeculation checks each
// pair. A pair that still holds becomes ld.sa and ld.c.clr; otherwise the
// early load becomes a copy of zero and the check the plain load, since its
// value is used only by the check.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-speculate-loads"

STATISTIC(NumSpeculated, "Number of loads issued early with ld.sa");
STATISTIC(NumChecked, "Number of ld.sa and ld.c.clr pairs emitted");
STATISTIC(NumUndone, "Number of pairs that became the plain load again");
STATISTIC(NumAddresses, "Number of load addresses computed early");
STATISTIC(NumOperands, "Number of store or branch operands computed early");

static cl::opt<bool> EnableSpeculation(
    "ia64-load-speculation", cl::init(true), cl::Hidden,
    cl::desc("Issue loads ahead of branches and stores with ld.sa"));

static cl::opt<unsigned> MinUsePercent(
    "ia64-speculate-min-use", cl::init(75), cl::Hidden,
    cl::desc("How often, in percent, an early load must reach its use"));

static cl::opt<unsigned>
    MaxDistance("ia64-speculate-max-distance", cl::init(40), cl::Hidden,
                cl::desc("How many instructions a load may move across"));

static cl::opt<unsigned>
    MinDistance("ia64-speculate-min-distance", cl::init(4), cl::Hidden,
                cl::desc("How many instructions a load must move across"));

static cl::opt<unsigned> MaxChain(
    "ia64-speculate-max-chain", cl::init(4), cl::Hidden,
    cl::desc("How many instructions computing the address may move with "
             "a load"));

static cl::opt<bool> OnlyChases(
    "ia64-speculate-only-chases", cl::init(true), cl::Hidden,
    cl::desc("Issue early only loads whose address comes from a load"));

static cl::opt<unsigned> AddressMinUse(
    "ia64-early-address-min-use", cl::init(40), cl::Hidden,
    cl::desc("How often, in percent, the address of a load that is not "
             "issued early must be used where it is computed"));

static cl::opt<unsigned> OperandMinUse(
    "ia64-early-operand-min-use", cl::init(60), cl::Hidden,
    cl::desc("How often, in percent, the operands of a store or branch must "
             "be used where they are computed"));

static cl::opt<bool> EarlyStoreOperands(
    "ia64-early-store-operands", cl::init(true), cl::Hidden,
    cl::desc("Compute the address and value of a store early"));

static cl::opt<bool> EarlyBranchOperands(
    "ia64-early-branch-operands", cl::init(true), cl::Hidden,
    cl::desc("Compute the operands of a conditional branch's compare early"));

static cl::opt<bool> OnePerLine(
    "ia64-speculate-one-per-line", cl::init(true), cl::Hidden,
    cl::desc("Issue early only the first of the loads in a block from "
             "near the same address"));

static cl::opt<unsigned> ChaseDepth(
    "ia64-speculate-chase-depth", cl::init(5), cl::Hidden,
    cl::desc("How many instructions back from the address a load may be"));

namespace {

struct Forms {
  unsigned Spec, Check;
};

} // end anonymous namespace

// The speculative and check forms of a plain load.
static std::optional<Forms> getForms(unsigned Opcode) {
  switch (Opcode) {
  default:
    return std::nullopt;
  case IA64::LD1:
    return Forms{IA64::LD1_SA, IA64::LD1_C_CLR};
  case IA64::LD2:
    return Forms{IA64::LD2_SA, IA64::LD2_C_CLR};
  case IA64::LD4:
    return Forms{IA64::LD4_SA, IA64::LD4_C_CLR};
  case IA64::LD8:
    return Forms{IA64::LD8_SA, IA64::LD8_C_CLR};
  case IA64::LDFS:
    return Forms{IA64::LDFS_SA, IA64::LDFS_C_CLR};
  case IA64::LDFD:
    return Forms{IA64::LDFD_SA, IA64::LDFD_C_CLR};
  }
}

static bool isPlainStore(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case IA64::ST1:
  case IA64::ST2:
  case IA64::ST4:
  case IA64::ST8:
  case IA64::STFS:
  case IA64::STFD:
    return true;
  }
}

// Whether a load may not move above MI.
static bool isBarrier(const MachineInstr &MI) {
  return MI.isCall() || MI.isInlineAsm() || MI.isPosition() ||
         MI.hasUnmodeledSideEffects() || MI.hasOrderedMemoryRef();
}

namespace {

class IA64SpeculateLoads : public MachineFunctionPass {
public:
  static char ID;

  IA64SpeculateLoads() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "IA64 load speculation"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  bool isMovable(const MachineInstr &MI) const;
  bool chases(Register Addr, const MachineLoop *L) const;
  struct Walk {
    MachineBasicBlock *MBB = nullptr;
    MachineBasicBlock::iterator At;
    SmallVector<MachineInstr *, 4> Chain;
    bool Crossed = false;
    unsigned Distance = 0;
  };
  Walk walkUp(MachineInstr &User, ArrayRef<Register> Regs, unsigned MinUse,
              const MachineInstr *Load) const;
  bool moveChain(Walk &W, const MachineInstr &User);
  std::pair<Register, int64_t> splitAddress(Register Addr) const;
  bool speculate(MachineInstr &Load, unsigned Pair);
  bool hoistOperands(MachineInstr &User);

  const IA64InstrInfo *TII;
  MachineRegisterInfo *MRI;
  const MachineBranchProbabilityInfo *MBPI;
  MachineLoopInfo *MLI;
  AAResults *AA;
  bool Changed;
};

} // end anonymous namespace

char IA64SpeculateLoads::ID = 0;

INITIALIZE_PASS_BEGIN(IA64SpeculateLoads, DEBUG_TYPE, "IA64 load speculation",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(IA64SpeculateLoads, DEBUG_TYPE, "IA64 load speculation",
                    false, false)

// Whether MI computes a general register from virtual registers and nothing
// else, so it can move up with the load whose address it computes. Integer
// operations cannot fault or set flags, so they may run where they are not
// needed.
bool IA64SpeculateLoads::isMovable(const MachineInstr &MI) const {
  if (MI.mayLoadOrStore() || MI.hasUnmodeledSideEffects() || MI.isPHI() ||
      MI.isCall() || MI.isTerminator() || MI.isInlineAsm() || MI.isPosition() ||
      MI.isDebugInstr())
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg())
      continue;
    Register Reg = MO.getReg();
    if (MO.isDef()) {
      if (!Reg.isVirtual() ||
          !IA64::GRRegClass.hasSubClassEq(MRI->getRegClass(Reg)))
        return false;
    } else if (!Reg.isVirtual() && Reg != IA64::P0 && Reg != IA64::R0 &&
               !MRI->isConstantPhysReg(Reg)) {
      return false;
    }
  }
  return true;
}

// Whether Addr is computed from a value loaded in L, or anywhere if L is
// null. Such an address is as hard to predict as the value, so the load is
// the likeliest to miss the cache, and the one that most repays issuing
// early. A load from an address that steps through an array finds it in the
// cache, and the check would only take an M slot.
bool IA64SpeculateLoads::chases(Register Addr, const MachineLoop *L) const {
  SmallVector<std::pair<Register, unsigned>, 8> Work{{Addr, 0}};
  SmallSet<Register, 16> Seen;
  while (!Work.empty()) {
    auto [Reg, Depth] = Work.pop_back_val();
    if (!Reg.isVirtual() || !Seen.insert(Reg).second)
      continue;
    const MachineInstr *Def = MRI->getVRegDef(Reg);
    if (!Def || (L && !L->contains(Def)))
      continue;
    // A post-increment load also writes its address register, which steps
    // like any other. An asm that clobbers memory may load too, but its
    // first operand is its text.
    if (Def->mayLoad() && !Def->isInlineAsm() &&
        Def->getOperand(0).getReg() == Reg)
      return true;
    // A value loaded further back is more likely the bound of an index that
    // steps through an array than the index itself.
    if (Depth == ChaseDepth || Def->mayStore() || Def->isCall() ||
        Def->hasUnmodeledSideEffects())
      continue;
    for (const MachineOperand &MO : Def->uses())
      if (MO.isReg())
        Work.push_back({MO.getReg(), Depth + 1});
  }
  return false;
}

// Walk up from User to the definitions of Regs, or to a block that other
// paths also enter, or that is entered too often for User to be reached at
// least MinUse percent of the time. Regs are often computed just before
// User, after the branches they could move past, so the few instructions
// that compute them from values available earlier are collected to move.
// Whether the walk crossed anything the scheduler could not move User past
// is recorded: a branch, or a store that Load, if given, may alias.
IA64SpeculateLoads::Walk
IA64SpeculateLoads::walkUp(MachineInstr &User, ArrayRef<Register> Regs,
                           unsigned MinUse, const MachineInstr *Load) const {
  Walk W;
  SmallSet<Register, 8> Needed;
  Needed.insert_range(Regs);
  auto DefinesNeeded = [&](const MachineInstr &MI) {
    return any_of(MI.all_defs(), [&](const MachineOperand &MO) {
      return Needed.count(MO.getReg());
    });
  };
  const MachineLoop *L = MLI->getLoopFor(User.getParent());
  MachineBasicBlock *MBB = User.getParent();
  MachineBasicBlock::iterator At = User.getIterator();
  // How likely User is to be reached from where the walk is.
  BranchProbability Reach = BranchProbability::getOne();
  SmallVector<MachineInstr *, 4> &Chain = W.Chain;
  bool Crossed = false;
  unsigned Distance = 0;
  // Where the walk entered the current block, to go back to if it stops
  // among the block's terminators.
  MachineBasicBlock *EntryMBB = MBB;
  MachineBasicBlock::iterator EntryAt = At;
  size_t EntryChain = 0;
  bool EntryCrossed = false;
  while (true) {
    MachineBasicBlock::iterator Begin = MBB->getFirstNonPHI();
    bool Stop = false;
    while (At != Begin) {
      MachineInstr &MI = *std::prev(At);
      if (!MI.isDebugInstr()) {
        if (isBarrier(MI) || ++Distance > MaxDistance) {
          Stop = true;
          break;
        }
        if (DefinesNeeded(MI)) {
          if (Chain.size() >= MaxChain || !isMovable(MI)) {
            Stop = true;
            break;
          }
          Chain.push_back(&MI);
          for (const MachineOperand &MO : MI.all_uses())
            if (MO.getReg().isVirtual())
              Needed.insert(MO.getReg());
        }
        // A store the load cannot alias is no obstacle to the scheduler.
        Crossed |= MI.isBranch() || (Load && MI.mayStore() &&
                                     MI.mayAlias(AA, *Load, /*UseTBAA=*/true));
      }
      --At;
    }
    // Nothing placed after one of the terminators would sit between a
    // conditional branch and the branch that follows it.
    if (Stop && std::prev(At)->isTerminator()) {
      MBB = EntryMBB;
      At = EntryAt;
      Chain.truncate(EntryChain);
      Crossed = EntryCrossed;
    }
    // A block with one predecessor may still have PHIs.
    if (Stop || MBB->pred_size() != 1 || MBB->isEHPad() ||
        any_of(MBB->phis(), DefinesNeeded))
      break;
    // Block frequencies at this point can rate a block above its only
    // predecessor, so follow the edges instead.
    MachineBasicBlock *Pred = *MBB->pred_begin();
    Reach *= MBPI->getEdgeProbability(Pred, MBB);
    if (MLI->getLoopFor(Pred) != L || MinUse > 100 ||
        Reach < BranchProbability(MinUse, 100))
      break;
    EntryMBB = MBB;
    EntryAt = Begin;
    EntryChain = Chain.size();
    EntryCrossed = Crossed;
    MBB = Pred;
    At = MBB->end();
  }
  W.MBB = MBB;
  W.At = At;
  W.Crossed = Crossed;
  W.Distance = Distance;
  return W;
}

// Move the chain to where the walk stopped, if that is in another block.
bool IA64SpeculateLoads::moveChain(Walk &W, const MachineInstr &User) {
  if (W.MBB == User.getParent() || W.Chain.empty() || !W.Crossed ||
      W.Distance < MinDistance)
    return false;
  // The chain was found from User up, so the last found goes first.
  for (MachineInstr *MI : reverse(W.Chain)) {
    // The walk may have stopped just above the chain, which then begins
    // where it stopped.
    if (W.At != W.MBB->end() && &*W.At == MI)
      ++W.At;
    else
      W.MBB->splice(W.At, MI->getParent(), MI->getIterator());
    for (const MachineOperand &MO : MI->all_uses())
      if (MO.getReg().isVirtual())
        MRI->clearKillFlags(MO.getReg());
  }
  Changed = true;
  return true;
}

// The register Addr adds a constant to, and the constant.
std::pair<Register, int64_t>
IA64SpeculateLoads::splitAddress(Register Addr) const {
  int64_t Offset = 0;
  while (Addr.isVirtual()) {
    const MachineInstr *Def = MRI->getVRegDef(Addr);
    if (!Def || Def->getOpcode() != IA64::ADDS || !Def->getOperand(1).isImm() ||
        !Def->getOperand(2).isReg() || !Def->getOperand(2).getReg().isVirtual())
      break;
    Offset += Def->getOperand(1).getImm();
    Addr = Def->getOperand(2).getReg();
  }
  return {Addr, Offset};
}

bool IA64SpeculateLoads::speculate(MachineInstr &Load, unsigned Pair) {
  std::optional<Forms> F = getForms(Load.getOpcode());
  if (!F || Load.getOperand(3).getReg() != IA64::P0 ||
      !Load.hasOneMemOperand() || !(*Load.memoperands_begin())->isUnordered())
    return false;
  Register Addr = Load.getOperand(1).getReg();
  if (!Addr.isVirtual())
    return false;

  const MachineLoop *L = MLI->getLoopFor(Load.getParent());
  // Whether only the address is computed early.
  if (OnlyChases && !chases(Addr, L)) {
    Walk W = walkUp(Load, Addr, AddressMinUse, &Load);
    NumAddresses += moveChain(W, Load);
    return false;
  }

  Walk W = walkUp(Load, Addr, MinUsePercent, &Load);
  // Moving it within a block past nothing the scheduler cannot move it past
  // gains nothing.
  if (!W.Crossed || W.Distance < MinDistance)
    return false;
  MachineBasicBlock *MBB = W.MBB;
  MachineBasicBlock::iterator At = W.At;
  MachineBasicBlock *Use = Load.getParent();
  SmallVector<MachineInstr *, 4> &Chain = W.Chain;

  const TargetRegisterClass *RC = MRI->getRegClass(Load.getOperand(0).getReg());
  bool FP = IA64::FRRegClass.hasSubClassEq(RC);
  Register Early = MRI->createVirtualRegister(RC);
  MachineInstr *Spec =
      BuildMI(*MBB, At, Load.getDebugLoc(),
              TII->get(FP ? IA64::SPEC_LD_FR : IA64::SPEC_LD_GR), Early)
          .addReg(Addr)
          .addImm(Load.getOpcode())
          .addImm(Pair)
          .cloneMemRefs(Load);
  // The chain was found from the load up, so the last found goes first.
  for (MachineInstr *MI : reverse(Chain)) {
    MBB->splice(Spec->getIterator(), MI->getParent(), MI->getIterator());
    for (const MachineOperand &MO : MI->all_uses())
      if (MO.getReg().isVirtual())
        MRI->clearKillFlags(MO.getReg());
  }
  MRI->clearKillFlags(Addr);
  BuildMI(*Use, Load, Load.getDebugLoc(),
          TII->get(FP ? IA64::CHECK_LD_FR : IA64::CHECK_LD_GR),
          Load.getOperand(0).getReg())
      .addReg(Addr)
      .addReg(Early)
      .addImm(Load.getOpcode())
      .addImm(Pair)
      .cloneMemRefs(Load);
  Load.eraseFromParent();
  ++NumSpeculated;
  return true;
}

// A store's operands, and those of the compare a conditional branch becomes,
// are needed when its group issues, which waits for them like any other.
// Compute them early in the same way as a load's address.
bool IA64SpeculateLoads::hoistOperands(MachineInstr &User) {
  int QPIdx = User.findFirstPredOperandIdx();
  SmallVector<Register, 2> Regs;
  for (const MachineOperand &MO : User.all_uses())
    if (MO.getReg().isVirtual() && (int)MO.getOperandNo() != QPIdx)
      Regs.push_back(MO.getReg());
  if (Regs.empty())
    return false;
  Walk W = walkUp(User, Regs, OperandMinUse, nullptr);
  return moveChain(W, User);
}

bool IA64SpeculateLoads::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableSpeculation || skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  MRI = &MF.getRegInfo();
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  SmallVector<MachineInstr *, 32> Loads, Users;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (getForms(MI.getOpcode()))
        Loads.push_back(&MI);
      else if ((EarlyStoreOperands && isPlainStore(MI.getOpcode())) ||
               (EarlyBranchOperands && (MI.getOpcode() == IA64::BRCC ||
                                        MI.getOpcode() == IA64::BRCCi ||
                                        MI.getOpcode() == IA64::BRCCf)))
        Users.push_back(&MI);
  unsigned Pair = 0;
  Changed = false;
  // A load that misses brings in the line its neighbours read, so issuing
  // them early as well would only take M slots. Copies and structure
  // accesses read runs of such loads from one base.
  DenseMap<std::pair<const MachineBasicBlock *, Register>,
           SmallVector<int64_t, 2>>
      Early;
  for (MachineInstr *MI : Loads) {
    SmallVector<int64_t, 2> *Near = nullptr;
    int64_t Offset = 0;
    if (OnePerLine && MI->getOperand(1).isReg()) {
      Register Base;
      std::tie(Base, Offset) = splitAddress(MI->getOperand(1).getReg());
      Near = &Early[{MI->getParent(), Base}];
      if (any_of(*Near, [&](int64_t O) { return std::abs(O - Offset) < 64; }))
        continue;
    }
    if (!speculate(*MI, Pair))
      continue;
    if (Near)
      Near->push_back(Offset);
    ++Pair;
  }
  for (MachineInstr *MI : Users)
    NumOperands += hoistOperands(*MI);
  return Changed || Pair != 0;
}

FunctionPass *llvm::createIA64SpeculateLoadsPass() {
  return new IA64SpeculateLoads();
}

namespace {

class IA64ExpandSpeculation : public MachineFunctionPass {
public:
  static char ID;

  IA64ExpandSpeculation() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "IA64 speculative load expansion";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64ExpandSpeculation::ID = 0;

INITIALIZE_PASS(IA64ExpandSpeculation, "ia64-expand-speculation",
                "IA64 speculative load expansion", false, false)

static bool isSpec(const MachineInstr &MI) {
  return MI.getOpcode() == IA64::SPEC_LD_GR ||
         MI.getOpcode() == IA64::SPEC_LD_FR;
}

static bool isCheck(const MachineInstr &MI) {
  return MI.getOpcode() == IA64::CHECK_LD_GR ||
         MI.getOpcode() == IA64::CHECK_LD_FR;
}

// Whether the only way to Check is through its own early load, with nothing
// between them that reads or writes its register, writes its address or may
// change the ALAT's register numbering.
static bool holds(const MachineInstr &Check, const TargetRegisterInfo *TRI) {
  Register Reg = Check.getOperand(0).getReg();
  Register Addr = Check.getOperand(1).getReg();
  int64_t Pair = Check.getOperand(4).getImm();
  const MachineBasicBlock *MBB = Check.getParent();
  MachineBasicBlock::const_iterator At = Check.getIterator();
  // An unreachable cycle of blocks may have one predecessor each.
  for (unsigned Blocks = 0; Blocks != 16; ++Blocks) {
    while (At != MBB->begin()) {
      const MachineInstr &MI = *--At;
      if (MI.isDebugInstr())
        continue;
      if (isSpec(MI) && MI.getOperand(3).getImm() == Pair)
        return MI.getOperand(0).getReg() == Reg &&
               MI.getOperand(1).getReg() == Addr;
      if (MI.isCall() || MI.isInlineAsm() || MI.readsRegister(Reg, TRI) ||
          MI.modifiesRegister(Reg, TRI) || MI.modifiesRegister(Addr, TRI))
        return false;
    }
    if (MBB->pred_size() != 1)
      return false;
    MBB = *MBB->pred_begin();
    At = MBB->end();
  }
  return false;
}

bool IA64ExpandSpeculation::runOnMachineFunction(MachineFunction &MF) {
  const auto &TII = *MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // A pair is kept only if every copy of its check holds.
  DenseMap<int64_t, bool> Holds;
  SmallVector<MachineInstr *, 16> Pseudos;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (isCheck(MI)) {
        Holds.try_emplace(MI.getOperand(4).getImm(), true).first->second &=
            holds(MI, TRI);
      }
      if (isCheck(MI) || isSpec(MI))
        Pseudos.push_back(&MI);
    }

  for (MachineInstr *MI : Pseudos) {
    MachineBasicBlock &MBB = *MI->getParent();
    const DebugLoc &DL = MI->getDebugLoc();
    Register Reg = MI->getOperand(0).getReg();
    bool Spec = isSpec(*MI);
    unsigned Load = MI->getOperand(Spec ? 2 : 3).getImm();
    int64_t Pair = MI->getOperand(Spec ? 3 : 4).getImm();
    bool Keep = Holds.lookup(Pair);
    Forms F = *getForms(Load);
    if (Spec && !Keep) {
      bool FP = MI->getOpcode() == IA64::SPEC_LD_FR;
      TII.copyPhysReg(MBB, *MI, DL, Reg, FP ? IA64::F0 : IA64::R0, false);
    } else {
      BuildMI(MBB, *MI, DL,
              TII.get(Spec   ? F.Spec
                      : Keep ? F.Check
                             : Load),
              Reg)
          .add(MI->getOperand(1))
          .addImm(0)
          .addReg(IA64::P0)
          .cloneMemRefs(*MI);
      // ld.c.clr leaves the register as it was when the ALAT entry is there.
      if (!Spec && Keep)
        std::prev(MI->getIterator())
            ->addOperand(MachineOperand::CreateReg(Reg, /*isDef=*/false,
                                                   /*isImp=*/true));
      if (!Spec)
        Keep ? ++NumChecked : ++NumUndone;
    }
    MI->eraseFromParent();
  }
  return !Pseudos.empty();
}

FunctionPass *llvm::createIA64ExpandSpeculationPass() {
  return new IA64ExpandSpeculation();
}

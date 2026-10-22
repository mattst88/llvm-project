//===-- IA64RematInCallLoops.cpp - Recompute addresses in calling loops ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A value computed before a loop and used in it is live across the whole
// loop. When the loop makes a call, the value needs a register the call
// preserves, which here is a stacked local, so it enlarges the frame that
// every alloc allocates. When the callees' frames no longer fit in the
// register stack, the register stack engine spills and fills the caller's
// registers on every call and return, about a cycle for every two registers
// each way.
//
// LICM hoists every invariant address out of a loop, and IA-64 has no
// register+offset addressing, so nothing sinks "adds r = imm, base" back
// into the loop. An interpreter's dispatch loop ends up keeping dozens of
// them, one per field of its state, live across every call it makes.
//
// In a function that keeps most of the register stack live across a call,
// this pass recomputes such a value in each block of the loop that uses it,
// when the loop's calls, other than to a leaf this module defines, run about
// as often as those blocks. It recomputes an adds from a register that is live
// in the loop anyway, or from a frame index or GP, and an addl of an immediate
// or a GP-relative address. An adds of an adds from a frame index becomes one
// from the frame index.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-remat-in-call-loops"
#define PASS_NAME "IA64 rematerialization in calling loops"

STATISTIC(NumRemat, "Number of values recomputed in a calling loop");
STATISTIC(NumErased, "Number of hoisted values erased");

static cl::opt<unsigned> CallRatio(
    "ia64-remat-call-ratio", cl::Hidden, cl::init(1),
    cl::desc("Recompute a value in a loop whose calls run at least 1/N as "
             "often as the blocks that use it (0 = never)"));

static cl::opt<unsigned> MinLive(
    "ia64-remat-min-live", cl::Hidden, cl::init(84),
    cl::desc("Recompute values only in a function that keeps at least this "
             "many general registers live across a call"));

namespace {

class IA64RematInCallLoops : public MachineFunctionPass {
  MachineRegisterInfo *MRI = nullptr;
  MachineLoopInfo *MLI = nullptr;
  MachineBlockFrequencyInfo *MBFI = nullptr;
  // The summed block frequency of each loop's calls that may overflow the
  // register stack.
  DenseMap<MachineLoop *, uint64_t> CallFreq;

public:
  static char ID;

  IA64RematInCallLoops() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  bool isCandidate(const MachineInstr &MI) const;
  uint64_t callFreq(MachineLoop *L);
  unsigned maxLiveAcrossCalls(MachineFunction &MF) const;
  const MachineInstr *frameBase(const MachineInstr &MI) const;
  bool baseLiveIn(const MachineInstr &MI, MachineLoop *L) const;
  bool remat(MachineInstr &MI);
};

} // end anonymous namespace

char IA64RematInCallLoops::ID = 0;

INITIALIZE_PASS_BEGIN(IA64RematInCallLoops, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(IA64RematInCallLoops, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MI is an unpredicated "adds r = imm, base" or "addl r = imm, r0/gp"
// that defines a virtual register.
bool IA64RematInCallLoops::isCandidate(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  if (Opc != IA64::ADDS && Opc != IA64::ADDL)
    return false;
  if (MI.getNumOperands() != 4 || !MI.getOperand(0).getReg().isVirtual() ||
      MI.getOperand(3).getReg() != IA64::P0)
    return false;
  const MachineOperand &Base = MI.getOperand(2);
  if (Base.isFI())
    return Opc == IA64::ADDS;
  if (!Base.isReg())
    return false;
  Register R = Base.getReg();
  return R == IA64::R0 || R == IA64::R1 || (Opc == IA64::ADDS && R.isVirtual());
}

// Whether a call can overflow the register stack and then return. A leaf
// this module defines allocates a small frame, if any, and calls nothing that
// allocates more.
static bool mayOverflow(const MachineInstr &Call) {
  const Function *F = nullptr;
  for (const MachineOperand &MO : Call.operands())
    if (MO.isGlobal())
      F = dyn_cast<Function>(MO.getGlobal());
  if (F && F->doesNotReturn())
    return false;
  if (!F || F->isDeclaration() || F->isInterposable())
    return true;
  for (const Instruction &I : instructions(F))
    if (const auto *CB = dyn_cast<CallBase>(&I))
      if (!isa<IntrinsicInst>(CB) || isa<AnyMemIntrinsic>(CB))
        return true;
  return false;
}

uint64_t IA64RematInCallLoops::callFreq(MachineLoop *L) {
  auto [It, New] = CallFreq.try_emplace(L, 0);
  if (New)
    for (MachineBasicBlock *MBB : L->blocks())
      It->second += count_if(*MBB,
                             [](const MachineInstr &I) {
                               return I.isCall() && mayOverflow(I);
                             }) *
                    MBFI->getBlockFreq(MBB).getFrequency();
  return It->second;
}

// The "adds r = k, fi" that MI, an adds, adds an immediate to, if any.
const MachineInstr *
IA64RematInCallLoops::frameBase(const MachineInstr &MI) const {
  const MachineOperand &Base = MI.getOperand(2);
  if (MI.getOpcode() != IA64::ADDS || !Base.isReg() ||
      !Base.getReg().isVirtual() || !MI.getOperand(1).isImm())
    return nullptr;
  const MachineInstr *Def = MRI->getVRegDef(Base.getReg());
  if (!Def || !isCandidate(*Def) || !Def->getOperand(2).isFI() ||
      !Def->getOperand(1).isImm())
    return nullptr;
  return Def;
}

// Whether MI's base is available in L without making a register live across
// it that is not already: a constant register, a frame index, or a virtual
// register that L reads other than through a candidate.
bool IA64RematInCallLoops::baseLiveIn(const MachineInstr &MI,
                                      MachineLoop *L) const {
  const MachineOperand &Base = MI.getOperand(2);
  if (Base.isFI() || !Base.getReg().isVirtual() || frameBase(MI))
    return true;
  for (const MachineInstr &U : MRI->use_nodbg_instructions(Base.getReg()))
    if (L->contains(&U) && !isCandidate(U))
      return true;
  return false;
}

// The most general registers live across any one call, which is about the
// number of stacked locals the function will need.
unsigned IA64RematInCallLoops::maxLiveAcrossCalls(MachineFunction &MF) const {
  unsigned NumVRegs = MRI->getNumVirtRegs();
  auto Tracked = [&](const MachineOperand &MO) {
    return MO.isReg() && MO.getReg().isVirtual() &&
           IA64::GRRegClass.hasSubClassEq(MRI->getRegClass(MO.getReg()));
  };
  auto Index = [](Register R) { return R.virtRegIndex(); };

  // Upward-exposed uses and defs of each block. A PHI's operands are used on
  // the edge from their block, so they are live out of it instead.
  unsigned NumBlocks = MF.getNumBlockIDs();
  std::vector<BitVector> Gen(NumBlocks, BitVector(NumVRegs)),
      Kill(NumBlocks, BitVector(NumVRegs)),
      PhiOut(NumBlocks, BitVector(NumVRegs)),
      LiveIn(NumBlocks, BitVector(NumVRegs));
  for (MachineBasicBlock &MBB : MF) {
    unsigned N = MBB.getNumber();
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (MI.isPHI()) {
        for (unsigned I = 1, E = MI.getNumOperands(); I < E; I += 2)
          if (Tracked(MI.getOperand(I)))
            PhiOut[MI.getOperand(I + 1).getMBB()->getNumber()].set(
                Index(MI.getOperand(I).getReg()));
      } else {
        for (const MachineOperand &MO : MI.uses())
          if (Tracked(MO) && !Kill[N].test(Index(MO.getReg())))
            Gen[N].set(Index(MO.getReg()));
      }
      for (const MachineOperand &MO : MI.defs())
        if (Tracked(MO))
          Kill[N].set(Index(MO.getReg()));
    }
  }

  auto LiveOut = [&](MachineBasicBlock &MBB) {
    BitVector Out = PhiOut[MBB.getNumber()];
    for (MachineBasicBlock *Succ : MBB.successors())
      Out |= LiveIn[Succ->getNumber()];
    return Out;
  };
  ReversePostOrderTraversal<MachineFunction *> RPOT(&MF);
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (MachineBasicBlock *MBB : reverse(RPOT)) {
      unsigned N = MBB->getNumber();
      BitVector In = LiveOut(*MBB);
      In.reset(Kill[N]);
      In |= Gen[N];
      if (In != LiveIn[N]) {
        LiveIn[N] = std::move(In);
        Changed = true;
      }
    }
  }

  unsigned Max = 0;
  for (MachineBasicBlock &MBB : MF) {
    BitVector Live = LiveOut(MBB);
    for (MachineInstr &MI : reverse(MBB)) {
      if (MI.isDebugInstr() || MI.isPHI())
        continue;
      for (const MachineOperand &MO : MI.defs())
        if (Tracked(MO))
          Live.reset(Index(MO.getReg()));
      if (MI.isCall())
        Max = std::max(Max, Live.count());
      for (const MachineOperand &MO : MI.uses())
        if (Tracked(MO))
          Live.set(Index(MO.getReg()));
    }
  }
  return Max;
}

// Recompute MI in the blocks of each calling loop that use it, and erase it
// if nothing else does.
bool IA64RematInCallLoops::remat(MachineInstr &MI) {
  Register Reg = MI.getOperand(0).getReg();
  MachineBasicBlock *DefMBB = MI.getParent();

  // The using blocks of each loop the value is live across: the outermost
  // loop around a use that does not contain the definition. A PHI's use is
  // on an edge, so a loop with one keeps the value live.
  DenseMap<MachineLoop *, SmallVector<MachineBasicBlock *, 4>> Users;
  SmallPtrSet<MachineLoop *, 4> Pinned;
  for (MachineInstr &U : MRI->use_nodbg_instructions(Reg)) {
    MachineBasicBlock *MBB = U.getParent();
    MachineLoop *L = nullptr;
    for (MachineLoop *P = MLI->getLoopFor(MBB); P && !P->contains(DefMBB);
         P = P->getParentLoop())
      L = P;
    if (!L)
      continue;
    if (U.isPHI()) {
      Pinned.insert(L);
      continue;
    }
    auto &V = Users[L];
    if (!is_contained(V, MBB))
      V.push_back(MBB);
  }

  bool Changed = false;
  for (auto &[L, Blocks] : Users) {
    if (Pinned.contains(L) || !CallRatio)
      continue;
    uint64_t Calls = callFreq(L);
    if (!Calls)
      continue;
    uint64_t Uses = 0;
    for (MachineBasicBlock *MBB : Blocks)
      Uses += MBFI->getBlockFreq(MBB).getFrequency();
    if (Calls * CallRatio < Uses || !baseLiveIn(MI, L))
      continue;
    for (MachineBasicBlock *MBB : Blocks) {
      auto First = find_if(*MBB, [&](const MachineInstr &I) {
        return !I.isDebugInstr() && I.readsRegister(Reg, /*TRI=*/nullptr);
      });
      Register New = MRI->cloneVirtualRegister(Reg);
      MachineInstr *Copy = MBB->getParent()->CloneMachineInstr(&MI);
      Copy->getOperand(0).setReg(New);
      if (const MachineInstr *FB = frameBase(MI)) {
        Copy->getOperand(1).setImm(MI.getOperand(1).getImm() +
                                   FB->getOperand(1).getImm());
        Copy->getOperand(2).ChangeToFrameIndex(FB->getOperand(2).getIndex());
      }
      MBB->insert(First, Copy);
      for (MachineOperand &MO :
           make_early_inc_range(MRI->use_nodbg_operands(Reg)))
        if (MO.getParent()->getParent() == MBB && MO.getParent() != Copy)
          MO.setReg(New);
      ++NumRemat;
    }
    Changed = true;
  }
  if (Changed) {
    MRI->clearKillFlags(Reg);
    if (MI.getOperand(2).isReg() && !frameBase(MI))
      MRI->clearKillFlags(MI.getOperand(2).getReg());
  }
  return Changed;
}

bool IA64RematInCallLoops::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()) || !CallRatio)
    return false;
  MRI = &MF.getRegInfo();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  CallFreq.clear();
  if (MLI->empty())
    return false;

  // The register stack holds 96 stacked registers. A frame that leaves a
  // callee a dozen or more of them seldom overflows it on a call, so
  // recomputing only costs.
  unsigned Live = maxLiveAcrossCalls(MF);
  LLVM_DEBUG(dbgs() << MF.getName() << ": " << Live
                    << " registers live across a call\n");
  if (Live < MinLive)
    return false;
  SmallVector<MachineInstr *, 32> Worklist;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (isCandidate(MI))
        Worklist.push_back(&MI);

  bool Changed = false;
  for (MachineInstr *MI : Worklist)
    Changed |= remat(*MI);

  // Erase what is left unused, its base first when that was a candidate.
  for (MachineInstr *MI : reverse(Worklist)) {
    Register Reg = MI->getOperand(0).getReg();
    if (!MRI->use_nodbg_empty(Reg))
      continue;
    for (MachineOperand &MO : make_early_inc_range(MRI->use_operands(Reg)))
      MO.getParent()->setDebugValueUndef();
    MI->eraseFromParent();
    ++NumErased;
    Changed = true;
  }
  return Changed;
}

FunctionPass *llvm::createIA64RematInCallLoopsPass() {
  return new IA64RematInCallLoops();
}

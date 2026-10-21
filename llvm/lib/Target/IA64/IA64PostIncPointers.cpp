//===-- IA64PostIncPointers.cpp - Give accesses pointers of their own -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A load or store has no offset, only an immediate it may add to its address
// register afterwards. Instruction selection folds the step of a pointer into
// one access through it, but a loop unrolled to read a[i], a[i + 1], ... and
// to write a[i] back has one pointer for them all, and computes the other
// addresses with adds in each iteration: in daxpy unrolled four times there
// are five adds for twelve accesses.
//
// IA64PostIncPointers gives each access in a single-block loop at a constant
// offset from a pointer that steps by a constant a pointer of its own, which
// starts at that offset in the preheader and is stepped by the access itself.
// The adds that formed the offsets go. So does the one that stepped the
// pointer, the first access at no offset stepping it instead, or where there
// is none and nothing else uses the pointer, with the pointer itself. Each
// new pointer holds a register for the whole loop, so a loop gains only a
// few, and none where no add goes. The accesses keep their memory operands,
// from which the pipeliner relates the new pointers to each other.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-postinc-pointers"

STATISTIC(NumPointers, "Number of accesses given a pointer of their own");
STATISTIC(NumAdds, "Number of address adds removed from loops");

static cl::opt<bool>
    EnablePostIncPointers("ia64-enable-postinc-pointers", cl::init(true),
                          cl::Hidden,
                          cl::desc("Give each access in a loop a pointer of "
                                   "its own that it steps itself"));

static cl::opt<unsigned>
    MaxPointers("ia64-postinc-max-pointers", cl::init(16), cl::Hidden,
                cl::desc("The most pointers to add to one loop"));

namespace {
class IA64PostIncPointers : public MachineFunctionPass {
public:
  static char ID;

  IA64PostIncPointers() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "IA64 post-increment pointers";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  const IA64InstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  bool runOnLoop(MachineBasicBlock &MBB, MachineBasicBlock &Preheader);
  bool isAccessThrough(const MachineInstr &MI, Register Base) const;
  bool isOffset(const MachineInstr &MI, Register Base,
                const MachineBasicBlock &MBB) const;
  void step(MachineInstr &Access, Register Base, Register NewBase,
            int64_t Step);
};
} // end anonymous namespace

char IA64PostIncPointers::ID = 0;

INITIALIZE_PASS(IA64PostIncPointers, DEBUG_TYPE, "IA64 post-increment pointers",
                false, false)

// The form of a load or store that adds an immediate to its address
// register, or 0 for one that has none.
static unsigned getPostIncOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
#define ACCESS(LOAD, STORE)                                                    \
  case IA64::LOAD:                                                             \
    return IA64::LOAD##_i;                                                     \
  case IA64::STORE:                                                            \
    return IA64::STORE##_i;
    ACCESS(LD1, ST1)
    ACCESS(LD2, ST2)
    ACCESS(LD4, ST4)
    ACCESS(LD8, ST8)
    ACCESS(LDFS, STFS)
    ACCESS(LDFD, STFD)
    ACCESS(LDFE, STFE)
#undef ACCESS
  }
}

static bool isUnpredicated(const MachineInstr &MI) {
  const MachineOperand &QP = MI.getOperand(MI.getNumExplicitOperands() - 1);
  return QP.isReg() && QP.getReg() == IA64::P0;
}

// The amount a load or store adds to its address register after the access,
// which follows the address of a load and the value of a store.
static std::optional<int64_t> getPostIncrement(const MachineInstr &MI,
                                               Register Base) {
  if (!MI.mayLoadOrStore() || !MI.hasOneMemOperand())
    return std::nullopt;
  unsigned Idx = MI.getNumExplicitDefs();
  const MachineOperand &BaseOp = MI.getOperand(Idx);
  if (!BaseOp.isReg() || BaseOp.getReg() != Base || !BaseOp.isTied())
    return std::nullopt;
  const MachineOperand &Inc = MI.getOperand(Idx + (MI.mayStore() ? 2 : 1));
  if (!Inc.isImm())
    return std::nullopt;
  return Inc.getImm();
}

// Whether MI is a plain load or store that uses Base as its address and for
// nothing else.
bool IA64PostIncPointers::isAccessThrough(const MachineInstr &MI,
                                          Register Base) const {
  if (!getPostIncOpcode(MI.getOpcode()) || !MI.hasOneMemOperand() ||
      MI.hasOrderedMemoryRef() || !isUnpredicated(MI))
    return false;
  unsigned Idx = MI.getNumExplicitDefs();
  if (MI.getOperand(Idx).getReg() != Base)
    return false;
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I)
    if (I != Idx && MI.getOperand(I).isReg() &&
        MI.getOperand(I).getReg() == Base)
      return false;
  return true;
}

// Whether MI adds a constant to Base to form addresses that only accesses in
// MBB use.
bool IA64PostIncPointers::isOffset(const MachineInstr &MI, Register Base,
                                   const MachineBasicBlock &MBB) const {
  if (MI.getOpcode() != IA64::ADDS || !isUnpredicated(MI) ||
      MI.getOperand(2).getReg() != Base ||
      !isInt<14>(MI.getOperand(1).getImm()))
    return false;
  Register Addr = MI.getOperand(0).getReg();
  return all_of(MRI->use_nodbg_instructions(Addr), [&](MachineInstr &U) {
    return U.getParent() == &MBB && isAccessThrough(U, Addr);
  });
}

// Replace Access with its form that adds Step to Base to give NewBase.
void IA64PostIncPointers::step(MachineInstr &Access, Register Base,
                               Register NewBase, int64_t Step) {
  unsigned Idx = Access.getNumExplicitDefs();
  MachineInstrBuilder MIB =
      BuildMI(*Access.getParent(), Access, Access.getDebugLoc(),
              TII->get(getPostIncOpcode(Access.getOpcode())));
  if (Access.mayLoad())
    MIB.add(Access.getOperand(0)).addDef(NewBase).addReg(Base);
  else
    MIB.addDef(NewBase).addReg(Base).add(Access.getOperand(Idx + 1));
  MIB.addImm(Step)
      .add(Access.getOperand(Access.getNumExplicitOperands() - 2))
      .add(Access.getOperand(Access.getNumExplicitOperands() - 1))
      .cloneMemRefs(Access)
      .setMIFlags(Access.getFlags());
  Access.eraseFromParent();
}

bool IA64PostIncPointers::runOnLoop(MachineBasicBlock &MBB,
                                    MachineBasicBlock &Preheader) {
  bool Changed = false;
  unsigned Added = 0;
  SmallVector<MachineInstr *, 8> Phis;
  for (MachineInstr &Phi : MBB.phis())
    if (MRI->getRegClass(Phi.getOperand(0).getReg()) == &IA64::GRRegClass)
      Phis.push_back(&Phi);

  for (MachineInstr *Phi : Phis) {
    Register Ptr = Phi->getOperand(0).getReg(), Init, Next;
    for (unsigned I = 1, E = Phi->getNumOperands(); I != E; I += 2)
      (Phi->getOperand(I + 1).getMBB() == &MBB ? Next : Init) =
          Phi->getOperand(I).getReg();
    MachineInstr *Step = MRI->getVRegDef(Next);
    if (!Step || Step->getParent() != &MBB)
      continue;
    int64_t Stride;
    bool StepIsAdd = Step->getOpcode() == IA64::ADDS;
    if (StepIsAdd) {
      if (!isUnpredicated(*Step) || Step->getOperand(2).getReg() != Ptr)
        continue;
      Stride = Step->getOperand(1).getImm();
    } else if (std::optional<int64_t> Inc = getPostIncrement(*Step, Ptr)) {
      Stride = *Inc;
    } else {
      continue;
    }
    if (!isInt<9>(Stride))
      continue;

    SmallVector<MachineInstr *, 4> Direct, Offsets;
    bool Others = false;
    for (MachineInstr &U : MRI->use_nodbg_instructions(Ptr)) {
      if (&U == Step)
        continue;
      if (U.getParent() == &MBB && isAccessThrough(U, Ptr))
        Direct.push_back(&U);
      else if (U.getParent() == &MBB && isOffset(U, Ptr, MBB))
        Offsets.push_back(&U);
      else
        Others = true;
    }
    // The add that steps the pointer goes if the first access at no offset
    // can step it instead, which it can if nothing in the loop reads the
    // stepped pointer before that access. With no access at no offset, the
    // add goes only if the pointer goes too, nothing else using either.
    MachineInstr *First = nullptr;
    if (StepIsAdd && !Direct.empty())
      for (MachineInstr &MI : MBB) {
        if (is_contained(Direct, &MI)) {
          First = &MI;
          break;
        }
        if (!MI.isPHI() && MI.readsRegister(Next, /*TRI=*/nullptr))
          break;
      }
    bool StepGoes = First || (StepIsAdd && Direct.empty() && !Others &&
                              MRI->hasOneNonDBGUse(Next));
    unsigned Needed = 0;
    for (MachineInstr *Offset : Offsets)
      Needed +=
          std::distance(MRI->use_nodbg_begin(Offset->getOperand(0).getReg()),
                        MRI->use_nodbg_end());
    if (First)
      Needed += Direct.size() - 1;
    if ((Offsets.empty() && !StepGoes) || Added + Needed > MaxPointers)
      continue;
    Added += Needed;

    // Accesses to the same address start from the same register.
    SmallDenseMap<int64_t, Register, 4> Starts;
    Starts[0] = Init;
    auto NewPointer = [&](MachineInstr &Access, int64_t Offset) {
      Register &Start = Starts[Offset];
      if (!Start) {
        Start = MRI->createVirtualRegister(&IA64::GRRegClass);
        BuildMI(Preheader, Preheader.getFirstTerminator(), DebugLoc(),
                TII->get(IA64::ADDS), Start)
            .addImm(Offset)
            .addReg(Init)
            .addReg(IA64::P0);
      }
      Register Base = MRI->createVirtualRegister(&IA64::GRRegClass);
      Register NewBase = MRI->createVirtualRegister(&IA64::GRRegClass);
      BuildMI(MBB, MBB.begin(), DebugLoc(), TII->get(IA64::PHI), Base)
          .addReg(Start)
          .addMBB(&Preheader)
          .addReg(NewBase)
          .addMBB(&MBB);
      step(Access, Base, NewBase, Stride);
      ++NumPointers;
    };

    for (MachineInstr *Offset : Offsets) {
      Register Addr = Offset->getOperand(0).getReg();
      int64_t Imm = Offset->getOperand(1).getImm();
      SmallVector<MachineInstr *, 4> Accesses(
          make_pointer_range(MRI->use_nodbg_instructions(Addr)));
      for (MachineInstr *Access : Accesses)
        NewPointer(*Access, Imm);
      MRI->markUsesInDebugValueAsUndef(Addr);
      Offset->eraseFromParent();
      ++NumAdds;
    }
    if (StepGoes) {
      Step->eraseFromParent();
      ++NumAdds;
      if (First) {
        step(*First, Ptr, Next, Stride);
        for (MachineInstr *Access : Direct)
          if (Access != First)
            NewPointer(*Access, 0);
      } else {
        MRI->markUsesInDebugValueAsUndef(Next);
        MRI->markUsesInDebugValueAsUndef(Ptr);
        Phi->eraseFromParent();
      }
    }
    Changed = true;
  }
  return Changed;
}

bool IA64PostIncPointers::runOnMachineFunction(MachineFunction &MF) {
  if (!EnablePostIncPointers || skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  MRI = &MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.isSuccessor(&MBB) || MBB.pred_size() != 2)
      continue;
    MachineBasicBlock *Preheader = *find_if(
        MBB.predecessors(), [&](MachineBasicBlock *P) { return P != &MBB; });
    if (Preheader == &MBB)
      continue;
    Changed |= runOnLoop(MBB, *Preheader);
  }
  return Changed;
}

FunctionPass *llvm::createIA64PostIncPointersPass() {
  return new IA64PostIncPointers();
}

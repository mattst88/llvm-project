//===-- IA64InsertStops.cpp - Insert stop bits ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instructions between two stops (an instruction group) may execute in
// parallel, so within a group no register may be written and then read, or
// written twice (RAW and WAW dependencies). This pass inserts a stop wherever
// a block would otherwise violate that rule (SDM volume 1, section 3.4, and
// volume 2, section 3.3 of the instruction reference).
//
// A taken branch ends the instruction group, but execution that falls
// through to the next block continues it, so a block that may fall through
// leaves its group open and the next block starts with what it wrote. A
// group that a taken branch enters is new, and assuming otherwise only adds
// stops.
//
// A store over what an integer load read also starts a group after one that
// read the loaded value. Itanium 2 otherwise cancels the fill of L1D the load
// started (see IA64HoistLoadUses.cpp).
//
// The dependency rules have two exceptions. Instructions under predicates
// that exclude each other never both take effect, so they may share a group
// even when they touch the same register. And compares that all set a
// predicate, or all clear it (parallel compares), give the same result in
// any order.
//
// A stop belongs to the instruction it follows, which is how the MC layer
// represents it, so it is recorded as an asm printer flag on the
// MachineInstr.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64ExclusivePredicates.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-insert-stops"
#define PASS_NAME "IA64 stop insertion"

static cl::opt<bool> EnableDisjoint("ia64-disjoint-groups", cl::Hidden,
                                    cl::init(true));
static cl::opt<bool> EnableFallThrough("ia64-fallthrough-groups", cl::Hidden,
                                       cl::init(true));
static cl::opt<bool> StopBeforeJoin("ia64-stop-before-join", cl::Hidden,
                                    cl::init(true));
static cl::opt<bool> StopAfterLoadUse("ia64-stop-after-load-use", cl::Hidden,
                                      cl::init(true));
static cl::opt<unsigned> LoadUseGroups("ia64-load-use-groups", cl::Hidden,
                                       cl::init(2));
static cl::opt<bool> GapBeforeRotating("ia64-gap-before-rotating", cl::Hidden,
                                       cl::init(true));
static cl::opt<bool> PartFPLoads("ia64-part-fp-loads", cl::Hidden,
                                 cl::init(true));

namespace {

class IA64InsertStops : public MachineFunctionPass {
public:
  static char ID;

  IA64InsertStops() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
    AU.addRequired<AAResultsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64InsertStops::ID = 0;

INITIALIZE_PASS_BEGIN(IA64InsertStops, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(LazyMachineBlockFrequencyInfoPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(IA64InsertStops, DEBUG_TYPE, PASS_NAME, false, false)

// Registers whose value is constant regardless of writes.
static bool isConstantReg(MCRegister Reg) {
  return Reg == IA64::R0 || Reg == IA64::F0 || Reg == IA64::F1 ||
         Reg == IA64::P0;
}

// Parallel compares and bit tests (or.andcm), which set the first predicate and
// clear the second when the condition holds, and write neither otherwise.
static bool isParallelCompare(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case IA64::CMP_EQ_OR_ANDCM:
  case IA64::CMP_EQ_OR_ANDCMi:
  case IA64::CMP_NE_OR_ANDCM:
  case IA64::CMP_NE_OR_ANDCMi:
  case IA64::CMP_GT_OR_ANDCMz:
  case IA64::CMP_GE_OR_ANDCMz:
  case IA64::CMP_LT_OR_ANDCMz:
  case IA64::CMP_LE_OR_ANDCMz:
  case IA64::CMP4_EQ_OR_ANDCM:
  case IA64::CMP4_EQ_OR_ANDCMi:
  case IA64::CMP4_NE_OR_ANDCM:
  case IA64::CMP4_NE_OR_ANDCMi:
  case IA64::CMP4_GT_OR_ANDCMz:
  case IA64::CMP4_GE_OR_ANDCMz:
  case IA64::CMP4_LT_OR_ANDCMz:
  case IA64::CMP4_LE_OR_ANDCMz:
  case IA64::TBIT_Z_OR_ANDCM:
  case IA64::TBIT_NZ_OR_ANDCM:
    return true;
  }
}

// And-type compares and bit and NaT tests, which clear both predicates
// unless the condition holds, and write neither otherwise.
static bool isAndCompare(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case IA64::CMP_EQ_AND:
  case IA64::CMP_EQ_ANDi:
  case IA64::CMP_NE_AND:
  case IA64::CMP_NE_ANDi:
  case IA64::CMP_GT_ANDz:
  case IA64::CMP_GE_ANDz:
  case IA64::CMP_LT_ANDz:
  case IA64::CMP_LE_ANDz:
  case IA64::CMP4_EQ_AND:
  case IA64::CMP4_EQ_ANDi:
  case IA64::CMP4_NE_AND:
  case IA64::CMP4_NE_ANDi:
  case IA64::CMP4_GT_ANDz:
  case IA64::CMP4_GE_ANDz:
  case IA64::CMP4_LT_ANDz:
  case IA64::CMP4_LE_ANDz:
  case IA64::TBIT_Z_AND:
  case IA64::TBIT_NZ_AND:
  case IA64::TNAT_Z_AND:
  case IA64::TNAT_NZ_AND:
    return true;
  }
}

// Whether the group open at the end of \p MBB may continue into the next
// block. \p Last is the last instruction that emits code, if any.
static bool continuesGroup(const MachineBasicBlock &MBB,
                           const MachineInstr *Last) {
  if (!EnableFallThrough || (Last && Last->isBarrier()))
    return false;
  // An alignment or a section change ends the group in the assembler, and
  // nothing falls through into a landing pad.
  const MachineBasicBlock *Next = MBB.getNextNode();
  return Next && !Next->isEHPad() && !Next->isBeginSection() &&
         !MBB.isEndSection() && Next->getAlignment() == Align(1);
}

// On Madison a rotating loop of 5 stages takes 14 cycles more when its passes
// are 1 modulo 4 and it is entered right after the writes of ar.ec and pr.rot
// (5 passes a sparse matrix row: 55 cycles a row against 41). An empty group
// after the writes avoids that. Moving them up the preheader instead does not
// hold for every loop, and a gap before every loop costs nbody 1% and fdtd2d
// 1%, so only stage counts that are 1 modulo 4 get one. Return the nops.
static SmallPtrSet<MachineInstr *, 2> insertRotatingGaps(MachineFunction &MF) {
  SmallPtrSet<MachineInstr *, 2> Gaps;
  if (!GapBeforeRotating || !MF.getSubtarget<IA64Subtarget>().isMadison())
    return Gaps;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  for (MachineBasicBlock &MBB : MF) {
    MachineInstr *Last = nullptr;
    bool Hit = false;
    for (MachineInstr &MI : MBB) {
      bool EC = MI.getOpcode() == IA64::MOV_ARi &&
                MI.getOperand(0).getReg() == IA64::AR66;
      if (EC)
        Hit = MI.getOperand(1).getImm() % 4 == 1;
      if (EC || MI.getOpcode() == IA64::MOV_PR_ROT)
        Last = &MI;
    }
    if (!Hit)
      continue;
    Gaps.insert(BuildMI(MBB, std::next(Last->getIterator()), Last->getDebugLoc(),
                        TII.get(IA64::NOP))
                    .addImm(0)
                    .addReg(IA64::P0));
  }
  return Gaps;
}

// Whether MI, an FP load, reads within 15 bytes of the FP load First. Each FP
// load reads L2, whose banks serve one load a cycle on Madison even from one
// chunk: the second takes 6 cycles more, against the 1 of a group of its own.
static bool nearFPLoad(const MachineInstr &MI, const MachineInstr &First) {
  auto Where = [&](const MachineInstr &L, int64_t &Off) -> const Value * {
    if (!L.hasOneMemOperand())
      return nullptr;
    const MachineMemOperand *MMO = *L.memoperands_begin();
    const Value *V = MMO->getValue();
    if (!V)
      return nullptr;
    const DataLayout &DL = L.getMF()->getDataLayout();
    const Value *Base = GetPointerBaseWithConstantOffset(V, Off, DL);
    Off += MMO->getOffset();
    return Base;
  };
  int64_t A = 0, B = 0;
  const Value *BaseA = Where(MI, A), *BaseB = Where(First, B);
  return BaseA && BaseA == BaseB && std::abs(A - B) < 16;
}

static bool isFPLoad(const MachineInstr &MI) {
  return MI.mayLoad() && !MI.mayStore() && MI.getNumExplicitDefs() &&
         MI.getOperand(0).isReg() &&
         IA64::FRRegClass.contains(MI.getOperand(0).getReg());
}

bool IA64InsertStops::runOnMachineFunction(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  SmallPtrSet<MachineInstr *, 2> RotatingGaps = insertRotatingGaps(MF);
  // Computed only for a block that needs it.
  auto &LazyMBFI = getAnalysis<LazyMachineBlockFrequencyInfoPass>();
  AAResults *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  // Registers written in the current group, and those written by an F-unit
  // instruction.
  BitVector Written(TRI.getNumRegs());
  BitVector WrittenByF(TRI.getNumRegs());
  // Predicates written in the current group only by parallel compares, split
  // by whether those compares would set or clear them.
  BitVector OnlySet(TRI.getNumRegs());
  BitVector OnlyCleared(TRI.getNumRegs());
  // Those written only by and-type compares, which may not share a group
  // with parallel compares of another type.
  BitVector OnlyAnded(TRI.getNumRegs());
  // Which predicates exclude each other, and for each register written in
  // the group, the qualifying predicate of its writes (P0 if they differ).
  IA64ExclusivePredicates Exclusive(TRI);
  std::vector<MCRegister> WriterQP(TRI.getNumRegs());
  // The integer load in the block whose value each register holds, if it
  // issued at most LoadUseGroups groups ago and no call followed it, with
  // the number of the group it issued in, and the loads whose values the
  // current group reads.
  std::vector<std::pair<MachineInstr *, unsigned>> LoadedBy(TRI.getNumRegs());
  SmallVector<MachineInstr *, 4> ReadLoads;
  // The FP loads of the current group, kept only where they are worth parting.
  bool Parting = PartFPLoads && MF.getSubtarget<IA64Subtarget>().isMadison();
  SmallVector<MachineInstr *, 4> GroupFPLoads;
  unsigned GroupNumber = 0;
  // The last instruction of the current group.
  MachineInstr *Last = nullptr;
  // Whether the current group started in an earlier block.
  bool Joined = false;
  // Whether the current group contains an alloc. No register models the
  // current frame marker, which calls and returns replace.
  bool NewFrame = false;
  // Whether the current group may have raised an FP exception. The flags
  // are part of ar.fpsr, but the instructions that set them only declare a
  // read of it (for the rounding mode), so any number may share a group.
  bool RaisedFlags = false;

  auto EndGroup = [&] {
    if (Last)
      Last->setAsmPrinterFlag(IA64::StopAfter);
    Last = nullptr;
    Written.reset();
    WrittenByF.reset();
    OnlySet.reset();
    OnlyCleared.reset();
    OnlyAnded.reset();
    NewFrame = false;
    RaisedFlags = false;
    Joined = false;
    ReadLoads.clear();
    GroupFPLoads.clear();
    ++GroupNumber;
  };

  // Records the stops of MBB, and returns how many there are and whether
  // the group open from the block before needed one.
  auto Scan = [&](MachineBasicBlock &MBB) -> std::pair<unsigned, bool> {
    bool JoinStop = false;
    // Which predicates exclude each other depends on the path, so only the
    // writes carry over from the block before.
    Exclusive.clear();
    llvm::fill(LoadedBy, std::make_pair(nullptr, 0));
    // The last instruction of the block that emits code.
    MachineInstr *BlockLast = nullptr;
    // The pipeliner has placed a kernel's cycles itself.
    bool PartHere = Parting && none_of(MBB, [](const MachineInstr &MI) {
      return MI.getAsmPrinterFlag(IA64::StartsCycle);
    });

    for (MachineInstr &MI : MBB) {
      if (MI.isMetaInstruction())
        continue;
      // Inline asm may read and write anything, so it gets a group of its
      // own. The asm printer emits the stop after it.
      if (MI.isInlineAsm()) {
        Exclusive.step(MI);
        EndGroup();
        BlockLast = &MI;
        continue;
      }
      IA64II::Unit Unit = IA64II::getUnit(MI.getDesc().TSFlags);

      // Whether MO counts for the dependency rules. A branch executes last,
      // and its target is outside the group, so implicit operands that only
      // describe liveness across it do not count. A branch may also read a
      // predicate or BR written in its own group, and a return may read
      // ar.pfs, except for a predicate written by an F-unit instruction.
      auto Touches = [&](const MachineOperand &MO) {
        if (!MO.isReg() || !MO.getReg().isPhysical() ||
            isConstantReg(MO.getReg()))
          return false;
        if (Unit != IA64II::UnitB)
          return true;
        MCRegister Reg = MO.getReg();
        if (MO.isDef())
          return !MO.isImplicit() || IA64::ARRegClass.contains(Reg);
        if (IA64::PRRegClass.contains(Reg))
          return !MO.isImplicit() && WrittenByF.test(Reg.id());
        if (IA64::ARRegClass.contains(Reg))
          return !(Reg == IA64::AR64 && MI.isReturn());
        return false;
      };

      // alloc, calls and returns change RSE state without declaring it, and a
      // read of the RSE registers sees the change only from a later group.
      bool Parallel = isParallelCompare(MI.getOpcode());
      bool Anded = isAndCompare(MI.getOpcode());

      int QPIdx = MI.findFirstPredOperandIdx();
      MCRegister QP = IA64ExclusivePredicates::qualifyingPredicate(MI);
      // Whether MO, which the group has written, is written only under a
      // predicate that excludes QP, so that MI and those writes never both
      // take effect.
      auto Disjoint = [&](const MachineOperand &MO) {
        unsigned R = MO.getReg().id();
        return QP != IA64::P0 && (int)MO.getOperandNo() != QPIdx &&
               WriterQP[R] != IA64::P0 && Exclusive.partner(WriterQP[R]) == QP;
      };

      auto ReadsRSE = [](const MachineInstr &MI) {
        if (MI.getOpcode() != IA64::MOV_FROM_AR)
          return false;
        Register AR = MI.getOperand(1).getReg();
        return AR == IA64::AR16 || AR == IA64::AR17 || AR == IA64::AR18 ||
               AR == IA64::AR19;
      };

      // A read of ar.fpsr, or fchkf of a status field's flags, does not see
      // flags raised in its own group, and a write of ar.fpsr or of a status
      // field's flags must not share a group that raised any.
      bool ReadsFlags = MI.getOpcode() == IA64::FCHKF ||
                        (MI.getOpcode() == IA64::MOV_FROM_AR &&
                         MI.getOperand(1).getReg() == IA64::AR40);
      bool UsesFPSR = MI.readsRegister(IA64::AR40, &TRI);
      bool SetsFPSR = MI.definesRegister(IA64::AR40, &TRI);

      bool AfterLoadUse =
          StopAfterLoadUse && any_of(ReadLoads, [&](MachineInstr *Load) {
            return IA64InstrInfo::cancelsFill(*Load, MI, AA);
          });
      bool FPLoad = PartHere && isFPLoad(MI);
      if (FPLoad && any_of(GroupFPLoads, [&](MachineInstr *Load) {
            return nearFPLoad(MI, *Load);
          }))
        AfterLoadUse = true;
      if (AfterLoadUse)
        MI.setAsmPrinterFlag(IA64::StartsGroup);

      // alloc and flushrs must be first in their group. A call or return
      // must not share a group with an alloc; a tail call is exempt, though
      // it acts as both.
      if (MI.getOpcode() == IA64::ALLOC || MI.getOpcode() == IA64::FLUSHRS ||
          ReadsRSE(MI) || (RaisedFlags && (ReadsFlags || SetsFPSR)) ||
          AfterLoadUse || MI.getAsmPrinterFlag(IA64::StartsCycle) ||
          (NewFrame && (MI.isCall() || MI.isReturn()) &&
           MI.getOpcode() != IA64::BR_TAIL) ||
          any_of(MI.operands(), [&](const MachineOperand &MO) {
            if (!Touches(MO) || !Written.test(MO.getReg().id()))
              return false;
            if (EnableDisjoint && Disjoint(MO))
              return false;
            // Another parallel compare writing the predicate the same way
            // as the others in the group.
            if (Parallel && MO.isDef())
              return !(MO.getOperandNo() == 0 ? OnlySet : OnlyCleared)
                          .test(MO.getReg().id());
            if (Anded && MO.isDef())
              return !OnlyAnded.test(MO.getReg().id());
            return true;
          })) {
        JoinStop |= Joined;
        EndGroup();
      }
      if (FPLoad)
        GroupFPLoads.push_back(&MI);
      NewFrame |= MI.getOpcode() == IA64::ALLOC;
      RaisedFlags |= Unit == IA64II::UnitF && UsesFPSR && !ReadsFlags;

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef() || !Touches(MO))
          continue;
        // A predicate first written in the group by a parallel compare stays
        // open to more parallel compares of the same kind.
        if (Parallel && !Written.test(MO.getReg().id()))
          (MO.getOperandNo() == 0 ? OnlySet : OnlyCleared)
              .set(MO.getReg().id());
        else if (Anded && !Written.test(MO.getReg().id()))
          OnlyAnded.set(MO.getReg().id());
        else if (!Parallel && !Anded) {
          OnlySet.reset(MO.getReg().id());
          OnlyCleared.reset(MO.getReg().id());
          OnlyAnded.reset(MO.getReg().id());
        }
        unsigned R = MO.getReg().id();
        WriterQP[R] =
            !Written.test(R) || WriterQP[R] == QP ? QP : MCRegister(IA64::P0);
        Written.set(R);
        if (Unit == IA64II::UnitF)
          WrittenByF.set(MO.getReg().id());
      }
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.getReg().isPhysical())
          continue;
        auto [Load, Group] = LoadedBy[MO.getReg().id()];
        if (MO.isUse() && Load && GroupNumber - Group <= LoadUseGroups)
          ReadLoads.push_back(Load);
      }
      if (MI.isCall())
        llvm::fill(LoadedBy, std::make_pair(nullptr, 0));
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical())
          LoadedBy[MO.getReg().id()] = {
              MO.getOperandNo() == 0 && MI.mayLoad() ? &MI : nullptr,
              GroupNumber};
      Last = BlockLast = &MI;

      Exclusive.step(MI);

      // A call returns to the next bundle, which starts a new group with or
      // without a stop, so record one. The rest of a flushrs's group would
      // see the register stack state from before the flush. br.ctop and
      // clrrrb rename the rotating registers, and clrrrb must end its group.
      if (MI.isCall() || MI.getOpcode() == IA64::FLUSHRS ||
          MI.getOpcode() == IA64::BR_CTOP || MI.getOpcode() == IA64::CLRRRB ||
          RotatingGaps.contains(&MI) || RotatingGaps.contains(MI.getNextNode()))
        EndGroup();
    }
    if (!continuesGroup(MBB, BlockLast))
      EndGroup();
    return {count_if(MBB,
                     [](const MachineInstr &MI) {
                       return MI.getAsmPrinterFlag(IA64::StopAfter);
                     }),
            JoinStop};
  };

  for (MachineBasicBlock &MBB : MF) {
    MachineInstr *Before = Last;
    Joined = Last != nullptr;
    if (!Joined || !StopBeforeJoin || MBB.pred_size() < 2) {
      Scan(MBB);
      continue;
    }
    BitVector Written0 = Written, WrittenByF0 = WrittenByF, OnlySet0 = OnlySet,
              OnlyCleared0 = OnlyCleared, OnlyAnded0 = OnlyAnded;
    std::vector<MCRegister> WriterQP0 = WriterQP;
    bool NewFrame0 = NewFrame, RaisedFlags0 = RaisedFlags;
    auto Restore = [&] {
      for (MachineInstr &MI : MBB) {
        MI.clearAsmPrinterFlag(IA64::StopAfter);
        MI.clearAsmPrinterFlag(IA64::StartsGroup);
      }
      Before->clearAsmPrinterFlag(IA64::StopAfter);
      Written = Written0;
      WrittenByF = WrittenByF0;
      OnlySet = OnlySet0;
      OnlyCleared = OnlyCleared0;
      OnlyAnded = OnlyAnded0;
      WriterQP = WriterQP0;
      NewFrame = NewFrame0;
      RaisedFlags = RaisedFlags0;
      Last = Before;
      Joined = true;
    };
    auto [Stops, JoinStop] = Scan(MBB);
    if (!JoinStop)
      continue;
    // A branch also enters the block, starting a group of its own there, so
    // the stop the group from the block before needs may split the block's
    // first group on every path. A stop at the end of the block before
    // instead costs a group only where the block before falls through.
    Restore();
    EndGroup();
    unsigned StopsAlone = Scan(MBB).first;
    const MachineBlockFrequencyInfo &MBFI = LazyMBFI.getBFI();
    uint64_t Freq = MBFI.getBlockFreq(&MBB).getFrequency();
    // The group may come from a block before a block of no code, and on no
    // path at all after a call that does not return.
    const MachineBasicBlock *Prev = MBB.getPrevNode();
    uint64_t FallFreq =
        Prev->isSuccessor(&MBB)
            ? (MBFI.getBlockFreq(Prev) *
               MBFI.getMBPI()->getEdgeProbability(Prev, &MBB))
                  .getFrequency()
            : 0;
    if (StopsAlone < Stops && Freq > FallFreq / (Stops - StopsAlone))
      continue;
    Restore();
    Scan(MBB);
  }
  EndGroup();
  return true;
}

FunctionPass *llvm::createIA64InsertStopsPass() {
  return new IA64InsertStops();
}

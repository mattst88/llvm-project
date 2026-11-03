//===-- IA64ISelLowering.cpp - IA64 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ISelLowering.h"
#include "IA64.h"
#include "IA64CallingConv.h"
#include "IA64FrameLowering.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64SelectionDAGInfo.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "IA64TargetObjectFile.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsIA64.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

static const MCPhysReg Slots[] = {IA64::R32, IA64::R33, IA64::R34, IA64::R35,
                                  IA64::R36, IA64::R37, IA64::R38, IA64::R39};

// Argument slots are numbered across the registers and on into the stack,
// whose first slot is 16-byte aligned. Skip the next slot if it is odd, so a
// 16-byte-aligned argument starts at an even slot. The argument itself is
// not allocated here.
static void skipOddSlot(CCState &State) {
  unsigned Slot = State.getFirstUnallocated(Slots);
  if (Slot == std::size(Slots))
    Slot += State.getStackSize() / 8;
  if (Slot % 2 != 0 && !State.AllocateReg(Slots))
    State.AllocateStack(8, Align(8));
}

static bool CC_IA64_SkipOddSlot(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                CCValAssign::LocInfo &LocInfo,
                                ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (ArgFlags.getNonZeroOrigAlign() >= Align(16))
    skipOddSlot(State);
  return false;
}

// By-value aggregates occupy as many 8-byte slots as their size requires: a
// register for each slot while argument registers remain, the stack for the
// rest.
static bool CC_IA64_ByVal(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                          CCValAssign::LocInfo &LocInfo,
                          ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (ArgFlags.getNonZeroByValAlign() >= Align(16))
    skipOddSlot(State);
  unsigned NumSlots = alignTo(ArgFlags.getByValSize(), 8) / 8;
  bool InMemory = NumSlots == 0;
  for (; NumSlots != 0; --NumSlots) {
    MCRegister Reg = State.AllocateReg(Slots);
    if (!Reg) {
      InMemory = true;
      break;
    }
    State.addLoc(
        CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::i64, LocInfo));
  }
  // Zero-sized aggregates take no slot and sit where the next argument goes.
  if (InMemory)
    State.addLoc(CCValAssign::getCustomMem(
        ValNo, ValVT, State.AllocateStack(NumSlots * 8, Align(8)), MVT::i64,
        LocInfo));
  return true;
}

// The C++ ABI passes the address of a non-trivially-copyable return value as
// an ordinary first argument; the front end marks that sret pointer inreg.
static bool CC_IA64_SRet(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                         CCValAssign::LocInfo &LocInfo,
                         ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (ArgFlags.isInReg())
    return false;
  State.addLoc(CCValAssign::getReg(ValNo, ValVT, IA64::R8, LocVT, LocInfo));
  return true;
}

static const MCPhysReg FRs[] = {IA64::F8,  IA64::F9,  IA64::F10, IA64::F11,
                                IA64::F12, IA64::F13, IA64::F14, IA64::F15};

static bool CC_IA64_FP(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                       CCValAssign::LocInfo &LocInfo, ISD::ArgFlagsTy &ArgFlags,
                       CCState &State) {
  // Past the eighth slot, FP arguments go on the stack even if FP registers
  // remain.
  MCRegister Slot = State.AllocateReg(Slots);
  if (!Slot)
    return false;
  // Float HFA members pack two per slot, so FP registers can run out before
  // slots do; the value is then passed as raw bits in the slot.
  MCRegister Reg = State.AllocateReg(FRs);
  if (!Reg) {
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Slot, MVT::i64,
                                           CCValAssign::BCvt));
    return true;
  }
  State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  // In a varargs call this is a named argument, which the callee expects in
  // the FP register. A call to an unprototyped function is also a varargs
  // call, and its callee may not name the argument and look in the slot, so
  // pass it in both.
  if (State.isVarArg())
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Slot, MVT::i64,
                                           CCValAssign::BCvt));
  return true;
}

// An f80 argument is 16 bytes and 16-byte aligned: two slots, starting at an
// even one. An unnamed (varargs) f80 is passed as raw bits in the two slots,
// since the callee reads it from memory. An aggregate member declared with
// lower alignment takes the next slot, so it may be split between the last
// register and the stack.
static bool CC_IA64_Extended(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                             CCValAssign::LocInfo &LocInfo,
                             ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  bool Aligned = ArgFlags.getNonZeroMemAlign() >= Align(16);
  if (Aligned)
    skipOddSlot(State);
  MCRegister Lo = State.AllocateReg(Slots);
  if (!Lo) {
    State.addLoc(CCValAssign::getMem(
        ValNo, ValVT, State.AllocateStack(16, Align(Aligned ? 16 : 8)), LocVT,
        LocInfo));
    return true;
  }
  MCRegister Hi = State.AllocateReg(Slots);
  int64_t HiOffset = Hi ? 0 : State.AllocateStack(8, Align(8));
  auto AddSlots = [&] {
    State.addLoc(
        CCValAssign::getCustomReg(ValNo, ValVT, Lo, MVT::i64, LocInfo));
    if (Hi)
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, Hi, MVT::i64, LocInfo));
    else
      State.addLoc(
          CCValAssign::getCustomMem(ValNo, ValVT, HiOffset, MVT::i64, LocInfo));
  };
  MCRegister Reg;
  if (!ArgFlags.isVarArg())
    Reg = State.AllocateReg(FRs);
  if (!Reg) {
    AddSlots();
    return true;
  }
  State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  if (State.isVarArg())
    AddSlots();
  return true;
}

// An HFA occupies the slots its size requires, and each member that lands in
// one of the first eight slots goes in the next of f8-f15 while any remain.
// Two float members share a slot, as they share 8 bytes of memory.
static bool CC_IA64_HFA(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                        CCValAssign::LocInfo &LocInfo,
                        ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  SmallVectorImpl<CCValAssign> &Pending = State.getPendingLocs();
  Pending.push_back(CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));
  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  if (ValVT == MVT::f80) {
    // Each member is passed as a standalone f80.
    SmallVector<CCValAssign, 8> Members(Pending.begin(), Pending.end());
    Pending.clear();
    for (const CCValAssign &VA : Members) {
      unsigned No = VA.getValNo();
      CC_IA64_Extended(No, ValVT, LocVT, LocInfo, ArgFlags, State);
    }
    return true;
  }

  // A 16-byte-aligned HFA starts at an even slot like any other argument, and
  // its size is rounded up to a multiple of 16 bytes.
  unsigned Size = ValVT.getStoreSize();
  unsigned NumSlots = alignTo(Pending.size() * Size, 8) / 8;
  if (ArgFlags.getNonZeroMemAlign() >= Align(16)) {
    skipOddSlot(State);
    NumSlots = alignTo(NumSlots, 2);
  }
  SmallVector<std::pair<MCRegister, int64_t>, 8> Places;
  for (unsigned I = 0; I != NumSlots; ++I) {
    MCRegister Reg = State.AllocateReg(Slots);
    Places.push_back({Reg, Reg ? 0 : State.AllocateStack(8, Align(8))});
  }

  bool HadReg = false;
  for (unsigned I = 0, E = Pending.size(); I != E; ++I) {
    unsigned No = Pending[I].getValNo();
    unsigned Byte = I * Size;
    auto [Slot, Offset] = Places[Byte / 8];
    bool Upper = Byte % 8 != 0;
    if (!Slot) {
      State.addLoc(
          CCValAssign::getMem(No, ValVT, Offset + Byte % 8, LocVT, LocInfo));
      continue;
    }
    // An unnamed member is passed as raw bits in the slot, since the callee
    // reads it from memory.
    MCRegister Reg;
    if (!ArgFlags.isVarArg())
      Reg = State.AllocateReg(FRs);
    if (Reg)
      State.addLoc(CCValAssign::getReg(No, ValVT, Reg, LocVT, LocInfo));
    // In a varargs call the member may or may not be named, so pass it in
    // both. A member with no FP register left goes in the slot; if the first
    // half of its slot got the last FP register, it goes in the low half of
    // the GR. This matches GCC, and so what its callees expect. That first
    // half is then passed in its FP register only, since a callee that names
    // it looks for the second half where the first would be.
    bool LastReg = Reg == IA64::F15 && Size == 4 && !Upper && I + 1 != E;
    if (!Reg || (State.isVarArg() && !LastReg))
      State.addLoc(CCValAssign::getCustomReg(No, ValVT, Slot, MVT::i64,
                                             Upper && !(HadReg && !Reg)
                                                 ? CCValAssign::AExtUpper
                                                 : CCValAssign::BCvt));
    HadReg = Reg.isValid();
  }
  Pending.clear();
  return true;
}

// GCC passes <2 x float> in an FP register because it is an 8-byte FP mode,
// though no arithmetic is done on it there: its 8 bytes are the register's
// significand, as for an integer operand of a multiply.
static bool isSignificandType(const Type *Ty) {
  const auto *VTy = dyn_cast_or_null<FixedVectorType>(Ty);
  return VTy && VTy->getNumElements() == 2 &&
         VTy->getElementType()->isFloatTy();
}

static bool isSignificandLoc(const CCValAssign &VA) {
  return VA.isRegLoc() && VA.needsCustom() && VA.getValVT() == MVT::i64 &&
         VA.getLocVT() == MVT::f64;
}

// A named argument goes in the next of f8-f15 while its slot is a register
// and an FP register remains, and in the slot otherwise. The front end marks
// inreg the case GCC counts two FP registers for: an aggregate whose only
// non-empty member is a float _Complex.
static bool CC_IA64_Significand(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                CCValAssign::LocInfo &LocInfo,
                                ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (ArgFlags.isVarArg())
    return false;
  MCRegister Slot = State.AllocateReg(Slots);
  if (!Slot)
    return false;
  MCRegister Reg = State.AllocateReg(FRs);
  if (ArgFlags.isInReg())
    State.AllocateReg(FRs);
  if (!Reg) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Slot, LocVT, LocInfo));
    return true;
  }
  State.addLoc(
      CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::f64, LocInfo));
  // As for any FP-register argument, a varargs callee may not name it and
  // look in the slot.
  if (State.isVarArg())
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Slot, LocVT, LocInfo));
  return true;
}

// FP vectors are returned in f8 onward, 8 bytes per register.
static bool RetCC_IA64_Significand(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  MCRegister Reg = State.AllocateReg(FRs);
  if (!Reg)
    return false;
  State.addLoc(
      CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::f64, LocInfo));
  return true;
}

#define GET_CALLING_CONV_IMPL
#include "IA64GenCallingConv.inc"

static cl::opt<unsigned> MinJumpTableEntries(
    "ia64-min-jump-table-entries", cl::init(64), cl::Hidden,
    cl::desc("The fewest cases a switch may have to use a jump table"));

IA64TargetLowering::IA64TargetLowering(const IA64TargetMachine &TM,
                                       const IA64Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes.
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f32, &IA64::FRRegClass);
  addRegisterClass(MVT::f64, &IA64::FRRegClass);
  addRegisterClass(MVT::f80, &IA64::FRRegClass);

  setStackPointerRegisterToSaveRestore(IA64::R12);

  // Functions start on a 16-byte bundle boundary, and preferably on a
  // 64-byte one, the size of a line of the instruction cache: an entry part
  // way into a line wastes the rest of its first fetch, and a short function
  // spreads over one line more.
  setMinFunctionAlignment(Align(16));
  setPrefFunctionAlignment(Align(64));

  // The front end reads a 32-byte pair of bundles a cycle, so a loop whose
  // first bundle is the second of a pair takes a cycle more a pass when its
  // groups are two bundles each.
  setPrefLoopAlignment(Align(32));

  // A call costs a register stack frame and the callee's own setup, while
  // the stores issue two per cycle. Copies of unknown alignment are the
  // common case that needs more than eight: unless they are whole
  // doublewords, which IA64SelectionDAGInfo expands, they go a byte at a time.
  MaxStoresPerMemset = MaxStoresPerMemcpy = MaxStoresPerMemmove = 32;

  // A jump table's entry is two dependent loads and an add from the branch,
  // whose target reaches b6 too late to be predicted, so it takes about ten
  // cycles; a compare can issue in the same group as the branch it feeds, and
  // a group can hold three branches.
  setMinimumJumpTableEntries(MinJumpTableEntries);

  computeRegisterProperties(STI.getRegisterInfo());

  // FP registers hold every precision in a single format, so an extending
  // load needs no conversion. A truncating store does not round, though.
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f80, MVT::f32, Expand);
  setTruncStoreAction(MVT::f80, MVT::f64, Expand);

  // No instruction loads, stores or converts f16: it is moved as 16 bits and
  // converted by libcalls.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80}) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f16, Expand);
    setTruncStoreAction(VT, MVT::f16, Expand);
    setOperationAction({ISD::FP16_TO_FP, ISD::FP_TO_FP16,
                        ISD::STRICT_FP16_TO_FP, ISD::STRICT_FP_TO_FP16},
                       VT, Expand);
    // Likewise bf16, which is the upper half of a float and is shifted into
    // and out of one in a GR.
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::bf16, Expand);
    setTruncStoreAction(VT, MVT::bf16, Expand);
    setOperationAction({ISD::BF16_TO_FP, ISD::FP_TO_BF16}, VT, Custom);
  }

  // An f80 store writes the significand exactly as it is in the register, so
  // a denormal loaded as f32 or f64 must be normalized first.
  for (MVT VT : {MVT::f32, MVT::f64})
    setLoadExtAction(ISD::EXTLOAD, MVT::f80, VT, Expand);
  // Likewise an f32 denormal, which stfd and getf.d would not recognize as
  // the double it is.
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  // Loads and stores can post-increment the address register.
  for (MVT VT :
       {MVT::i8, MVT::i16, MVT::i32, MVT::i64, MVT::f32, MVT::f64, MVT::f80}) {
    setIndexedLoadAction(ISD::POST_INC, VT, Legal);
    setIndexedStoreAction(ISD::POST_INC, VT, Legal);
  }

  // f32 and f64 constants are materialized in a GR and moved to an FR.
  setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);
  // f80 constants do not fit in a GR and are loaded from memory.
  setOperationAction(ISD::ConstantFP, MVT::f80, Expand);

  // There are no divide or square root instructions, only reciprocal
  // approximations. f32 and f64 division and square root are expanded inline;
  // f80 has no spare precision in the register for the same sequences to be
  // accurate enough.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80}) {
    if (VT == MVT::f80)
      setOperationAction(ISD::FDIV, VT, LibCall);
    setOperationAction(ISD::FSQRT, VT, VT == MVT::f80 ? LibCall : Custom);
    setOperationAction(ISD::FREM, VT, LibCall);
    for (unsigned Opc : {ISD::FSIN, ISD::FCOS, ISD::FSINCOS, ISD::FPOW})
      setOperationAction(Opc, VT, Expand);
  }
  // Some default actions for f32 and f64 are not the defaults for f80.
  for (unsigned Opc : {ISD::LRINT, ISD::LLRINT, ISD::LROUND, ISD::LLROUND})
    setOperationAction(Opc, MVT::f80, LibCall);
  setOperationAction(ISD::FCANONICALIZE, MVT::f80, Expand);

  // Every FP instruction honors the dynamic rounding mode and raises
  // exceptions, so strict FP nodes select the same instructions.
  IsStrictFPEnabled = true;
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80}) {
    for (unsigned Opc :
         {ISD::STRICT_FADD, ISD::STRICT_FSUB, ISD::STRICT_FMUL, ISD::STRICT_FMA,
          ISD::STRICT_FP_ROUND, ISD::STRICT_FP_EXTEND, ISD::STRICT_FSETCC,
          ISD::STRICT_FSETCCS})
      setOperationAction(Opc, VT, Legal);
    setOperationAction(ISD::STRICT_FDIV, VT, VT == MVT::f80 ? LibCall : Legal);
    for (unsigned Opc : {ISD::STRICT_FREM, ISD::STRICT_FSQRT})
      setOperationAction(Opc, VT, LibCall);
  }
  for (unsigned Opc : {ISD::STRICT_FP_TO_SINT, ISD::STRICT_FP_TO_UINT,
                       ISD::STRICT_SINT_TO_FP, ISD::STRICT_UINT_TO_FP})
    setOperationAction(Opc, MVT::i64, Legal);
  for (unsigned Opc : {ISD::STRICT_LRINT, ISD::STRICT_LLRINT,
                       ISD::STRICT_LROUND, ISD::STRICT_LLROUND})
    setOperationAction(Opc, MVT::f80, LibCall);

  // fclass tests the FP class directly, which depends on more than the bits
  // a GR would hold.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80})
    setOperationAction(ISD::IS_FPCLASS, VT, Custom);

  // The bits of a single-precision value are half a general register.
  setOperationAction(ISD::BITCAST, MVT::i32, Custom);

  // Compares write predicates; materialized as a value, the result is 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);

  // Selects are predicated on the compare itself, so a select on a value
  // becomes a select on its comparison with zero.
  for (MVT VT : {MVT::i64, MVT::f32, MVT::f64, MVT::f80})
    setOperationAction(ISD::SELECT, VT, Expand);

  // No single compare implements ordered-not-equal or unordered-or-equal.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80}) {
    setCondCodeAction(ISD::SETONE, VT, Expand);
    setCondCodeAction(ISD::SETUEQ, VT, Expand);
  }
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // Counted loops branch with br.cloop.
  setTargetDAGCombine({ISD::BRCOND, ISD::INTRINSIC_VOID});
  setTargetDAGCombine({ISD::ADD, ISD::SUB, ISD::MUL, ISD::FDIV});
  setTargetDAGCombine({ISD::ZERO_EXTEND, ISD::AND, ISD::SETCC});

  // va_list is a pointer to the next argument; each argument occupies one or
  // more 8-byte slots.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  for (unsigned Opc : {ISD::VAARG, ISD::VACOPY, ISD::VAEND})
    setOperationAction(Opc, MVT::Other, Expand);
  setMinStackArgumentAlignment(Align(8));

  setOperationAction(ISD::RETURNADDR, MVT::i64, Custom);
  setOperationAction(ISD::FRAMEADDR, MVT::i64, Custom);
  setOperationAction(ISD::SPONENTRY, MVT::i64, Custom);

  // Unsupported operations are reported as errors. The i32 result of
  // EH_SJLJ_SETJMP and the operand of FPTRUNC_ROUND may have illegal types,
  // so these are custom-lowered before type legalization.
  setOperationAction(ISD::ADDROFRETURNADDR, MVT::i64, Custom);
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);
  setOperationAction(ISD::EH_RETURN, MVT::Other, Custom);
  for (MVT VT : MVT::fp_valuetypes())
    setOperationAction(ISD::FPTRUNC_ROUND, VT, Custom);
  // Its operand is an i32, so it must be lowered before type legalization.
  setOperationAction(ISD::EH_DWARF_CFA, MVT::i32, Custom);
  setOperationAction(ISD::INIT_TRAMPOLINE, MVT::Other, Custom);
  setOperationAction(ISD::ADJUST_TRAMPOLINE, MVT::i64, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, MVT::i64, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  // The rounding mode is two bits of the floating-point status register.
  setOperationAction(ISD::GET_ROUNDING, MVT::i64, Custom);
  setOperationAction(ISD::SET_ROUNDING, MVT::Other, Custom);

  // Prefetch, trap and debugtrap are single instructions.
  setOperationAction(ISD::PREFETCH, MVT::Other, Legal);
  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);

  // A misaligned doubleword is read from the two doublewords containing it.
  setOperationAction(ISD::LOAD, MVT::i64, Custom);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::JumpTable, MVT::i64, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i64, Custom);

  // A jump table branch loads the entry and branches through a BR.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // popcnt exists, and ctlz and cttz are expanded using it. clz exists only
  // on the latest processors.
  for (unsigned Opc : {ISD::CTLZ, ISD::CTTZ, ISD::BITREVERSE})
    setOperationAction(Opc, MVT::i64, Expand);

  // shrp does a rotate or funnel shift by a constant. A 32-bit rotate is
  // custom too, so that one by a constant becomes shrp of the value and the
  // value shifted to the upper half.
  for (unsigned Opc : {ISD::ROTL, ISD::ROTR, ISD::FSHL, ISD::FSHR})
    setOperationAction(Opc, MVT::i64, Custom);
  for (unsigned Opc : {ISD::ROTL, ISD::ROTR})
    setOperationAction(Opc, MVT::i32, Custom);

  // Expand double-register shifts inline instead of calling a libcall.
  for (unsigned Opc : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Opc, MVT::i64, Custom);

  // MUL_LOHI expands to two multiplies, one for each half.
  // Atomics wider than 64 bits are libcalls.
  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(8);

  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);

  // There is no integer divide instruction. Division is done in the FP unit,
  // or is a libcall when optimizing for size.
  for (unsigned Opc : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
    setOperationAction(Opc, MVT::i64, Custom);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);

  // No instruction sign-extends from i1; it takes two shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // No load sign-extends, and an i1 is a byte in memory.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    for (MVT MemVT : {MVT::i8, MVT::i16, MVT::i32})
      setLoadExtAction(ISD::SEXTLOAD, VT, MemVT, Expand);
  }
}

// Report an unsupported intrinsic as an error in the calling function
// instead of crashing.
static void reportUnsupported(SelectionDAG &DAG, const SDLoc &DL,
                              const Twine &What) {
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(),
      What + " is not supported on IA-64", DL.getDebugLoc()));
}

// A branch on llvm.loop.decrement, which br.cloop does in one instruction,
// decrementing ar.lc as it branches. The decrement moves to the branch.
static SDValue combineLoopBranch(SDNode *N, SelectionDAG &DAG) {
  SDValue Cond = N->getOperand(1);
  bool Exit = false;
  // Find the decrement under any inversions of it.
  while (Cond.hasOneUse()) {
    if (Cond.getOpcode() == ISD::XOR && isOneConstant(Cond.getOperand(1))) {
      Exit = !Exit;
      Cond = Cond.getOperand(0);
      continue;
    }
    if (Cond.getOpcode() == ISD::SETCC) {
      auto *RHS = dyn_cast<ConstantSDNode>(Cond.getOperand(1));
      ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
      if (!RHS || RHS->getZExtValue() > 1 ||
          (CC != ISD::SETEQ && CC != ISD::SETNE))
        break;
      // The value is 0 or 1, so comparing it with one value is comparing it
      // with the other, inverted.
      Exit ^= (CC == ISD::SETEQ) != RHS->isOne();
      Cond = Cond.getOperand(0);
      continue;
    }
    break;
  }
  if (!Cond.hasOneUse() || Cond.getOpcode() != ISD::INTRINSIC_W_CHAIN ||
      Cond.getResNo() != 0 ||
      Cond.getConstantOperandVal(1) != Intrinsic::loop_decrement ||
      !isOneConstant(Cond.getOperand(2)))
    return SDValue();
  SDLoc DL(N);
  DAG.ReplaceAllUsesOfValueWith(Cond.getValue(1), Cond.getOperand(0));
  return DAG.getNode(IA64ISD::BRLOOP, DL, MVT::Other, N->getOperand(0),
                     N->getOperand(2),
                     DAG.getTargetConstant(Exit, DL, MVT::i64));
}

// B + ext(C ? K + M : K) as (C ? B + ext(M) : B) + ext(K), where the add
// does not wrap for the extension: the wrap of an index into an array,
// "if (k < 0) k += n; a[k]". The extension of K then runs beside the compare
// instead of after the select, and B + ext(M) is often invariant. The add
// may still be K + (C ? M : 0).
static SDValue combineAddOfExtendedSelect(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  if (VT != MVT::i64)
    return SDValue();
  for (unsigned I = 0; I != 2; ++I) {
    SDValue B = N->getOperand(I), Ext = N->getOperand(1 - I);
    unsigned ExtOpc = Ext.getOpcode();
    if ((ExtOpc != ISD::SIGN_EXTEND && ExtOpc != ISD::ZERO_EXTEND) ||
        !Ext.hasOneUse())
      continue;
    auto NoWrap = [&](SDValue Add) {
      return Add.getOpcode() == ISD::ADD &&
             (ExtOpc == ISD::SIGN_EXTEND ? Add->getFlags().hasNoSignedWrap()
                                         : Add->getFlags().hasNoUnsignedWrap());
    };
    // The other operand of an add of X.
    auto Other = [](SDValue Add, SDValue X) {
      return Add.getOperand(0) == X   ? Add.getOperand(1)
             : Add.getOperand(1) == X ? Add.getOperand(0)
                                      : SDValue();
    };
    SDValue Sel = Ext.getOperand(0), K, M;
    bool WhenTrue = false;
    if (Sel.getOpcode() == ISD::SELECT && Sel.hasOneUse()) {
      // C ? K + M : K, or C ? K : K + M.
      for (unsigned Arm = 1; Arm != 3 && !M; ++Arm)
        if (NoWrap(Sel.getOperand(Arm)) &&
            (M = Other(Sel.getOperand(Arm), Sel.getOperand(3 - Arm)))) {
          K = Sel.getOperand(3 - Arm);
          WhenTrue = Arm == 1;
        }
    } else if (NoWrap(Sel) && Sel.hasOneUse()) {
      // K + (C ? M : 0), or K + (C ? 0 : M).
      for (unsigned Op = 0; Op != 2 && !M; ++Op) {
        SDValue S = Sel.getOperand(Op);
        if (S.getOpcode() != ISD::SELECT || !S.hasOneUse())
          continue;
        for (unsigned Arm = 1; Arm != 3 && !M; ++Arm)
          if (isNullConstant(S.getOperand(3 - Arm))) {
            M = S.getOperand(Arm);
            K = Sel.getOperand(1 - Op);
            WhenTrue = Arm == 1;
          }
        if (M)
          Sel = S;
      }
    }
    if (!M)
      continue;
    SDLoc DL(N);
    SDValue Moved =
        DAG.getNode(ISD::ADD, DL, VT, B, DAG.getNode(ExtOpc, DL, VT, M));
    SDValue Base = DAG.getSelect(DL, VT, Sel.getOperand(0),
                                 WhenTrue ? Moved : B, WhenTrue ? B : Moved);
    return DAG.getNode(ISD::ADD, DL, VT, Base, DAG.getNode(ExtOpc, DL, VT, K));
  }
  return SDValue();
}

namespace {
// A step of a multiply by a constant: T becomes (T << Amt) + X, and so on,
// where X is the multiplicand and T starts as X.
struct MulStep {
  enum Kind { Shl, AddX, SubX, AddT, SubT, Neg } K;
  unsigned Amt;
};
} // namespace

// The single-cycle ALU operations a step takes. shladd shifts by 1 to 4 and
// adds in one; a longer shift, or a shift before a subtract, is one more.
static unsigned mulStepCost(const MulStep &S) {
  if (S.K == MulStep::AddX || S.K == MulStep::AddT)
    return S.Amt <= 4 ? 1 : 2;
  return S.K == MulStep::SubX || S.K == MulStep::SubT ? 2 : 1;
}

// Whether X * C takes at most Budget operations, appending the steps if it
// does. Failed records the constants already found to need more.
static bool synthMul(uint64_t C, unsigned Budget,
                     SmallVectorImpl<MulStep> &Steps,
                     DenseSet<std::pair<uint64_t, unsigned>> &Failed) {
  if (C == 1)
    return true;
  if (C == 0 || Failed.contains({C, Budget}))
    return false;
  SmallVector<std::pair<MulStep, uint64_t>, 16> Preds;
  if (int64_t(C) < 0 && C != -C)
    Preds.push_back({{MulStep::Neg, 0}, -C});
  if (!(C & 1)) {
    unsigned M = llvm::countr_zero(C);
    Preds.push_back({{MulStep::Shl, M}, C >> M});
  } else {
    unsigned TZ = llvm::countr_zero(C - 1);
    for (unsigned K = 1; K <= std::min(TZ, 4u); ++K)
      Preds.push_back({{MulStep::AddX, K}, (C - 1) >> K});
    if (TZ > 4)
      Preds.push_back({{MulStep::AddX, TZ}, (C - 1) >> TZ});
    if (C + 1 != 0) {
      unsigned M = llvm::countr_zero(C + 1);
      Preds.push_back({{MulStep::SubX, M}, (C + 1) >> M});
    }
    for (unsigned K = 1; K < 63; ++K) {
      uint64_t D = (uint64_t(1) << K) + 1;
      if (C % D == 0)
        Preds.push_back({{MulStep::AddT, K}, C / D});
      D -= 2;
      if (K > 1 && C % D == 0)
        Preds.push_back({{MulStep::SubT, K}, C / D});
    }
  }
  for (auto &[S, Pred] : Preds) {
    unsigned Cost = mulStepCost(S);
    if (Cost <= Budget && synthMul(Pred, Budget - Cost, Steps, Failed)) {
      Steps.push_back(S);
      return true;
    }
  }
  Failed.insert({C, Budget});
  return false;
}

static cl::opt<unsigned> MulTreeMaxOps(
    "ia64-mul-tree-max-ops", cl::init(24), cl::Hidden,
    cl::desc("The most operations a multiply by a constant may take as a "
             "tree of shifted multiplicands (0 for none)"));

namespace {
// A term of a constant in signed binary: -1 or 1 times 2^Exp.
struct MulTerm {
  unsigned Exp;
  bool Neg;
};
// The sum of a run of terms, as Neg ? -V << Shift : V << Shift, Depth
// operations from X along its longest path and Ops in all.
struct MulNode {
  SDValue V;
  unsigned Shift;
  bool Neg;
  unsigned Depth, Ops;
};
} // namespace

// The terms of C in non-adjacent form, low first: no two in a row are both
// non-zero, so there are fewest of them.
static SmallVector<MulTerm, 32> mulTerms(uint64_t C) {
  SmallVector<MulTerm, 32> Terms;
  for (unsigned E = 0; C && E < 64; ++E, C >>= 1) {
    if (!(C & 1))
      continue;
    bool Neg = (C & 3) == 3 && E < 63;
    Terms.push_back({E, Neg});
    C = Neg ? C + 1 : C - 1;
  }
  return Terms;
}

// The sum of two runs of terms, the higher's shifted value and the lower's:
// a shladd, or a shift and an add or subtract.
static MulNode sumMulNodes(const MulNode &L, const MulNode &H) {
  bool Add = L.Neg == H.Neg;
  MulNode R = {SDValue(), L.Shift, H.Neg, 0, L.Ops + H.Ops + 1};
  if (Add && H.Shift - L.Shift <= 4) {
    R.Depth = std::max(L.Depth, H.Depth) + 1;
  } else {
    R.Depth = std::max(L.Depth, H.Depth + 1) + 1;
    ++R.Ops;
  }
  return R;
}

// The terms summed as a tree, each run split where its two halves' sum takes
// fewest operations in a row, then fewest in all. Split[I][J] is where the
// run from term I up to term J is split.
static MulNode buildMulTree(ArrayRef<MulTerm> Terms, SDValue X,
                            SelectionDAG *DAG, const SDLoc &DL) {
  size_t N = Terms.size();
  SmallVector<SmallVector<MulNode, 0>, 0> Best(N,
                                               SmallVector<MulNode, 0>(N + 1));
  SmallVector<SmallVector<unsigned, 0>, 0> Split(
      N, SmallVector<unsigned, 0>(N + 1));
  for (size_t I = 0; I != N; ++I)
    Best[I][I + 1] = {SDValue(), Terms[I].Exp, Terms[I].Neg, 0, 0};
  for (size_t Len = 2; Len <= N; ++Len) {
    for (size_t I = 0; I + Len <= N; ++I) {
      size_t J = I + Len;
      for (size_t M = I + 1; M != J; ++M) {
        MulNode R = sumMulNodes(Best[I][M], Best[M][J]);
        MulNode &B = Best[I][J];
        if (M == I + 1 || std::tie(R.Depth, R.Ops) < std::tie(B.Depth, B.Ops)) {
          B = R;
          Split[I][J] = M;
        }
      }
    }
  }
  std::function<MulNode(size_t, size_t)> Build = [&](size_t I, size_t J) {
    MulNode R = Best[I][J];
    if (J - I == 1) {
      R.V = X;
      return R;
    }
    MulNode L = Build(I, Split[I][J]), H = Build(Split[I][J], J);
    SDValue Shl = DAG->getNode(
        ISD::SHL, DL, MVT::i64, H.V,
        DAG->getShiftAmountConstant(H.Shift - L.Shift, MVT::i64, DL));
    R.V = DAG->getNode(L.Neg == H.Neg ? ISD::ADD : ISD::SUB, DL, MVT::i64, Shl,
                       L.V);
    return R;
  };
  return DAG ? Build(0, N) : Best[0][N];
}

// The tree with its final shift and negation.
static MulNode buildMulTree(uint64_t C, SDValue X, SelectionDAG *DAG,
                            const SDLoc &DL) {
  SmallVector<MulTerm, 32> Terms = mulTerms(C);
  MulNode N = buildMulTree(Terms, X, DAG, DL);
  if (N.Shift) {
    ++N.Depth;
    ++N.Ops;
    if (DAG)
      N.V = DAG->getNode(ISD::SHL, DL, MVT::i64, N.V,
                         DAG->getShiftAmountConstant(N.Shift, MVT::i64, DL));
  }
  if (N.Neg) {
    ++N.Depth;
    ++N.Ops;
    if (DAG)
      N.V = DAG->getNegative(N.V, DL, MVT::i64);
  }
  return N;
}

// A multiply by a constant as shifts, shladds and subtracts, when that is
// shorter than the round trip through the FP unit: setf.sig, xma.l and
// getf.sig take 14 cycles. The steps form a chain, a cycle each, and up to
// 12 of them beat fewer in mst. A tree of the constant's terms takes more
// operations but fewer in a row, and is taken instead when it has fewer in a
// row than the shortest chain, and at most MulTreeMaxOps in all. Done after legalization, where the multiply is i64, like RISC-V's
// expansion, and with the same disregard for undef X.
static SDValue combineMulByConstant(SDNode *N, SelectionDAG &DAG,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer() ||
      N->getValueType(0) != MVT::i64 ||
      DAG.getMachineFunction().getFunction().hasMinSize())
    return SDValue();
  auto *CN = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!CN)
    return SDValue();
  uint64_t C = CN->getZExtValue();
  SDLoc DL(N);
  SDValue X = N->getOperand(0), T = X;
  unsigned MaxChain = 12;
  bool Tree = false;
  if (MulTreeMaxOps && C) {
    MulNode Est = buildMulTree(C, X, nullptr, DL);
    if (Est.Ops <= MulTreeMaxOps && Est.Depth < 14) {
      Tree = true;
      MaxChain = std::min(MaxChain, Est.Depth);
    }
  }
  SmallVector<MulStep, 8> Steps;
  DenseSet<std::pair<uint64_t, unsigned>> Failed;
  unsigned Budget = 0;
  while (!synthMul(C, Budget, Steps, Failed)) {
    if (++Budget > MaxChain) {
      if (Tree)
        return buildMulTree(C, X, &DAG, DL).V;
      return SDValue();
    }
  }
  for (const MulStep &S : Steps) {
    if (S.K == MulStep::Neg) {
      T = DAG.getNegative(T, DL, MVT::i64);
      continue;
    }
    SDValue Shl = DAG.getNode(ISD::SHL, DL, MVT::i64, T,
                              DAG.getShiftAmountConstant(S.Amt, MVT::i64, DL));
    switch (S.K) {
    case MulStep::Shl:
      T = Shl;
      break;
    case MulStep::AddX:
    case MulStep::AddT:
      T = DAG.getNode(ISD::ADD, DL, MVT::i64, Shl,
                      S.K == MulStep::AddX ? X : T);
      break;
    default:
      T = DAG.getNode(ISD::SUB, DL, MVT::i64, Shl,
                      S.K == MulStep::SubX ? X : T);
      break;
    }
  }
  return T;
}

// A product of two values that fit in a halfword, as pmpy2.r, which
// multiplies the signed low halfwords of its operands into the low word of its
// result, rather than the round trip through the FP unit. It is an I0 op of 2
// cycles against setf.sig, xma.l and getf.sig's 14, and the operands need not
// be sign-extended first: see SimplifyDemandedBitsForTargetNode.
static SDValue combineMulOfHalfwords(SDNode *N, SelectionDAG &DAG,
                                     TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || N->getValueType(0) != MVT::i64)
    return SDValue();
  SDValue A = N->getOperand(0), B = N->getOperand(1);
  if (DAG.ComputeNumSignBits(A) < 49 || DAG.ComputeNumSignBits(B) < 49)
    return SDValue();
  SDLoc DL(N);
  return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64,
                     DAG.getNode(IA64ISD::PMPY2_R, DL, MVT::i64, A, B),
                     DAG.getValueType(MVT::i32));
}

// a / b for a constant b, as q + (a - b * q) * y with y = 1 / b rounded to
// double and q = a * y in the 64-bit precision and wide exponent range of
// status field 1. a - b * q is then about a * 2^-53 and the sum is within
// 2^-105 of a / b relative to it, while a / b, unless it is a midpoint, is
// further than that from every midpoint between results when b's significand
// has at most 32 bits. a / b is a midpoint only if it is subnormal and b is a
// multiple of 2, which is excluded. Only round-to-nearest gets the right
// result from a value that close, which non-strict division may assume. frcpa
// still screens out zeros, infinities and NaNs, which it divides itself.
static SDValue combineFDivByConstant(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  auto *CN = dyn_cast<ConstantFPSDNode>(N->getOperand(1));
  if ((VT != MVT::f32 && VT != MVT::f64) || !CN)
    return SDValue();
  bool LosesInfo;
  APFloat B = CN->getValueAPF();
  B.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &LosesInfo);
  if (!B.isNormal())
    return SDValue();
  // The significand, with the integer bit, scaled to be an odd integer.
  uint64_t Sig = B.bitcastToAPInt().getZExtValue() & ((1ULL << 52) - 1);
  Sig |= 1ULL << 52;
  unsigned Zeros = llvm::countr_zero(Sig);
  if (53 - Zeros > 32 || ilogb(B) - 52 + int(Zeros) > 0)
    return SDValue();
  APFloat Y(1.0);
  Y.divide(B, APFloat::rmNearestTiesToEven);
  SDLoc DL(N);
  return DAG.getNode(IA64ISD::FDIV_CONST, DL, VT, N->getOperand(0),
                     N->getOperand(1), DAG.getConstantFP(Y, DL, MVT::f64));
}

// a - q * b, where q came out of the FP unit, as xma.l of q and -b plus a
// there, rather than moving q back in for the multiply. This is how
// DAGCombiner takes the remainder when the quotient is also wanted.
static SDValue
combineSubOfMulBySignificand(SDNode *N, SelectionDAG &DAG,
                             TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || N->getValueType(0) != MVT::i64)
    return SDValue();
  SDValue Mul = N->getOperand(1);
  if (Mul.getOpcode() != ISD::MUL || !Mul.hasOneUse())
    return SDValue();
  SDValue Q = Mul.getOperand(0), B = Mul.getOperand(1);
  if (Q.getOpcode() != IA64ISD::GETF_SIG)
    std::swap(Q, B);
  if (Q.getOpcode() != IA64ISD::GETF_SIG || isa<ConstantSDNode>(B))
    return SDValue();
  SDLoc DL(N);
  SDValue NegB = DAG.getNode(IA64ISD::SETF_SIG, DL, MVT::f64,
                             DAG.getNegative(B, DL, MVT::i64));
  SDValue A = DAG.getNode(IA64ISD::SETF_SIG, DL, MVT::f64, N->getOperand(0));
  return DAG.getNode(
      IA64ISD::GETF_SIG, DL, MVT::i64,
      DAG.getNode(IA64ISD::XMA_L, DL, MVT::f64, Q.getOperand(0), NegB, A));
}

// zext (~(-1 << n)) from i32, a mask of the low n bits, is the same mask
// made in 64 bits: the i32 shift is poison for n of 32 or more, and for
// smaller n the 64-bit mask has no bits above 31. zstd's BIT_mask[n] lookup
// becomes this, and the zxt4 it would need waits for an I unit.
static SDValue combineZextOfLowMask(SDNode *N, SelectionDAG &DAG,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Not = N->getOperand(0);
  if (!DCI.isBeforeLegalize() || N->getValueType(0) != MVT::i64 ||
      Not.getValueType() != MVT::i32 || !isBitwiseNot(Not))
    return SDValue();
  SDValue Shl = Not.getOperand(0);
  if (Shl.getOpcode() != ISD::SHL || !isAllOnesConstant(Shl.getOperand(0)))
    return SDValue();
  SDLoc DL(N);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Count =
      DAG.getZExtOrTrunc(Shl.getOperand(1), DL,
                         TLI.getShiftAmountTy(MVT::i64, DAG.getDataLayout()));
  return DAG.getNOT(DL,
                    DAG.getNode(ISD::SHL, DL, MVT::i64,
                                DAG.getAllOnesConstant(DL, MVT::i64), Count),
                    MVT::i64);
}

// (and (xor X, Y), M) for a mask M of low bits, where X has no bits outside
// M, is (xor X, (and Y, M)). CoreMark's list search compares a byte it loads
// with ld1.s as ((X ^ Key) & 0xff) == 0: the mask of Key moves out of the
// loop and the compare takes X and Key directly, instead of after an xor and
// a zxt1.
static SDValue combineAndOfXorWithinMask(SDNode *N, SelectionDAG &DAG) {
  auto *M = dyn_cast<ConstantSDNode>(N->getOperand(1));
  SDValue Xor = N->getOperand(0);
  if (!M || !M->getAPIntValue().isMask() || Xor.getOpcode() != ISD::XOR ||
      !Xor.hasOneUse())
    return SDValue();
  const APInt &Mask = M->getAPIntValue();
  for (unsigned I = 0; I != 2; ++I) {
    SDValue X = Xor.getOperand(I), Y = Xor.getOperand(1 - I);
    if (!(~Mask).isSubsetOf(DAG.computeKnownBits(X).Zero))
      continue;
    SDLoc DL(N);
    EVT VT = N->getValueType(0);
    return DAG.getNode(ISD::XOR, DL, VT, X,
                       DAG.getNode(ISD::AND, DL, VT, Y, N->getOperand(1)));
  }
  return SDValue();
}

// (and (load i16 X), 0xff), where the load has other uses, is an ld1 of X:
// the low byte comes first. CPython's dispatch loads a 16-bit code unit for
// its opcode and argument; an ld1 of the opcode takes the zxt1 out of the
// chain that ends in the indirect branch, at the cost of a second load.
// Wider loads are left alone: the narrow load is not advanced with its
// partner, so bzip2's walk of a 32-bit tt[] took the partner's cache miss in
// it, and sha's 64-bit loads split into 32-bit halves scheduled worse.
static cl::opt<bool> NarrowSharedLoads(
    "ia64-narrow-shared-loads", cl::Hidden, cl::init(true),
    cl::desc("Load the masked low bytes of a shared load separately"));

static SDValue combineAndOfSharedLoad(SDNode *N, SelectionDAG &DAG) {
  if (!NarrowSharedLoads)
    return SDValue();
  auto *M = dyn_cast<ConstantSDNode>(N->getOperand(1));
  auto *Ld = dyn_cast<LoadSDNode>(N->getOperand(0));
  if (!M || !Ld || !Ld->isSimple() || !Ld->isUnindexed() ||
      Ld->getExtensionType() == ISD::SEXTLOAD || Ld->hasNUsesOfValue(1, 0) ||
      !DAG.getDataLayout().isLittleEndian())
    return SDValue();
  if (M->getAPIntValue() != 0xff || Ld->getMemoryVT() != MVT::i16)
    return SDValue();
  SDValue NewLd = DAG.getExtLoad(
      ISD::ZEXTLOAD, SDLoc(N), N->getValueType(0), Ld->getChain(),
      Ld->getBasePtr(), Ld->getPointerInfo(), MVT::i8, Ld->getBaseAlign(),
      Ld->getMemOperand()->getFlags(), Ld->getAAInfo());
  DAG.makeEquivalentMemoryOrdering(Ld, NewLd);
  return NewLd;
}

// (setcc (xor X, Y), 0, eq/ne) is (setcc X, Y, eq/ne): cmp takes two
// registers, and the xor would cost a group ahead of it.
static SDValue combineSetCCOfXor(SDNode *N, SelectionDAG &DAG) {
  SDValue Xor = N->getOperand(0);
  auto CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  if ((CC != ISD::SETEQ && CC != ISD::SETNE) || Xor.getOpcode() != ISD::XOR ||
      !Xor.hasOneUse() || !isNullConstant(N->getOperand(1)))
    return SDValue();
  return DAG.getSetCC(SDLoc(N), N->getValueType(0), Xor.getOperand(0),
                      Xor.getOperand(1), CC);
}

// (setcc (and (add X, C1), M), C2) unsigned, for a mask M of low bits,
// where X has no bits outside M, tests that X is in a range: (X - Lo) <u N,
// or not in one. A byte compared after an add, as CoreMark's isdigit,
// c - '0' <= 9 in i8, is promoted to this; the range test needs no zxt1
// between the add and the compare.
static SDValue combineSetCCOfMaskedAdd(SDNode *N, SelectionDAG &DAG,
                                       TargetLowering::DAGCombinerInfo &DCI) {
  SDValue And = N->getOperand(0);
  auto *C2 = dyn_cast<ConstantSDNode>(N->getOperand(1));
  auto CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  if (!C2 || !ISD::isUnsignedIntSetCC(CC) || And.getOpcode() != ISD::AND ||
      !And.hasOneUse())
    return SDValue();
  auto *M = dyn_cast<ConstantSDNode>(And.getOperand(1));
  SDValue Add = And.getOperand(0);
  if (!M || !M->getAPIntValue().isMask() || Add.getOpcode() != ISD::ADD ||
      !Add.hasOneUse())
    return SDValue();
  auto *C1 = dyn_cast<ConstantSDNode>(Add.getOperand(1));
  SDValue X = Add.getOperand(0);
  EVT VT = X.getValueType();
  const APInt &MaskBits = M->getAPIntValue();
  if (!C1 || VT.getSizeInBits() > 64 || MaskBits.isAllOnes())
    return SDValue();
  // An extending load of no more bits than M is ld1, ld2 or ld4, which
  // clear the rest anyway: make it a zero-extending one.
  auto *Ld = dyn_cast<LoadSDNode>(X);
  if (Ld && Ld->getExtensionType() == ISD::EXTLOAD && Ld->isUnindexed() &&
      Ld->isSimple() &&
      Ld->getMemoryVT().getSizeInBits() <= MaskBits.countr_one()) {
    SDValue ZLd = DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(Ld), VT, Ld->getChain(),
                                 Ld->getBasePtr(), Ld->getMemoryVT(),
                                 Ld->getMemOperand());
    DCI.CombineTo(Ld, ZLd, ZLd.getValue(1));
    return SDValue(N, 0);
  }
  if (!(~MaskBits).isSubsetOf(DAG.computeKnownBits(X).Zero))
    return SDValue();
  uint64_t Mask = MaskBits.getZExtValue(), V = C2->getZExtValue();
  if (V > Mask)
    return SDValue();
  // The values of the masked sum for which the compare holds, [Lo, Hi].
  uint64_t Lo = 0, Hi = Mask;
  switch (CC) {
  case ISD::SETULT:
    if (V == 0)
      return SDValue();
    Hi = V - 1;
    break;
  case ISD::SETULE:
    Hi = V;
    break;
  case ISD::SETUGT:
    if (V == Mask)
      return SDValue();
    Lo = V + 1;
    break;
  default:
    Lo = V;
    break;
  }
  // The values of X for them, which may wrap past Mask.
  uint64_t Add1 = C1->getZExtValue();
  Lo = (Lo - Add1) & Mask;
  Hi = (Hi - Add1) & Mask;
  SDLoc DL(N);
  EVT CCVT = N->getValueType(0);
  if (Lo <= Hi)
    return DAG.getSetCC(
        DL, CCVT, DAG.getNode(ISD::SUB, DL, VT, X, DAG.getConstant(Lo, DL, VT)),
        DAG.getConstant(Hi - Lo + 1, DL, VT), ISD::SETULT);
  // X is outside [Hi + 1, Lo - 1].
  return DAG.getSetCC(
      DL, CCVT,
      DAG.getNode(ISD::SUB, DL, VT, X, DAG.getConstant(Hi + 1, DL, VT)),
      DAG.getConstant(Lo - Hi - 1, DL, VT), ISD::SETUGE);
}

// X + zext(C) as C ? X + 1 : X, and X - zext(C) and X + sext(C) as
// C ? X - 1 : X: the add runs beside the compare and the predicated move
// after it, where the extension of the compare is a predicated move of 1
// that the add then waits for. The count of samples under a curve. Once
// the types are legal, when a zero-extended compare is the compare itself,
// and after the compare's own combines, which a select would hide.
static SDValue combineAddOfBool(SDNode *N, SelectionDAG &DAG,
                                TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);
  if (DCI.isBeforeLegalize() || !VT.isScalarInteger())
    return SDValue();
  bool IsSub = N->getOpcode() == ISD::SUB;
  for (unsigned I = 0; I != (IsSub ? 1 : 2); ++I) {
    SDValue X = N->getOperand(I), B = N->getOperand(1 - I);
    if (!B.hasOneUse() || isa<ConstantSDNode>(X))
      continue;
    bool Negative = false;
    SDValue C;
    if (B.getOpcode() == ISD::SETCC) {
      C = B;
    } else if ((B.getOpcode() == ISD::ZERO_EXTEND ||
                B.getOpcode() == ISD::SIGN_EXTEND) &&
               B.getOperand(0).getOpcode() == ISD::SETCC) {
      C = B.getOperand(0);
      Negative =
          B.getOpcode() == ISD::SIGN_EXTEND && C.getValueType() == MVT::i1;
    }
    if (!C)
      continue;
    if (SDValue V = combineSetCCOfMaskedAdd(C.getNode(), DAG, DCI))
      C = V;
    else if (SDValue V = combineSetCCOfXor(C.getNode(), DAG))
      C = V;
    bool Down = IsSub != Negative;
    SDLoc DL(N);
    SDValue Stepped = DAG.getNode(ISD::ADD, DL, VT, X,
                                  DAG.getSignedConstant(Down ? -1 : 1, DL, VT));
    return DAG.getSelect(DL, VT, C, Stepped, X);
  }
  return SDValue();
}

SDValue IA64TargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  switch (N->getOpcode()) {
  case ISD::BRCOND:
    return combineLoopBranch(N, DAG);
  case ISD::ADD:
    if (SDValue V = combineAddOfBool(N, DAG, DCI))
      return V;
    return combineAddOfExtendedSelect(N, DAG);
  case ISD::SUB:
    if (SDValue V = combineAddOfBool(N, DAG, DCI))
      return V;
    return combineSubOfMulBySignificand(N, DAG, DCI);
  case ISD::MUL:
    if (SDValue V = combineMulByConstant(N, DAG, DCI))
      return V;
    return combineMulOfHalfwords(N, DAG, DCI);
  case ISD::FDIV:
    return combineFDivByConstant(N, DAG);
  case ISD::ZERO_EXTEND:
    return combineZextOfLowMask(N, DAG, DCI);
  case ISD::AND:
    if (SDValue V = combineAndOfSharedLoad(N, DAG))
      return V;
    return combineAndOfXorWithinMask(N, DAG);
  case ISD::SETCC:
    if (SDValue V = combineSetCCOfMaskedAdd(N, DAG, DCI))
      return V;
    return combineSetCCOfXor(N, DAG);
  case ISD::INTRINSIC_VOID:
    if (N->getConstantOperandVal(1) == Intrinsic::set_loop_iterations) {
      SDLoc DL(N);
      SDValue Count = N->getOperand(2);
      return DAG.getNode(IA64ISD::SET_LC, DL, MVT::Other, N->getOperand(0),
                         DAG.getNode(ISD::ADD, DL, MVT::i64, Count,
                                     DAG.getAllOnesConstant(DL, MVT::i64)));
    }
    break;
  }
  return SDValue();
}

// A rotate or funnel shift by a constant is shrp, which shifts the 128-bit
// concatenation of two registers right. Others are expanded.
static SDValue lowerShiftRightPair(SDValue Op, SelectionDAG &DAG) {
  unsigned Opc = Op.getOpcode();
  bool IsFunnel = Opc == ISD::FSHL || Opc == ISD::FSHR;
  bool IsLeft = Opc == ISD::ROTL || Opc == ISD::FSHL;
  SDValue Hi = Op.getOperand(0);
  SDValue Lo = IsFunnel ? Op.getOperand(1) : Hi;
  auto *Amt = dyn_cast<ConstantSDNode>(Op.getOperand(IsFunnel ? 2 : 1));
  if (!Amt)
    return SDValue();
  unsigned Count = Amt->getZExtValue() % 64;
  if (Count == 0)
    return IsLeft ? Hi : Lo;
  SDLoc DL(Op);
  return DAG.getNode(IA64ISD::SHRP, DL, MVT::i64, Hi, Lo,
                     DAG.getTargetConstant(IsLeft ? 64 - Count : Count, DL,
                                           MVT::i64));
}

// A 32-bit rotate is a rotate of the value concatenated with itself: shrp of
// the value and its copy in the upper half when the count is a constant, and
// a shift of the 64-bit concatenation otherwise.
static SDValue lowerRotate32(SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);
  bool IsLeft = N->getOpcode() == ISD::ROTL;
  SDValue X = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));
  SDValue Upper = DAG.getNode(ISD::SHL, DL, MVT::i64, X,
                              DAG.getShiftAmountConstant(32, MVT::i64, DL));
  SDValue Res;
  if (auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
    unsigned Count = C->getZExtValue() % 32;
    if (Count == 0)
      return N->getOperand(0);
    // The low half of the result is bits 64 - left count up of Upper:X's.
    unsigned Left = IsLeft ? Count : 32 - Count;
    Res = DAG.getNode(IA64ISD::SHRP, DL, MVT::i64, X, Upper,
                      DAG.getTargetConstant(64 - Left, DL, MVT::i64));
  } else {
    SDValue Both = DAG.getNode(ISD::OR, DL, MVT::i64, Upper,
                               DAG.getZeroExtendInReg(X, DL, MVT::i32));
    SDValue Amt =
        DAG.getNode(ISD::AND, DL, MVT::i64,
                    DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(1)),
                    DAG.getConstant(31, DL, MVT::i64));
    Res = DAG.getNode(IsLeft ? ISD::SHL : ISD::SRL, DL, MVT::i64, Both, Amt);
    if (IsLeft)
      Res = DAG.getNode(ISD::SRL, DL, MVT::i64, Res,
                        DAG.getShiftAmountConstant(32, MVT::i64, DL));
  }
  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
}

SDValue IA64TargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::BITCAST:
    // i32 to f32, while i32 is not yet legalized.
    if (Op.getValueType() != MVT::f32 ||
        Op.getOperand(0).getValueType() != MVT::i32)
      return SDValue();
    return DAG.getNode(
        IA64ISD::SETF_S, SDLoc(Op), MVT::f32,
        DAG.getNode(ISD::ANY_EXTEND, SDLoc(Op), MVT::i64, Op.getOperand(0)));
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM: {
    // The integers are divided as significands (see SDIV_SIG), and the
    // remainder is a + q * -b, all without leaving the FP unit; the division
    // and remainder of the same operands share the quotient. A call is
    // smaller.
    SDLoc DL(Op);
    unsigned Opc = Op.getOpcode();
    bool Signed = Opc == ISD::SDIV || Opc == ISD::SREM;
    if (DAG.shouldOptForSize()) {
      RTLIB::Libcall LC = Opc == ISD::SDIV   ? RTLIB::SDIV_I64
                          : Opc == ISD::UDIV ? RTLIB::UDIV_I64
                          : Opc == ISD::SREM ? RTLIB::SREM_I64
                                             : RTLIB::UREM_I64;
      MakeLibCallOptions CallOptions;
      CallOptions.setIsSigned(Signed);
      return makeLibCall(DAG, LC, MVT::i64,
                         {Op.getOperand(0), Op.getOperand(1)}, CallOptions, DL)
          .first;
    }
    SDValue A = DAG.getNode(IA64ISD::SETF_SIG, DL, MVT::f64, Op.getOperand(0));
    SDValue B = DAG.getNode(IA64ISD::SETF_SIG, DL, MVT::f64, Op.getOperand(1));
    // Integers that fit in 32 bits, as those promoted from i32 do, take a
    // shorter sequence.
    auto Fits32 = [&](SDValue V) {
      return Signed ? DAG.ComputeNumSignBits(V) > 32
                    : DAG.computeKnownBits(V).countMinLeadingZeros() >= 32;
    };
    SDValue Q;
    if (Fits32(Op.getOperand(0)) && Fits32(Op.getOperand(1)))
      Q = DAG.getNode(Signed ? IA64ISD::SDIV32_SIG : IA64ISD::UDIV32_SIG, DL,
                      MVT::f64, A, B, DAG.getConstantFP(0x1p-34, DL, MVT::f64));
    else
      Q = DAG.getNode(Signed ? IA64ISD::SDIV_SIG : IA64ISD::UDIV_SIG, DL,
                      MVT::f64, A, B);
    if (Opc == ISD::SREM || Opc == ISD::UREM) {
      SDValue NegB =
          DAG.getNode(IA64ISD::SETF_SIG, DL, MVT::f64,
                      DAG.getNegative(Op.getOperand(1), DL, MVT::i64));
      Q = DAG.getNode(IA64ISD::XMA_L, DL, MVT::f64, Q, NegB, A);
    }
    return DAG.getNode(IA64ISD::GETF_SIG, DL, MVT::i64, Q);
  }
  case ISD::BF16_TO_FP: {
    // A bf16 is the upper 16 bits of a float.
    SDLoc DL(Op);
    SDValue Bits = DAG.getAnyExtOrTrunc(Op.getOperand(0), DL, MVT::i64);
    Bits = DAG.getNode(ISD::SHL, DL, MVT::i64, Bits,
                       DAG.getShiftAmountConstant(16, MVT::i64, DL));
    SDValue Res = DAG.getNode(IA64ISD::SETF_S, DL, MVT::f32, Bits);
    if (Op.getValueType() != MVT::f32)
      Res = DAG.getNode(ISD::FP_EXTEND, DL, Op.getValueType(), Res);
    return Res;
  }
  case ISD::FP_TO_BF16: {
    // Take the upper 16 bits of the float, rounding to nearest-even by
    // adding 0x7fff, plus one if the upper half is odd. A NaN is not rounded,
    // since it could turn into a number; it is quieted instead.
    SDLoc DL(Op);
    SDValue Src = Op.getOperand(0);
    // Rounding a wider value to float first would round twice, so round to
    // odd instead: if the float is inexact, take whichever of the two floats
    // bracketing the value is odd, which the second rounding then treats as
    // it would the exact value.
    SDValue Wide;
    if (Src.getValueType() != MVT::f32) {
      Wide = Src;
      Src = DAG.getNode(ISD::FP_ROUND, DL, MVT::f32, Wide,
                        DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
    }
    SDValue Bits = DAG.getNode(IA64ISD::GETF_S, DL, MVT::i64, Src);
    if (Wide) {
      EVT WideVT = Wide.getValueType();
      EVT CCVT =
          getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), WideVT);
      SDValue Back = DAG.getNode(ISD::FP_EXTEND, DL, WideVT, Src);
      SDValue Keep = DAG.getSetCC(DL, CCVT, Wide, Back, ISD::SETUEQ);
      SDValue Below =
          DAG.getSetCC(DL, CCVT, DAG.getNode(ISD::FABS, DL, WideVT, Wide),
                       DAG.getNode(ISD::FABS, DL, WideVT, Back), ISD::SETOGT);
      // The odd one of the two bracketing floats: the float or the next one
      // if the float is nearer zero than the value, else the float or the
      // previous one.
      SDValue Next = DAG.getNode(
          ISD::SUB, DL, MVT::i64, Bits,
          DAG.getSelect(DL, MVT::i64, Below, DAG.getConstant(0, DL, MVT::i64),
                        DAG.getConstant(1, DL, MVT::i64)));
      Next = DAG.getNode(ISD::OR, DL, MVT::i64, Next,
                         DAG.getConstant(1, DL, MVT::i64));
      Bits = DAG.getSelect(DL, MVT::i64, Keep, Bits, Next);
    }
    SDValue IsNaN = DAG.getSetCC(
        DL,
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f32),
        Src, Src, ISD::SETUO);
    SDValue NaN = DAG.getNode(ISD::OR, DL, MVT::i64, Bits,
                              DAG.getConstant(0x400000, DL, MVT::i64));
    SDValue Sixteen = DAG.getShiftAmountConstant(16, MVT::i64, DL);
    SDValue Odd = DAG.getNode(ISD::AND, DL, MVT::i64,
                              DAG.getNode(ISD::SRL, DL, MVT::i64, Bits, Sixteen),
                              DAG.getConstant(1, DL, MVT::i64));
    SDValue Rounded = DAG.getNode(
        ISD::ADD, DL, MVT::i64, Bits,
        DAG.getNode(ISD::ADD, DL, MVT::i64, Odd,
                    DAG.getConstant(0x7fff, DL, MVT::i64)));
    SDValue Res = DAG.getSelect(DL, MVT::i64, IsNaN, NaN, Rounded);
    Res = DAG.getNode(ISD::SRL, DL, MVT::i64, Res, Sixteen);
    return DAG.getAnyExtOrTrunc(Res, DL, Op.getValueType());
  }
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::GET_ROUNDING:
    return LowerGET_ROUNDING(Op, DAG);
  case ISD::SET_ROUNDING:
    return LowerSET_ROUNDING(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN:
    return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::SPONENTRY:
    return LowerSPONENTRY(Op, DAG);
  case ISD::ADDROFRETURNADDR:
    // The return link is in a branch register, not in memory.
    reportUnsupported(DAG, SDLoc(Op), "llvm.addressofreturnaddress");
    return DAG.getConstant(0, SDLoc(Op), MVT::i64);
  case ISD::EH_SJLJ_LONGJMP:
    reportUnsupported(DAG, SDLoc(Op), "llvm.eh.sjlj.longjmp");
    return Op.getOperand(0);
  case ISD::EH_RETURN:
    // Returning to a handler requires restoring its frame's register stack
    // as well as SP. GCC also rejects __builtin_eh_return on IA-64; libgcc's
    // unwinder restores both itself.
    reportUnsupported(DAG, SDLoc(Op), "llvm.eh.return");
    return Op.getOperand(0);
  case ISD::FPTRUNC_ROUND:
    reportUnsupported(DAG, SDLoc(Op), "llvm.fptrunc.round");
    return DAG.getPOISON(Op.getValueType());
  case ISD::EH_DWARF_CFA:
    return LowerEH_DWARF_CFA(Op, DAG);
  case ISD::INIT_TRAMPOLINE:
    return LowerINIT_TRAMPOLINE(Op, DAG);
  case ISD::ADJUST_TRAMPOLINE:
    // The trampoline is a function descriptor; its address is the function
    // pointer.
    return Op.getOperand(0);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    return LowerGET_DYNAMIC_AREA_OFFSET(Op, DAG);
  case ISD::FSQRT: {
    SDLoc DL(Op);
    SDValue Half = DAG.getConstantFP(0.5, DL, MVT::f64);
    if (Op.getValueType() == MVT::f64) {
      SmallVector<SDValue, 7> Ops = {Op.getOperand(0), Half};
      for (double C : {1.5, 2.5, 231.0 / 16, 63.0 / 8, 35.0 / 8})
        Ops.push_back(DAG.getConstantFP(C, DL, MVT::f64));
      return DAG.getNode(IA64ISD::FSQRT_D, DL, MVT::f64, Ops);
    }
    return DAG.getNode(IA64ISD::FSQRT_S, DL, MVT::f32, Op.getOperand(0), Half,
                       DAG.getConstantFP(0.375, DL, MVT::f64));
  }
  case ISD::IS_FPCLASS:
    return LowerIS_FPCLASS(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::ROTL:
  case ISD::ROTR:
  case ISD::FSHL:
  case ISD::FSHR:
    return lowerShiftRightPair(Op, DAG);
  case ISD::SHL_PARTS:
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS: {
    SDValue Lo, Hi;
    expandShiftParts(Op.getNode(), Lo, Hi, DAG);
    return DAG.getMergeValues({Lo, Hi}, SDLoc(Op));
  }
  default:
    llvm_unreachable("an operation with no custom lowering");
  }
}

void IA64TargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case ISD::BITCAST: {
    // f32 to i32.
    SDValue Src = N->getOperand(0);
    if (N->getValueType(0) != MVT::i32 || Src.getValueType() != MVT::f32)
      return;
    SDValue Bits = DAG.getNode(IA64ISD::GETF_S, DL, MVT::i64, Src);
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Bits));
    return;
  }
  case ISD::ROTL:
  case ISD::ROTR:
    Results.push_back(lowerRotate32(N, DAG));
    return;
  case ISD::EH_SJLJ_SETJMP:
    reportUnsupported(DAG, DL, "llvm.eh.sjlj.setjmp");
    Results.push_back(DAG.getConstant(0, DL, MVT::i32));
    Results.push_back(N->getOperand(0));
    return;
  case ISD::FPTRUNC_ROUND:
    reportUnsupported(DAG, DL, "llvm.fptrunc.round");
    Results.push_back(DAG.getPOISON(N->getValueType(0)));
    return;
  default:
    llvm_unreachable("a result with no custom lowering");
  }
}

// Stack maps and patch points are not implemented: neither the stack map
// section nor the patch space is emitted. Statepoints and calls with a
// "deopt" operand bundle (which become statepoints) are rejected too.
MachineBasicBlock *
IA64TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *MBB) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::STACKMAP:
  case TargetOpcode::PATCHPOINT:
  case TargetOpcode::STATEPOINT:
    reportFatalUsageError("stack maps, patch points and statepoints are not "
                          "supported on IA-64");
  case IA64::FDIV_S:
  case IA64::FDIV_D:
    return emitFDiv(MI, MBB);
  case IA64::FDIV_CONST_S:
  case IA64::FDIV_CONST_D:
    return emitFDivConst(MI, MBB);
  case IA64::FSQRT_S:
  case IA64::FSQRT_D:
    return emitFSqrt(MI, MBB);
  case IA64::SDIV_SIG:
  case IA64::UDIV_SIG:
  case IA64::SDIV32_SIG:
  case IA64::UDIV32_SIG:
    return emitIDiv(MI, MBB);
  default:
    return TargetLowering::EmitInstrWithCustomInserter(MI, MBB);
  }
}

// Whether Reg holds 1.0, which f1 always does.
static bool isOne(Register Reg, const MachineRegisterInfo &MRI) {
  if (Reg.isVirtual()) {
    const MachineInstr *Def = MRI.getVRegDef(Reg);
    if (!Def || !Def->isCopy())
      return false;
    Reg = Def->getOperand(1).getReg();
  }
  return Reg == IA64::F1;
}

// Returns what the last step of a division or square root leaves in place
// when it is predicated off: the approximation an frcpa or frsqrta step left
// in Y. Y is written while the operand is still live, so when the result is
// copied to a register, as it is to f8 when it is returned, the operand may
// hold that register and the copy would follow the last step. The result is
// then tied to a copy of Y instead, written once the operand is dead. That
// copy is an fmerge, since a COPY would be coalesced with Y.
static Register keepApproximation(MachineInstr &MI, MachineBasicBlock *MBB,
                                  const TargetInstrInfo &TII,
                                  MachineRegisterInfo &MRI, Register Y) {
  Register Res = MI.getOperand(0).getReg();
  if (!MRI.hasOneNonDBGUse(Res))
    return Y;
  const MachineInstr &Use = *MRI.use_instr_nodbg_begin(Res);
  if (!Use.isCopy() || !Use.getOperand(0).getReg().isPhysical())
    return Y;
  Register R = MRI.createVirtualRegister(&IA64::FRRegClass);
  BuildMI(*MBB, MI, MI.getDebugLoc(), TII.get(IA64::FMERGE_S), R)
      .addReg(Y)
      .addReg(Y)
      .addReg(IA64::P0);
  return R;
}

static cl::opt<int> FDivLateDividend(
    "ia64-fdiv-late-dividend", cl::Hidden, cl::init(0),
    cl::desc("Cycles by which a division's dividend must be ready after its "
             "divisor for frcpa(1, b) to start the divisor's reciprocal"));

static cl::opt<bool> FDivSqrtSeed(
    "ia64-fdiv-sqrt-seed", cl::Hidden, cl::init(true),
    cl::desc("Seed a division by a square root with the square root's "
             "reciprocal"));

// Follows Reg through copies of virtual registers.
static Register stripCopies(Register Reg, const MachineRegisterInfo &MRI) {
  while (Reg.isVirtual()) {
    const MachineInstr *Def = MRI.getVRegDef(Reg);
    if (!Def || !Def->isCopy())
      break;
    Reg = Def->getOperand(1).getReg();
  }
  return Reg;
}

// Whether Reg is what a call of the library's sqrt returns for X: a copy of
// f8 after a call of sqrt, which f8 was copied from X for.
static bool isSqrtCall(Register Reg, Register X,
                       const MachineRegisterInfo &MRI) {
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
  const MachineInstr *Copy = nullptr;
  while (Reg.isVirtual()) {
    Copy = MRI.getVRegDef(Reg);
    if (!Copy || !Copy->isCopy())
      return false;
    Reg = Copy->getOperand(1).getReg();
  }
  if (!Copy || Reg != IA64::F8)
    return false;
  bool Called = false;
  for (const MachineInstr &I :
       make_range(std::next(MachineBasicBlock::const_reverse_iterator(Copy)),
                  Copy->getParent()->rend())) {
    if (!Called) {
      if (I.isCall()) {
        const MachineOperand &Callee = I.getOperand(0);
        if (!Callee.isGlobal() || Callee.getGlobal()->getName() != "sqrt")
          return false;
        Called = true;
      } else if (I.modifiesRegister(IA64::F8, TRI)) {
        return false;
      }
    } else if (I.modifiesRegister(IA64::F8, TRI)) {
      return I.isCopy() &&
             stripCopies(I.getOperand(1).getReg(), MRI) == stripCopies(X, MRI);
    } else if (I.isCall()) {
      return false;
    }
  }
  return false;
}

// When B is a double square root expanded as at FSQRT_D, returns the
// approximation of 0.5 / sqrt(x) its last step uses. B may
// also be the square root's result joined with that of a call of sqrt for
// the same x, made where the expansion's result is a NaN, as
// PartiallyInlineLibCalls arranges to set errno.
static Register sqrtHalfReciprocal(Register B, MachineRegisterInfo &MRI) {
  MachineInstr *Last = MRI.getVRegDef(B);
  Register Other;
  if (Last && Last->isPHI()) {
    if (Last->getNumOperands() != 5)
      return Register();
    for (unsigned I : {1, 3}) {
      MachineInstr *In = MRI.getVRegDef(Last->getOperand(I).getReg());
      if (In && In->getOpcode() == IA64::FMA_D_MERGE) {
        Other = Last->getOperand(4 - I).getReg();
        Last = In;
        break;
      }
    }
  }
  if (!Last || Last->getOpcode() != IA64::FMA_D_MERGE)
    return Register();
  // The last step is h * (b - g * g) + g.
  MachineInstr *D = MRI.getVRegDef(Last->getOperand(2).getReg());
  if (!D || D->getOpcode() != IA64::FNMA_SUM ||
      D->getOperand(1).getReg() != D->getOperand(2).getReg() ||
      D->getOperand(2).getReg() != Last->getOperand(4).getReg())
    return Register();
  if (Other && !isSqrtCall(Other, D->getOperand(3).getReg(), MRI))
    return Register();
  Register H = Last->getOperand(3).getReg();
  // h is added in 2 * h = h * 1 + h.
  if (!MRI.constrainRegClass(H, &IA64::FRAddendRegClass))
    return Register();
  return H;
}

// The cycle, counted from the start of MBB, at which Reg is ready: the
// latency of the longest chain of instructions in MBB before MI that
// computes it, followed at most Limit instructions deep.
static unsigned readyCycle(Register Reg, const MachineBasicBlock *MBB,
                           const MachineRegisterInfo &MRI,
                           const TargetSchedModel &SM,
                           DenseMap<const MachineInstr *, unsigned> &Ready,
                           unsigned Limit = 32) {
  if (!Reg.isVirtual() || !Limit)
    return 0;
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (!Def || Def->getParent() != MBB || Def->isPHI())
    return 0;
  auto [It, Inserted] = Ready.try_emplace(Def, 0);
  if (!Inserted)
    return It->second;
  unsigned Cycle = 0;
  for (const MachineOperand &MO : Def->uses())
    if (MO.isReg())
      Cycle = std::max(Cycle,
                       readyCycle(MO.getReg(), MBB, MRI, SM, Ready, Limit - 1));
  if (!Def->isCopy() && !Def->isMetaInstruction())
    Cycle += SM.computeInstrLatency(Def);
  return Ready[Def] = Cycle;
}

// Division in Newton-Raphson steps after frcpa, as described at FDIV_S. Every
// step but frcpa is predicated on frcpa's predicate, which is clear when
// frcpa's result is the quotient already; the last step leaves that result
// in place then.
//
// When the dividend is ready well after the divisor, y is refined alone first,
// from frcpa of 1 and b, which approximates 1 / b the same way, and only
// q = a * y, rounded to the target precision, the remainder a - b * q and
// q + (a - b * q) * y wait for a: Intel's throughput-optimized sequence,
// which gcc uses. frcpa of a and b runs beside q for its predicate.
MachineBasicBlock *IA64TargetLowering::emitFDiv(MachineInstr &MI,
                                                MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  bool IsDouble = MI.getOpcode() == IA64::FDIV_D;
  Register Res = MI.getOperand(0).getReg();
  Register A = MI.getOperand(1).getReg();
  Register B = MI.getOperand(2).getReg();
  Register P = MRI.createVirtualRegister(&IA64::PRValueRegClass);
  auto NewFR = [&] { return MRI.createVirtualRegister(&IA64::FRRegClass); };
  uint32_t Flags = MI.getFlags();
  // Returns M1 * M2 + Addend (or Addend - M1 * M2) in status field 1.
  auto Step = [&](unsigned Opcode, Register M1, Register M2, Register Addend) {
    Register R = NewFR();
    BuildMI(*MBB, MI, DL, TII.get(Opcode), R)
        .addReg(M1)
        .addReg(M2)
        .addReg(Addend)
        .addImm(1)
        .addReg(P)
        .setMIFlags(Flags);
    return R;
  };
  Register Y0 = NewFR();
  MachineInstr *Y0Pos = BuildMI(*MBB, MI, DL, TII.get(IA64::FRCPA), Y0)
                            .addReg(P, RegState::Define)
                            .addReg(A)
                            .addReg(B)
                            .addImm(0)
                            .addReg(IA64::P0)
                            .setMIFlags(Flags);
  // With e = 1 - b * y, a / b is a * y / (1 - e), and 1 / (1 - e) is
  // (1 + e) * (1 + e^2) * (1 + e^4) and so on. When a is 1, q = a * y is y
  // and each step of q repeats that of y, so q is only formed when it is
  // rounded.
  bool Recip = isOne(A, MRI);
  if (Register H;
      IsDouble && !Recip && FDivSqrtSeed && (H = sqrtHalfReciprocal(B, MRI)))
    return emitFDivBySqrt(MI, MBB, H, Y0, P);
  if (!Recip) {
    TargetSchedModel SM;
    SM.init(&Subtarget);
    DenseMap<const MachineInstr *, unsigned> Ready;
    // Refine y = 1 / b alone, then q = a * y, r = a - b * q and q + r * y:
    // two steps fewer than refining q alongside y, one more on the longest
    // path. y starts from frcpa(a, b) unless a dividend ready after the
    // divisor would hold that back, or the divisor comes from another block:
    // then from frcpa(1, b), which with y can leave a loop that a does not,
    // or be shared with another division by b.
    Register YB = Y0, PB = P;
    MachineBasicBlock::iterator Pos = MI;
    if (!B.isVirtual() || MRI.getVRegDef(B)->getParent() != MBB ||
        (int)readyCycle(A, MBB, MRI, SM, Ready) >
            (int)readyCycle(B, MBB, MRI, SM, Ready) + FDivLateDividend) {
      PB = MRI.createVirtualRegister(&IA64::PRValueRegClass);
      YB = NewFR();
      Pos = Y0Pos;
      BuildMI(*MBB, Pos, DL, TII.get(IA64::FRCPA), YB)
          .addReg(PB, RegState::Define)
          .addReg(IA64::F1)
          .addReg(B)
          .addImm(1)
          .addReg(IA64::P0)
          .setMIFlags(Flags | MachineInstr::NoFPExcept);
    }
    // The steps refining y are predicated on frcpa(1, b)'s predicate where
    // that ran: it is set wherever frcpa(a, b)'s is.
    auto BStep = [&](unsigned Opcode, Register M1, Register M2,
                     Register Addend) {
      Register R = NewFR();
      BuildMI(*MBB, Pos, DL, TII.get(Opcode), R)
          .addReg(M1)
          .addReg(M2)
          .addReg(Addend)
          .addImm(1)
          .addReg(PB)
          .setMIFlags(Flags);
      return R;
    };
    Register E = BStep(IA64::FNMA, B, YB, IA64::F1);
    Register Y1 = BStep(IA64::FMA, E, YB, YB);
    Register Y;
    if (IsDouble) {
      Register E1 = BStep(IA64::FMA, E, E, IA64::F0);
      Register Y2 = BStep(IA64::FMA, E1, Y1, Y1);
      Register E2 = BStep(IA64::FMA, E1, E1, IA64::F0);
      Y = BStep(IA64::FMA, E2, Y2, Y2);
    } else {
      Y = BStep(IA64::FMA, E, Y1, YB);
    }
    // q is not predicated: where b's steps left a loop, a live-in predicate
    // on it would keep the loop from rotating, and in status field 1 what it
    // computes where the predicate is clear goes unseen.
    Register Q = NewFR();
    BuildMI(*MBB, MI, DL, TII.get(IsDouble ? IA64::FMA_D : IA64::FMA_S), Q)
        .addReg(A)
        .addReg(Y)
        .addReg(IA64::F0)
        .addImm(1)
        .addReg(IA64::P0)
        .setMIFlags(Flags);
    // The dividend is added, so it cannot be f0.
    Register R = Step(IA64::FNMA_SUM, B, Q, A);
    Register Kept = keepApproximation(MI, MBB, TII, MRI, Y0);
    BuildMI(*MBB, MI, DL,
            TII.get(IsDouble ? IA64::FMA_D_MERGE : IA64::FMA_S_MERGE), Res)
        .addReg(Kept)
        .addReg(R)
        .addReg(Y)
        .addReg(Q)
        .addImm(0)
        .addReg(P)
        .setMIFlags(Flags);
    MI.eraseFromParent();
    return MBB;
  }
  Register Y = Y0;
  Register E = Step(IA64::FNMA, B, Y, IA64::F1);
  Register T = Step(IA64::FMA, E, E, IA64::F0);
  Y = Step(IA64::FMA, E, Y, Y);
  if (IsDouble) {
    Register T2 = Step(IA64::FMA, T, T, IA64::F0);
    Y = Step(IA64::FMA, T, Y, Y);
    T = T2;
  }
  Register Q = Step(IsDouble ? IA64::FMA_D : IA64::FMA_S, T, Y, Y);
  Y = Step(IA64::FMA, T, Y, Y);
  // The dividend is added, so it cannot be f0, which would make the step a
  // multiply.
  E = Step(IA64::FNMA_SUM, B, Q, A);
  Register Kept = keepApproximation(MI, MBB, TII, MRI, Y0);
  BuildMI(*MBB, MI, DL,
          TII.get(IsDouble ? IA64::FMA_D_MERGE : IA64::FMA_S_MERGE), Res)
      .addReg(Kept)
      .addReg(E)
      .addReg(Y)
      .addReg(Q)
      .addImm(0)
      .addReg(P)
      .setMIFlags(Flags);
  MI.eraseFromParent();
  return MBB;
}

// Division of a by b = sqrt(x), the result of a square root expanded as at
// FSQRT_D, whose last step uses h, 0.5 / sqrt(x) to nearly the precision of
// the register format. 2 * h then approximates 1 / b far better than frcpa
// does: well enough that one Newton-Raphson step, for y and q = a * y
// together, makes y as accurate and q, rounded to double, as close to a / b
// as the steps of the throughput sequence at emitFDiv do, and its last two
// steps follow. That is 4 steps after b where that sequence takes 8.
// frcpa of a and b still runs for its predicate and for its result, which is
// the quotient when the predicate is clear.
//
// h is only computed where frsqrta's predicate is set. frsqrta needs software
// assistance, which clears it, only for an x at the extremes of the register
// format's range, which a double is not. Otherwise it is clear for x of zero,
// an infinity, a NaN or below zero, and then so is that of frcpa: b is zero,
// an infinity or a NaN. Until the last step, which is predicated, the steps
// raise no flag the program sees.
MachineBasicBlock *IA64TargetLowering::emitFDivBySqrt(MachineInstr &MI,
                                                      MachineBasicBlock *MBB,
                                                      Register H, Register Y0,
                                                      Register P) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  Register Res = MI.getOperand(0).getReg();
  Register A = MI.getOperand(1).getReg();
  Register B = MI.getOperand(2).getReg();
  uint32_t Flags = MI.getFlags();
  auto Step = [&](unsigned Opcode, Register M1, Register M2, Register Addend,
                  Register Pred = IA64::P0) {
    Register R = MRI.createVirtualRegister(&IA64::FRRegClass);
    BuildMI(*MBB, MI, DL, TII.get(Opcode), R)
        .addReg(M1)
        .addReg(M2)
        .addReg(Addend)
        .addImm(1)
        .addReg(Pred)
        .setMIFlags(Flags | MachineInstr::NoFPExcept);
    return R;
  };
  Register Y = Step(IA64::FMA_SUM, H, IA64::F1, H);
  Register E = Step(IA64::FNMA, B, Y, IA64::F1);
  Register Q = Step(IA64::FMA, A, Y, IA64::F0);
  Y = Step(IA64::FMA, E, Y, Y);
  Q = Step(IA64::FMA_D, E, Q, Q);
  // The dividend is added, so it cannot be f0.
  Register R = Step(IA64::FNMA_SUM, B, Q, A, P);
  Register Kept = keepApproximation(MI, MBB, TII, MRI, Y0);
  BuildMI(*MBB, MI, DL, TII.get(IA64::FMA_D_MERGE), Res)
      .addReg(Kept)
      .addReg(R)
      .addReg(Y)
      .addReg(Q)
      .addImm(0)
      .addReg(P)
      .setMIFlags(Flags);
  MI.eraseFromParent();
  return MBB;
}

// Division by a constant, as described at FDIV_CONST_S: q and the remainder
// do not wait for frcpa's predicate, and only the last step, which leaves
// frcpa's result in place when it is the quotient, is predicated.
MachineBasicBlock *
IA64TargetLowering::emitFDivConst(MachineInstr &MI,
                                  MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  bool IsDouble = MI.getOpcode() == IA64::FDIV_CONST_D;
  Register Res = MI.getOperand(0).getReg();
  Register A = MI.getOperand(1).getReg();
  Register B = MI.getOperand(2).getReg();
  Register Y = MI.getOperand(3).getReg();
  Register P = MRI.createVirtualRegister(&IA64::PRValueRegClass);
  Register R = MRI.createVirtualRegister(&IA64::FRRegClass);
  Register Q = MRI.createVirtualRegister(&IA64::FRRegClass);
  Register E = MRI.createVirtualRegister(&IA64::FRRegClass);
  uint32_t Flags = MI.getFlags();
  BuildMI(*MBB, MI, DL, TII.get(IA64::FRCPA), R)
      .addReg(P, RegState::Define)
      .addReg(A)
      .addReg(B)
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlags(Flags);
  BuildMI(*MBB, MI, DL, TII.get(IA64::FMA), Q)
      .addReg(A)
      .addReg(Y)
      .addReg(IA64::F0)
      .addImm(1)
      .addReg(IA64::P0)
      .setMIFlags(Flags);
  // The dividend is added, so it cannot be f0.
  BuildMI(*MBB, MI, DL, TII.get(IA64::FNMA_SUM), E)
      .addReg(B)
      .addReg(Q)
      .addReg(A)
      .addImm(1)
      .addReg(IA64::P0)
      .setMIFlags(Flags);
  Register Kept = keepApproximation(MI, MBB, TII, MRI, R);
  BuildMI(*MBB, MI, DL,
          TII.get(IsDouble ? IA64::FMA_D_MERGE : IA64::FMA_S_MERGE), Res)
      .addReg(Kept)
      .addReg(E)
      .addReg(Y)
      .addReg(Q)
      .addImm(0)
      .addReg(P)
      .setMIFlags(Flags);
  MI.eraseFromParent();
  return MBB;
}

static cl::opt<bool> SqrtThroughput(
    "ia64-sqrt-throughput", cl::Hidden, cl::init(true),
    cl::desc("Expand a double square root in fewer, longer steps unless a "
             "division by it starts from its approximation of 0.5 / sqrt(x)"));

// Returns whether a division by R, or by a join of R with a call's result as
// sqrtHalfReciprocal looks through, can start from R's square root steps.
static bool dividesBySqrt(Register R, const MachineRegisterInfo &MRI) {
  for (const MachineInstr &Use : MRI.use_nodbg_instructions(R)) {
    if (Use.getOpcode() == IA64::FDIV_D && Use.getOperand(2).getReg() == R)
      return true;
    if (Use.isPHI()) {
      Register Join = Use.getOperand(0).getReg();
      for (const MachineInstr &JoinUse : MRI.use_nodbg_instructions(Join))
        if (JoinUse.getOpcode() == IA64::FDIV_D &&
            JoinUse.getOperand(2).getReg() == Join)
          return true;
    }
  }
  return false;
}

// Square root in steps after frsqrta, as described at FSQRT_S. Every step
// but frsqrta is predicated on frsqrta's predicate, which is clear when
// frsqrta's result is the square root already; the last step leaves that
// result in place then.
MachineBasicBlock *IA64TargetLowering::emitFSqrt(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  bool IsDouble = MI.getOpcode() == IA64::FSQRT_D;
  Register Res = MI.getOperand(0).getReg();
  Register B = MI.getOperand(1).getReg();
  Register Half = MI.getOperand(2).getReg();
  Register P = MRI.createVirtualRegister(&IA64::PRValueRegClass);
  uint32_t Flags = MI.getFlags();
  // Returns M1 * M2 + Addend (or Addend - M1 * M2) in status field 1.
  auto Step = [&](unsigned Opcode, Register M1, Register M2, Register Addend) {
    Register R = MRI.createVirtualRegister(&IA64::FRRegClass);
    BuildMI(*MBB, MI, DL, TII.get(Opcode), R)
        .addReg(M1)
        .addReg(M2)
        .addReg(Addend)
        .addImm(1)
        .addReg(P)
        .setMIFlags(Flags);
    return R;
  };
  Register Y = MRI.createVirtualRegister(&IA64::FRRegClass);
  BuildMI(*MBB, MI, DL, TII.get(IA64::FRSQRTA), Y)
      .addReg(P, RegState::Define)
      .addReg(B)
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlags(Flags);
  // b and 0.5 are added, so they cannot be f0, which would make the step a
  // multiply.
  Register G, H;
  if (IsDouble && SqrtThroughput && !dividesBySqrt(Res, MRI)) {
    // Two steps taking g to sqrt(x) and h to 0.5 / sqrt(x) together double
    // their accuracy, and one more corrects g from its residual. This is four
    // fewer operations than the sequence below for one more step of chain,
    // and h ends only half as accurate, which is why a division by the result
    // uses the sequence below.
    G = Step(IA64::FMA, B, Y, IA64::F0);
    H = Step(IA64::FMA, Half, Y, IA64::F0);
    for (unsigned I = 0; I != 2; ++I) {
      Register E = Step(IA64::FNMA_SUM, G, H, Half);
      G = Step(IA64::FMA, E, G, G);
      H = Step(IA64::FMA, E, H, H);
    }
    G = Step(IA64::FMA, Step(IA64::FNMA_SUM, G, G, B), H, G);
  } else if (IsDouble) {
    Register C1 = MI.getOperand(3).getReg(), C2 = MI.getOperand(4).getReg();
    Register C3 = MI.getOperand(5).getReg(), C4 = MI.getOperand(6).getReg();
    Register C5 = MI.getOperand(7).getReg();
    G = Step(IA64::FMA, B, Y, IA64::F0);
    H = Step(IA64::FMA, Half, Y, IA64::F0);
    Register E = Step(IA64::FNMA_SUM, G, H, Half);
    Register E2 = Step(IA64::FMA, E, E, IA64::F0);
    // g and h are each multiplied by 1 + Lo + e^4 * Hi, where Lo holds the
    // terms of the polynomial in e below e^4 and Hi those above, over e^4.
    Register Lo = Step(IA64::FMA_SUM, C2, E, C1);
    Register Hi = Step(IA64::FMA_SUM, C3, E, C4);
    Register E4 = Step(IA64::FMA, E2, E2, IA64::F0);
    Lo = Step(IA64::FMA, E2, Lo, E);
    Hi = Step(IA64::FMA, E, Hi, C5);
    Register G1 = Step(IA64::FMA, Lo, G, G);
    Register T = Step(IA64::FMA, G, E4, IA64::F0);
    Register HCorr = Step(IA64::FMA, E4, Hi, Lo);
    G = Step(IA64::FMA_D, Hi, T, G1);
    H = Step(IA64::FMA, HCorr, H, H);
  } else {
    Register C = MI.getOperand(3).getReg();
    Register E = Step(IA64::FNMA, Step(IA64::FMA, B, Y, IA64::F0), Y, IA64::F1);
    Register S = Step(IA64::FMA_SUM, C, E, Half);
    Register F = Step(IA64::FMA, Y, E, IA64::F0);
    Register Y1 = Step(IA64::FMA, F, S, Y);
    G = Step(IA64::FMA_S, B, Y1, IA64::F0);
    H = Step(IA64::FMA, Half, Y1, IA64::F0);
  }
  Register D = Step(IA64::FNMA_SUM, G, G, B);
  Register Kept = keepApproximation(MI, MBB, TII, MRI, Y);
  BuildMI(*MBB, MI, DL,
          TII.get(IsDouble ? IA64::FMA_D_MERGE : IA64::FMA_S_MERGE), Res)
      .addReg(Kept)
      .addReg(D)
      .addReg(H)
      .addReg(G)
      .addImm(0)
      .addReg(P)
      .setMIFlags(Flags);
  MI.eraseFromParent();
  return MBB;
}

// Integer division, as described at SDIV_SIG. No step is predicated, and
// none raises a flag the program sees, being in status field 1 (fcvt.xf
// raises none), so the steps can be moved as freely as the pseudo could.
MachineBasicBlock *IA64TargetLowering::emitIDiv(MachineInstr &MI,
                                                MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();
  bool Signed = Opc == IA64::SDIV_SIG || Opc == IA64::SDIV32_SIG;
  auto NewFR = [&] { return MRI.createVirtualRegister(&IA64::FRRegClass); };
  // Returns M1 * M2 + Addend (or Addend - M1 * M2) in status field 1.
  auto Step = [&](unsigned Opcode, Register M1, Register M2, Register Addend,
                  Register R = Register()) {
    if (!R)
      R = NewFR();
    BuildMI(*MBB, MI, DL, TII.get(Opcode), R)
        .addReg(M1)
        .addReg(M2)
        .addReg(Addend)
        .addImm(1)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::NoFPExcept);
    return R;
  };
  // A signed integer is converted with fcvt.xf; an unsigned one is a valid
  // FP value as it is and only needs normalizing. The dividend is added to
  // form the remainder, so it cannot be f0.
  auto Convert = [&](Register S) {
    Register R = MRI.createVirtualRegister(&IA64::FRAddendRegClass);
    if (!Signed)
      return Step(IA64::FMA, S, IA64::F1, IA64::F0, R);
    BuildMI(*MBB, MI, DL, TII.get(IA64::FCVT_XF), R)
        .addReg(S)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::NoFPExcept);
    return R;
  };
  Register FA = Convert(MI.getOperand(1).getReg());
  Register FB = Convert(MI.getOperand(2).getReg());
  Register Y = NewFR();
  BuildMI(*MBB, MI, DL, TII.get(IA64::FRCPA), Y)
      .addReg(MRI.createVirtualRegister(&IA64::PRValueRegClass),
              RegState::Define | RegState::Dead)
      .addReg(FA)
      .addReg(FB)
      .addImm(1)
      .addReg(IA64::P0)
      .setMIFlag(MachineInstr::NoFPExcept);
  // With e = 1 - b * y, 1 / b is y * (1 + e) * (1 + e^2) and so on.
  Register E = Step(IA64::FNMA, FB, Y, IA64::F1);
  if (Opc == IA64::SDIV32_SIG || Opc == IA64::UDIV32_SIG) {
    // q = a * y, then q * (1 + e) * (1 + 2^-34 + e^2).
    Register Q = Step(IA64::FMA, FA, Y, IA64::F0);
    Q = Step(IA64::FMA, Q, E, Q);
    E = Step(IA64::FMA, E, E, MI.getOperand(3).getReg());
    Y = Step(IA64::FMA, E, Q, Q);
  } else {
    // q = a * y, after two steps, and q + (a - b * q) * y.
    Register T = Step(IA64::FMA, E, E, IA64::F0);
    Y = Step(IA64::FMA, Y, E, Y);
    Y = Step(IA64::FMA, Y, T, Y);
    Register Q = Step(IA64::FMA, Y, FA, IA64::F0);
    Register R = Step(IA64::FNMA_SUM, FB, Q, FA);
    Y = Step(IA64::FMA, R, Y, Q);
  }
  BuildMI(*MBB, MI, DL,
          TII.get(Signed ? IA64::FCVT_FX_TRUNC : IA64::FCVT_FXU_TRUNC),
          MI.getOperand(0).getReg())
      .addReg(Y)
      .addImm(1)
      .addReg(IA64::P0)
      .setMIFlag(MachineInstr::NoFPExcept);
  MI.eraseFromParent();
  return MBB;
}

// The address of a symbol that may be preemptible is loaded from the linkage
// table (GOT), addressed via GP. A function's entry holds the address of its
// canonical function descriptor, which is the function pointer.
//
// A DSO-local symbol is at a link-time constant offset from GP, so no load
// is needed.
SDValue IA64TargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  const TargetMachine &TM = getTargetMachine();
  int64_t Offset = N->getOffset();
  SDLoc DL(Op);

  auto WithOffset = [&](SDValue Addr, int64_t Offset) {
    if (Offset == 0)
      return Addr;
    return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                       DAG.getConstant(Offset, DL, MVT::i64));
  };

  // A small-code-model variable is within reach of an absolute 22-bit add
  // (from r0), whichever module defines it. Fold the offset into the symbol
  // only if it stays inside the object, which is then in reach too.
  if (const auto *Var = dyn_cast<GlobalVariable>(GV);
      Var && Var->getCodeModel() == CodeModel::Small) {
    uint64_t Size =
        Var->getValueType()->isSized()
            ? DAG.getDataLayout().getTypeAllocSize(Var->getValueType())
            : 0;
    int64_t InObject = Offset >= 0 && uint64_t(Offset) < Size ? Offset : 0;
    return WithOffset(
        DAG.getNode(IA64ISD::ABS22, DL, MVT::i64,
                    DAG.getTargetGlobalAddress(GV, DL, MVT::i64, InObject)),
        Offset - InObject);
  }

  // An undefined weak symbol is not at a fixed offset from GP.
  if (GV->getValueType()->isFunctionTy() || !TM.shouldAssumeDSOLocal(GV) ||
      GV->hasExternalWeakLinkage()) {
    unsigned Flags =
        GV->getValueType()->isFunctionTy() ? IA64::S_LTOFF_FPTR : IA64::S_LTOFF;
    return WithOffset(
        loadLinkageTableEntry(
            DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags), DL, DAG),
        Offset);
  }

  // The folded offset must keep the address in reach of the 22-bit add,
  // which holds if it stays inside the object.
  const auto &TLOF =
      static_cast<const IA64TargetObjectFile &>(*TM.getObjFileLowering());
  if (TLOF.isShortData(GV, TM)) {
    int64_t InObject =
        Offset >= 0 && uint64_t(Offset) < TLOF.getShortDataLimit() ? Offset : 0;
    return WithOffset(
        DAG.getNode(IA64ISD::GPREL22, DL, MVT::i64,
                    DAG.getTargetGlobalAddress(GV, DL, MVT::i64, InObject,
                                               IA64::S_GPREL)),
        Offset - InObject);
  }
  return DAG.getNode(
      IA64ISD::GPREL64, DL, MVT::i64,
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, Offset, IA64::S_GPREL));
}

// From the executable, a TLS variable is at an offset from the thread
// pointer: a link-time constant if the executable defines it (local-exec),
// or loaded from a GOT entry the dynamic loader fills in (initial-exec).
// Otherwise its module ID and DTP-relative offset are passed to
// __tls_get_addr (general-dynamic); for a symbol in this module, the
// link-time constant offset is added to the result of __tls_get_addr for
// offset 0 (local-dynamic).
SDValue IA64TargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  const auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  SDLoc DL(Op);

  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(N, DAG);

  auto Sym = [&](unsigned Flags) {
    return DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, Flags);
  };
  auto ThreadPointer = [&] {
    return DAG.getCopyFromReg(DAG.getEntryNode(), DL, IA64::R13, MVT::i64);
  };
  auto GetAddr = [&](SDValue Module, SDValue Offset) {
    ArgListTy Args;
    Type *Ty = Type::getInt64Ty(*DAG.getContext());
    Args.emplace_back(Module, Ty);
    Args.emplace_back(Offset, Ty);
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(DAG.getEntryNode())
        .setLibCallee(CallingConv::C, PointerType::getUnqual(*DAG.getContext()),
                      DAG.getExternalSymbol("__tls_get_addr", MVT::i64),
                      std::move(Args));
    return LowerCallTo(CLI).first;
  };

  // A link-time constant offset is added to its base with a single add if
  // it fits in 22 bits, which is assumed unless -mtls-size says otherwise.
  // A 14-bit offset lets the add use any base register; a 64-bit one is
  // materialized first.
  unsigned Size = getTargetMachine().Options.TLSSize;
  Size = Size == 0 ? 22 : Size <= 14 ? 14 : Size <= 22 ? 22 : 64;
  auto Known = [&](unsigned Flags, SDValue Base) {
    return DAG.getNode(Size == 14 ? IA64ISD::TLSOFF14 : IA64ISD::TLSOFF, DL,
                       MVT::i64, Sym(Flags), Base);
  };

  SDValue Addr;
  switch (getTargetMachine().getTLSModel(GV)) {
  case TLSModel::LocalExec:
    if (Size == 64)
      Addr = DAG.getNode(
          ISD::ADD, DL, MVT::i64,
          DAG.getNode(IA64ISD::TLSOFF64, DL, MVT::i64, Sym(IA64::S_TPREL)),
          ThreadPointer());
    else
      Addr = Known(IA64::S_TPREL, ThreadPointer());
    break;
  case TLSModel::InitialExec:
    Addr = DAG.getNode(ISD::ADD, DL, MVT::i64,
                       loadLinkageTableEntry(Sym(IA64::S_LTOFF_TPREL), DL, DAG),
                       ThreadPointer());
    break;
  case TLSModel::LocalDynamic: {
    SDValue Module =
        GetAddr(loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPMOD), DL, DAG),
                DAG.getConstant(0, DL, MVT::i64));
    if (Size == 64)
      Addr = DAG.getNode(
          ISD::ADD, DL, MVT::i64,
          loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPREL), DL, DAG), Module);
    else
      Addr = Known(IA64::S_DTPREL, Module);
    break;
  }
  case TLSModel::GeneralDynamic:
    Addr = GetAddr(loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPMOD), DL, DAG),
                   loadLinkageTableEntry(Sym(IA64::S_LTOFF_DTPREL), DL, DAG));
    break;
  }
  if (N->getOffset() == 0)
    return Addr;
  return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                     DAG.getConstant(N->getOffset(), DL, MVT::i64));
}

SDValue IA64TargetLowering::loadLinkageTableEntry(SDValue Sym, const SDLoc &DL,
                                                  SelectionDAG &DAG) const {
  SDValue Entry = DAG.getNode(IA64ISD::LTOFF, DL, MVT::i64, Sym);
  return DAG.getLoad(
      MVT::i64, DL, DAG.getEntryNode(), Entry,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), Align(8),
      MachineMemOperand::MODereferenceable | MachineMemOperand::MOInvariant);
}

SDValue IA64TargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  const auto *N = cast<JumpTableSDNode>(Op);
  return loadLinkageTableEntry(
      DAG.getTargetJumpTable(N->getIndex(), MVT::i64, IA64::S_LTOFF), SDLoc(Op),
      DAG);
}

SDValue IA64TargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  const auto *N = cast<BlockAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue Addr = loadLinkageTableEntry(
      DAG.getTargetBlockAddress(N->getBlockAddress(), MVT::i64, 0,
                                IA64::S_LTOFF),
      DL, DAG);
  if (N->getOffset() == 0)
    return Addr;
  return DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                     DAG.getConstant(N->getOffset(), DL, MVT::i64));
}

// Constant pool entries are in this module, at a link-time constant offset
// from GP.
SDValue IA64TargetLowering::LowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  const auto *N = cast<ConstantPoolSDNode>(Op);
  assert(!N->isMachineConstantPoolEntry() && "no constants of the target's");
  return DAG.getNode(IA64ISD::GPREL64, SDLoc(Op), MVT::i64,
                     DAG.getTargetConstantPool(N->getConstVal(), MVT::i64,
                                               N->getAlign(), N->getOffset(),
                                               IA64::S_GPREL));
}

// va_list is the address of the next argument.
SDValue IA64TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), SDLoc(Op),
                      DAG.getFrameIndex(FI->getVarArgsFrameIndex(), MVT::i64),
                      Op.getOperand(1), MachinePointerInfo(SV));
}

// The return address is in b0 on entry. Only an unwinder can recover a
// caller's.
// The rounding control of status field sf0 is bits 10-11 of ar.fpsr: 0 is
// to nearest, 1 down, 2 up, 3 toward zero. FLT_ROUNDS numbers them 1, 3, 2,
// 0. Each table constant below maps one encoding to the other, two bits per
// entry.
static const unsigned RoundingControlShift = 10;

SDValue IA64TargetLowering::LowerGET_ROUNDING(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue FPSR(DAG.getMachineNode(IA64::GET_FPSR, DL, MVT::i64, MVT::Other,
                                  Op.getOperand(0)),
               0);
  SDValue RC =
      DAG.getNode(ISD::SRL, DL, MVT::i64, FPSR,
                  DAG.getConstant(RoundingControlShift - 1, DL, MVT::i64));
  RC =
      DAG.getNode(ISD::AND, DL, MVT::i64, RC, DAG.getConstant(6, DL, MVT::i64));
  SDValue Mode = DAG.getNode(ISD::SRL, DL, MVT::i64,
                             DAG.getConstant(0x2d, DL, MVT::i64), RC);
  Mode = DAG.getNode(ISD::AND, DL, MVT::i64, Mode,
                     DAG.getConstant(3, DL, MVT::i64));
  return DAG.getMergeValues(
      {DAG.getZExtOrTrunc(Mode, DL, VT), FPSR.getValue(1)}, DL);
}

SDValue IA64TargetLowering::LowerSET_ROUNDING(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Mode = DAG.getZExtOrTrunc(Op.getOperand(1), DL, MVT::i64);
  Mode = DAG.getNode(ISD::AND, DL, MVT::i64, Mode,
                     DAG.getConstant(3, DL, MVT::i64));
  Mode = DAG.getNode(ISD::SHL, DL, MVT::i64, Mode,
                     DAG.getConstant(1, DL, MVT::i64));
  SDValue RC = DAG.getNode(ISD::SRL, DL, MVT::i64,
                           DAG.getConstant(0x63, DL, MVT::i64), Mode);
  RC =
      DAG.getNode(ISD::AND, DL, MVT::i64, RC, DAG.getConstant(3, DL, MVT::i64));
  RC = DAG.getNode(ISD::SHL, DL, MVT::i64, RC,
                   DAG.getConstant(RoundingControlShift, DL, MVT::i64));
  SDValue FPSR(
      DAG.getMachineNode(IA64::GET_FPSR, DL, MVT::i64, MVT::Other, Chain), 0);
  SDValue Rest = DAG.getNode(
      ISD::AND, DL, MVT::i64, FPSR,
      DAG.getConstant(~(uint64_t(3) << RoundingControlShift), DL, MVT::i64));
  SDValue New = DAG.getNode(ISD::OR, DL, MVT::i64, Rest, RC);
  return SDValue(
      DAG.getMachineNode(IA64::SET_FPSR, DL, MVT::Other, New, FPSR.getValue(1)),
      0);
}

SDValue IA64TargetLowering::LowerRETURNADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MF.getFrameInfo().setReturnAddressIsTaken(true);
  SDLoc DL(Op);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, MVT::i64);
  Register Reg = MF.addLiveIn(IA64::B0, &IA64::GRRegClass);
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, MVT::i64);
}

SDValue IA64TargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                    SelectionDAG &DAG) const {
  switch (Op.getConstantOperandVal(0)) {
  default:
    return SDValue();
  case Intrinsic::thread_pointer:
    return DAG.getCopyFromReg(DAG.getEntryNode(), SDLoc(Op), IA64::R13,
                              MVT::i64);
  }
}

// glibc keeps the stack guard in the doubleword below the thread pointer and
// exports no __stack_chk_guard.
Value *
IA64TargetLowering::getIRStackGuard(IRBuilderBase &IRB,
                                    const LibcallLoweringInfo &Libcalls) const {
  if (!getTargetMachine().getTargetTriple().isOSLinux())
    return TargetLowering::getIRStackGuard(IRB, Libcalls);
  Module *M = IRB.GetInsertBlock()->getModule();
  Function *ThreadPointer = Intrinsic::getOrInsertDeclaration(
      M, Intrinsic::thread_pointer, IRB.getPtrTy());
  return IRB.CreateConstGEP1_32(IRB.getInt8Ty(), IRB.CreateCall(ThreadPointer),
                                -8);
}

// The frame address is SP on entry, where the caller's frame ends.
SDValue IA64TargetLowering::LowerFRAMEADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, MVT::i64);
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);
  return DAG.getNode(IA64ISD::FRAMEADDR, DL, MVT::i64);
}

// SP on entry is the frame address.
SDValue IA64TargetLowering::LowerSPONENTRY(SDValue Op,
                                           SelectionDAG &DAG) const {
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);
  return DAG.getNode(IA64ISD::FRAMEADDR, SDLoc(Op), MVT::i64);
}

// The CFA is the start of the stack arguments, past the 16-byte scratch area
// at the caller's SP. This matches GCC's __builtin_dwarf_cfa.
SDValue IA64TargetLowering::LowerEH_DWARF_CFA(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);
  SDValue Offset =
      DAG.getNode(ISD::ADD, DL, MVT::i64,
                  DAG.getSExtOrTrunc(Op.getOperand(0), DL, MVT::i64),
                  DAG.getConstant(16, DL, MVT::i64));
  return DAG.getNode(ISD::ADD, DL, MVT::i64,
                     DAG.getNode(IA64ISD::FRAMEADDR, DL, MVT::i64), Offset);
}

// A trampoline is four doublewords. The first two are a function descriptor
// whose entry point is libgcc's __ia64_trampoline and whose GP is the address
// of the other two: the target's function descriptor address and the static
// chain. __ia64_trampoline loads them via GP, puts the chain in r15 and
// branches to the function.
SDValue IA64TargetLowering::LowerINIT_TRAMPOLINE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Tramp = Op.getOperand(1);
  const Value *TrampV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();

  // This is the code address, not a function descriptor.
  SDValue Entry = loadLinkageTableEntry(
      DAG.getTargetExternalSymbol("__ia64_trampoline", MVT::i64, IA64::S_LTOFF),
      DL, DAG);
  SDValue Words[] = {
      Entry,
      DAG.getMemBasePlusOffset(Tramp, TypeSize::getFixed(16), DL),
      Op.getOperand(2),
      Op.getOperand(3),
  };
  SDValue Stores[4];
  for (unsigned I = 0; I != 4; ++I)
    Stores[I] = DAG.getStore(
        Chain, DL, Words[I],
        DAG.getMemBasePlusOffset(Tramp, TypeSize::getFixed(8 * I), DL),
        MachinePointerInfo(TrampV, 8 * I), Align(8));
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

// SP moves down by the size, which is already a multiple of 16. The outgoing
// argument area stays at SP, so the allocation starts above it and is
// aligned from there.
SDValue IA64TargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  uint64_t Alignment = Op.getConstantOperandVal(2);

  if (Alignment > 16)
    Size = DAG.getNode(ISD::ADD, DL, MVT::i64, Size,
                       DAG.getConstant(Alignment - 16, DL, MVT::i64));
  SDValue SP = DAG.getCopyFromReg(Chain, DL, IA64::R12, MVT::i64);
  Chain = DAG.getCopyToReg(SP.getValue(1), DL, IA64::R12,
                           DAG.getNode(ISD::SUB, DL, MVT::i64, SP, Size));
  SDValue Addr = DAG.getNode(IA64ISD::ADJDYNALLOC, DL,
                             DAG.getVTList(MVT::i64, MVT::Other), Chain);
  Chain = Addr.getValue(1);
  if (Alignment > 16)
    Addr =
        DAG.getNode(ISD::AND, DL, MVT::i64,
                    DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                                DAG.getConstant(Alignment - 1, DL, MVT::i64)),
                    DAG.getSignedConstant(-int64_t(Alignment), DL, MVT::i64));
  return DAG.getMergeValues({Addr, Chain}, DL);
}

// The offset from SP to the latest dynamic allocation, i.e. the size of the
// outgoing argument area, which is known once all calls are lowered.
SDValue
IA64TargetLowering::LowerGET_DYNAMIC_AREA_OFFSET(SDValue Op,
                                                 SelectionDAG &DAG) const {
  return DAG.getNode(IA64ISD::DYNAREAOFFSET, SDLoc(Op), MVT::i64);
}

// fclass takes a 9-bit class mask: three NaN kinds, and four classes that
// match only together with the selected sign bits. A class set that differs
// between signs therefore needs one test per sign, and the complement set is
// used where it needs fewer tests. A denormal is unnormalized in a register,
// whatever its precision.
void IA64TargetLowering::getClassTests(unsigned Mask,
                                       SmallVectorImpl<unsigned> &Tests) {
  enum {
    Pos = 1,
    Neg = 2,
    Zero = 4,
    Unorm = 8,
    Norm = 16,
    Inf = 32,
    SNaN = 64,
    QNaN = 128
  };
  unsigned NaNs = (Mask & fcSNan ? SNaN : 0) | (Mask & fcQNan ? QNaN : 0);
  unsigned OfPos =
      (Mask & fcPosZero ? Zero : 0) | (Mask & fcPosSubnormal ? Unorm : 0) |
      (Mask & fcPosNormal ? Norm : 0) | (Mask & fcPosInf ? Inf : 0);
  unsigned OfNeg =
      (Mask & fcNegZero ? Zero : 0) | (Mask & fcNegSubnormal ? Unorm : 0) |
      (Mask & fcNegNormal ? Norm : 0) | (Mask & fcNegInf ? Inf : 0);
  if (OfPos == OfNeg) {
    Tests.push_back(NaNs | (OfPos ? Pos | Neg | OfPos : 0));
    return;
  }
  if (OfPos)
    Tests.push_back(NaNs | Pos | OfPos);
  if (OfNeg)
    Tests.push_back((OfPos ? 0 : NaNs) | Neg | OfNeg);
}

// Expanding a doubleword with byte or halfword alignment reads every byte or
// halfword of it. Instead, read the aligned doublewords holding its first and
// last bytes, which are the same doubleword when it is aligned, and shift the
// bytes together. Neither read can leave the pages holding the doubleword.
SDValue IA64TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  auto *LD = cast<LoadSDNode>(Op);
  const DataLayout &Layout = DAG.getDataLayout();
  if (allowsMemoryAccessForAlignment(*DAG.getContext(), Layout,
                                     LD->getMemoryVT(), *LD->getMemOperand()))
    return SDValue();
  if (LD->getAlign() >= Align(4) || LD->isVolatile() || !LD->isUnindexed() ||
      !Layout.isLittleEndian()) {
    auto [Val, Chain] = expandUnalignedLoad(LD, DAG);
    return DAG.getMergeValues({Val, Chain}, SDLoc(Op));
  }

  SDLoc DL(Op);
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  auto Word = [&](SDValue Addr) {
    Addr = DAG.getNode(ISD::AND, DL, MVT::i64, Addr,
                       DAG.getSignedConstant(-8, DL, MVT::i64));
    return DAG.getLoad(MVT::i64, DL, Chain, Addr,
                       MachinePointerInfo(LD->getAddressSpace()), Align(8),
                       LD->getMemOperand()->getFlags() &
                           ~MachineMemOperand::MODereferenceable,
                       LD->getAAInfo());
  };
  SDValue Lo = Word(Ptr);
  SDValue Hi = Word(DAG.getNode(ISD::ADD, DL, MVT::i64, Ptr,
                                DAG.getConstant(7, DL, MVT::i64)));
  SDValue Shift = DAG.getNode(ISD::SHL, DL, MVT::i64,
                              DAG.getNode(ISD::AND, DL, MVT::i64, Ptr,
                                          DAG.getConstant(7, DL, MVT::i64)),
                              DAG.getConstant(3, DL, MVT::i64));
  SDValue Val = DAG.getNode(
      ISD::OR, DL, MVT::i64, DAG.getNode(ISD::SRL, DL, MVT::i64, Lo, Shift),
      DAG.getNode(IA64ISD::SHL_WIDE, DL, MVT::i64, Hi,
                  DAG.getNode(ISD::SUB, DL, MVT::i64,
                              DAG.getConstant(64, DL, MVT::i64), Shift)));
  SDValue Chains = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo.getValue(1),
                               Hi.getValue(1));
  return DAG.getMergeValues({Val, Chains}, DL);
}

// Leave the copies IA64SelectionDAGInfo expands to it.
bool IA64TargetLowering::findOptimalMemOpLowering(
    LLVMContext &Context, std::vector<EVT> &MemOps, unsigned Limit,
    const MemOp &Op, unsigned DstAS, unsigned SrcAS,
    const AttributeList &FuncAttributes, EVT *LargestVT) const {
  if (Limit != ~0U && Op.isMemcpyOrMemmove() && !Op.isMemcpyStrSrc() &&
      Op.isFixedDstAlign() && !Op.isVolatile() &&
      IA64SelectionDAGInfo::expandsCopy(Op.size(), Op.getDstAlign(),
                                        Op.getSrcAlign()))
    return false;
  return TargetLowering::findOptimalMemOpLowering(
      Context, MemOps, Limit, Op, DstAS, SrcAS, FuncAttributes, LargestVT);
}

SDValue IA64TargetLowering::LowerIS_FPCLASS(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  unsigned Mask = Op.getConstantOperandVal(1) & fcAllFlags;
  if (Mask == 0 || Mask == fcAllFlags)
    return DAG.getConstant(Mask != 0, DL, VT);

  SmallVector<unsigned, 2> Tests, Others;
  getClassTests(Mask, Tests);
  getClassTests(~Mask & fcAllFlags, Others);
  // Testing the complement: the value must fail every test, so AND them.
  bool None = Others.size() < Tests.size();
  SDValue Res;
  for (unsigned Test : None ? Others : Tests) {
    SDValue Is(DAG.getMachineNode(IA64::SETFCLASS, DL, MVT::i64,
                                  Op.getOperand(0),
                                  DAG.getTargetConstant(Test, DL, MVT::i64),
                                  DAG.getTargetConstant(None, DL, MVT::i64)),
               0);
    Res = Res ? DAG.getNode(None ? ISD::AND : ISD::OR, DL, MVT::i64, Res, Is)
              : Is;
  }
  return DAG.getZExtOrTrunc(Res, DL, VT);
}

// Jump table entries are table-relative offsets, which need no dynamic
// relocation.
// A symbol offset is folded into the relocation where the address is formed,
// and added in code where the address is loaded from the GOT.
bool IA64TargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  return true;
}

// The post-increment is a 9-bit immediate, or for loads a register.
bool IA64TargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                    SDValue &Base,
                                                    SDValue &Offset,
                                                    ISD::MemIndexedMode &AM,
                                                    SelectionDAG &DAG) const {
  if (Op->getOpcode() != ISD::ADD)
    return false;
  SDValue Ptr = cast<MemSDNode>(N)->getBasePtr();
  Base = Op->getOperand(0);
  Offset = Op->getOperand(1);
  if (Offset == Ptr)
    std::swap(Base, Offset);
  if (Base != Ptr)
    return false;
  if (const auto *C = dyn_cast<ConstantSDNode>(Offset)) {
    if (!isInt<9>(C->getSExtValue()))
      return false;
  } else if (!isa<LoadSDNode>(N)) {
    return false;
  }
  // A store that steps the address of a load puts the load of the next
  // iteration after the computation of the stored value, so that iterations
  // cannot overlap. Leave the increment to an add instead.
  if (isa<StoreSDNode>(N) && any_of(Ptr->users(), [&](const SDNode *U) {
        const auto *L = dyn_cast<LoadSDNode>(U);
        return L && L->getBasePtr() == Ptr;
      }))
    return false;
  AM = ISD::POST_INC;
  return true;
}

// pmpy2.r reads halfwords 0 and 2 of its operands, and its low word depends
// only on halfword 0.
bool IA64TargetLowering::SimplifyDemandedBitsForTargetNode(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    KnownBits &Known, TargetLoweringOpt &TLO, unsigned Depth) const {
  if (Op.getOpcode() != IA64ISD::PMPY2_R)
    return TargetLowering::SimplifyDemandedBitsForTargetNode(
        Op, DemandedBits, DemandedElts, Known, TLO, Depth);
  APInt Halves(64,
               DemandedBits.getActiveBits() <= 32 ? 0xffff : 0xffff0000ffffULL);
  SDValue Ops[2];
  for (unsigned I = 0; I != 2; ++I) {
    KnownBits K;
    if (SimplifyDemandedBits(Op.getOperand(I), Halves, DemandedElts, K, TLO,
                             Depth + 1))
      return true;
    Ops[I] = SimplifyMultipleUseDemandedBits(Op.getOperand(I), Halves,
                                             DemandedElts, TLO.DAG, Depth + 1);
  }
  Known = KnownBits(64);
  if (!Ops[0] && !Ops[1])
    return false;
  return TLO.CombineTo(Op,
                       TLO.DAG.getNode(IA64ISD::PMPY2_R, SDLoc(Op), MVT::i64,
                                       Ops[0] ? Ops[0] : Op.getOperand(0),
                                       Ops[1] ? Ops[1] : Op.getOperand(1)));
}

// A control-speculative load zero-extends what it reads, as ld does. One
// that defers a fault leaves zero beneath the NaT.
void IA64TargetLowering::computeKnownBitsForTargetNode(
    const SDValue Op, KnownBits &Known, const APInt &DemandedElts,
    const SelectionDAG &DAG, unsigned Depth) const {
  Known.resetAll();
  if (Op.getOpcode() != ISD::INTRINSIC_W_CHAIN || Op.getResNo() != 0)
    return;
  unsigned Bits;
  switch (Op.getConstantOperandVal(1)) {
  case Intrinsic::ia64_ld1_s:
    Bits = 8;
    break;
  case Intrinsic::ia64_ld2_s:
    Bits = 16;
    break;
  case Intrinsic::ia64_ld4_s:
    Bits = 32;
    break;
  default:
    return;
  }
  Known.Zero.setBitsFrom(Bits);
}

// A control-speculative load reads memory like any other, so the scheduler
// can order it against stores rather than everything else.
void IA64TargetLowering::getTgtMemIntrinsic(
    SmallVectorImpl<IntrinsicInfo> &Infos, const CallBase &I,
    MachineFunction &MF, unsigned Intrinsic) const {
  unsigned Size;
  switch (Intrinsic) {
  case Intrinsic::ia64_ld1_s:
    Size = 1;
    break;
  case Intrinsic::ia64_ld2_s:
    Size = 2;
    break;
  case Intrinsic::ia64_ld4_s:
    Size = 4;
    break;
  case Intrinsic::ia64_ld8_s:
    Size = 8;
    break;
  default:
    return;
  }
  IntrinsicInfo Info;
  Info.opc = ISD::INTRINSIC_W_CHAIN;
  Info.memVT = MVT::getIntegerVT(Size * 8);
  Info.ptrVal = I.getArgOperand(0);
  Info.offset = 0;
  Info.align = Align(Size);
  Info.flags = MachineMemOperand::MOLoad;
  Infos.push_back(Info);
}

bool IA64TargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  if (const auto *Load = dyn_cast<LoadSDNode>(Val)) {
    EVT MemVT = Load->getMemoryVT();
    if (MemVT.isScalarInteger() && MemVT.getSizeInBits() <= 32 &&
        (Load->getExtensionType() == ISD::NON_EXTLOAD ||
         Load->getExtensionType() == ISD::ZEXTLOAD))
      return true;
  }
  return TargetLowering::isZExtFree(Val, VT2);
}

ISD::NodeType
IA64TargetLowering::getPreferredExtendForValue(const Instruction *I) const {
  if (isa<LoadInst>(I))
    return ISD::ZERO_EXTEND;
  return TargetLowering::getPreferredExtendForValue(I);
}

MVT IA64TargetLowering::getPreferredSwitchConditionType(LLVMContext &Context,
                                                        EVT ConditionVT) const {
  if (ConditionVT == MVT::i32)
    return MVT::i32;
  return TargetLowering::getPreferredSwitchConditionType(Context, ConditionVT);
}

unsigned IA64TargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_LabelDifference64;
}

Register
IA64TargetLowering::getExceptionPointerRegister(ExceptionHandling,
                                                const Constant *) const {
  return IA64::R15;
}

Register
IA64TargetLowering::getExceptionSelectorRegister(ExceptionHandling,
                                                 const Constant *) const {
  return IA64::R16;
}

/// getf.sig and setf.sig: move \p Val between the significand of an FR and
/// a GR.
static SDValue getSignificand(SelectionDAG &DAG, const SDLoc &DL, SDValue Val) {
  return SDValue(DAG.getMachineNode(IA64::GETF_SIG, DL, MVT::i64, Val,
                                    DAG.getRegister(IA64::P0, MVT::i1)),
                 0);
}

static SDValue setSignificand(SelectionDAG &DAG, const SDLoc &DL, SDValue Val) {
  return SDValue(DAG.getMachineNode(IA64::SETF_SIG, DL, MVT::f64, Val,
                                    DAG.getRegister(IA64::P0, MVT::i1)),
                 0);
}

// <2 x float> is passed and returned as a single i64, in whichever register
// the convention assigns.
MVT IA64TargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                      CallingConv::ID CC,
                                                      EVT VT) const {
  if (VT == MVT::v2f32)
    return MVT::i64;
  return TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);
}

unsigned IA64TargetLowering::getNumRegistersForCallingConv(LLVMContext &Context,
                                                           CallingConv::ID CC,
                                                           EVT VT) const {
  if (VT == MVT::v2f32)
    return 1;
  return TargetLowering::getNumRegistersForCallingConv(Context, CC, VT);
}

SDValue IA64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_IA64);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    ISD::ArgFlagsTy Flags = Ins[VA.getValNo()].Flags;
    if (Flags.isByVal()) {
      // The function gets the argument's address, so the bytes must be
      // contiguous. Bytes passed in registers are stored just below the
      // stack-passed part, which starts at the caller's frame, or to a local
      // stack object if there is no stack-passed part.
      unsigned NumRegs = 0;
      while (I + NumRegs != E && ArgLocs[I + NumRegs].isRegLoc() &&
             ArgLocs[I + NumRegs].getValNo() == VA.getValNo())
        ++NumRegs;
      bool HasMem =
          I + NumRegs != E && ArgLocs[I + NumRegs].getValNo() == VA.getValNo();
      // The function accesses it through the address, so the object is not
      // immutable.
      unsigned Size = std::max(Flags.getByValSize(), 1u);
      int FI;
      if (NumRegs == 0)
        FI = MFI.CreateFixedObject(Size, VA.getLocMemOffset(), false, true);
      else if (HasMem)
        FI = MFI.CreateFixedObject(Size, -8 * int64_t(NumRegs), false, true);
      else
        FI = MFI.CreateStackObject(
            alignTo(Size, 8), std::max(Flags.getNonZeroByValAlign(), Align(8)),
            false);
      SDValue Addr = DAG.getFrameIndex(FI, MVT::i64);
      SmallVector<SDValue, 8> Stores;
      for (unsigned Slot = 0; Slot != NumRegs; ++Slot) {
        Register VReg =
            MF.addLiveIn(ArgLocs[I + Slot].getLocReg(), &IA64::GRRegClass);
        Stores.push_back(DAG.getStore(
            Chain, DL, DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64),
            DAG.getMemBasePlusOffset(Addr, TypeSize::getFixed(8 * Slot), DL),
            MachinePointerInfo::getFixedStack(MF, FI, 8 * Slot)));
      }
      if (!Stores.empty())
        Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
      InVals.push_back(Addr);
      I += NumRegs + HasMem - 1;
      continue;
    }
    if (isSignificandLoc(VA)) {
      Register VReg = MF.addLiveIn(VA.getLocReg(), &IA64::FRRegClass);
      InVals.push_back(getSignificand(
          DAG, DL, DAG.getCopyFromReg(Chain, DL, VReg, MVT::f64)));
      continue;
    }
    if (VA.needsCustom()) {
      // A varargs function reads a named argument from the FP register and
      // ignores the copy in the slot.
      if (I != 0 && ArgLocs[I - 1].getValNo() == VA.getValNo())
        continue;
      // No FP register was left, so the value is raw bits in the slot.
      auto Slot = [&](const CCValAssign &VA) {
        if (VA.isMemLoc()) {
          int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), true);
          return DAG.getLoad(MVT::i64, DL, Chain,
                             DAG.getFrameIndex(FI, MVT::i64),
                             MachinePointerInfo::getFixedStack(MF, FI));
        }
        Register VReg = MF.addLiveIn(VA.getLocReg(), &IA64::GRRegClass);
        return DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64);
      };
      SDValue Bits = Slot(VA);
      if (VA.getValVT() == MVT::f80) {
        SDValue Temp =
            DAG.CreateStackTemporary(TypeSize::getFixed(16), Align(16));
        int FI = cast<FrameIndexSDNode>(Temp)->getIndex();
        SDValue Lo = DAG.getStore(Chain, DL, Bits, Temp,
                                  MachinePointerInfo::getFixedStack(MF, FI));
        SDValue Hi = DAG.getStore(
            Chain, DL, Slot(ArgLocs[I + 1]),
            DAG.getMemBasePlusOffset(Temp, TypeSize::getFixed(8), DL),
            MachinePointerInfo::getFixedStack(MF, FI, 8));
        InVals.push_back(DAG.getLoad(
            MVT::f80, DL, DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi),
            Temp, MachinePointerInfo::getFixedStack(MF, FI)));
      } else if (VA.getValVT() == MVT::f32) {
        if (VA.getLocInfo() == CCValAssign::AExtUpper)
          Bits = DAG.getNode(ISD::SRL, DL, MVT::i64, Bits,
                             DAG.getConstant(32, DL, MVT::i64));
        InVals.push_back(DAG.getBitcast(
            MVT::f32, DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Bits)));
      } else {
        InVals.push_back(DAG.getBitcast(MVT::f64, Bits));
      }
      continue;
    }
    if (VA.isMemLoc()) {
      // Offsets count from the end of the scratch area, where the first
      // stack argument is.
      int FI = MFI.CreateFixedObject(VA.getLocVT() == MVT::f80 ? 16 : 8,
                                     VA.getLocMemOffset(), true);
      InVals.push_back(DAG.getLoad(
          VA.getLocVT(), DL, Chain, DAG.getFrameIndex(FI, MVT::i64),
          MachinePointerInfo::getFixedStack(MF, FI), MFI.getObjectAlign(FI)));
      continue;
    }
    // The ABI leaves the bits above a value narrower than its slot undefined,
    // so emit no AssertZext/AssertSext whatever the zeroext/signext
    // attributes say: a caller built by another compiler may not extend.
    Register VReg = MF.addLiveIn(VA.getLocReg(), VA.getLocVT().isFloatingPoint()
                                                     ? &IA64::FRRegClass
                                                     : &IA64::GRRegClass);
    InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT()));
  }

  // The function returns the sret pointer in r8, so keep it.
  for (const CCValAssign &VA : ArgLocs) {
    if (!Ins[VA.getValNo()].Flags.isSRet())
      continue;
    Register SRetReg = MF.getRegInfo().createVirtualRegister(&IA64::GRRegClass);
    MF.getInfo<IA64MachineFunctionInfo>()->setSRetReg(SRetReg);
    Chain = DAG.getCopyToReg(Chain, DL, SRetReg, InVals[VA.getValNo()]);
    break;
  }
  if (!IsVarArg)
    return Chain;

  // va_arg reads unnamed arguments from memory slot by slot, so the
  // register-passed ones are stored just below the stack-passed ones. The
  // last two fit in the scratch area and the rest go at the top of the
  // frame. An argument register the caller left unset can have its NaT bit
  // set, which a plain store faults on, so use st8.spill.
  unsigned First = CCInfo.getFirstUnallocated(Slots);
  auto SlotOffset = [](unsigned Slot) { return -8 * int64_t(8 - Slot); };
  SmallVector<SDValue, 8> Stores;
  // The slots are a single mutable object: va_arg walks from slot to slot,
  // and a load folded to an offset from the first slot must alias the spill
  // of the slot it reads.
  int VarArgsFI =
      First != 8
          ? MFI.CreateFixedObject(8 * (8 - First), SlotOffset(First), false)
          : MFI.CreateFixedObject(8, CCInfo.getStackSize(), true);
  for (unsigned Slot = First; Slot != 8; ++Slot) {
    int64_t Offset = 8 * int64_t(Slot - First);
    Register VReg = MF.addLiveIn(Slots[Slot], &IA64::GRRegClass);
    MachineSDNode *Spill = DAG.getMachineNode(
        IA64::ST8_SPILL, DL, MVT::Other,
        {DAG.getMemBasePlusOffset(DAG.getFrameIndex(VarArgsFI, MVT::i64),
                                  TypeSize::getFixed(Offset), DL),
         DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64),
         DAG.getTargetConstant(0, DL, MVT::i64),
         DAG.getRegister(IA64::P0, MVT::i1), Chain});
    DAG.setNodeMemRefs(
        Spill, {MF.getMachineMemOperand(
                   MachinePointerInfo::getFixedStack(MF, VarArgsFI, Offset),
                   MachineMemOperand::MOStore, LLT::scalar(64), Align(8))});
    Stores.push_back(SDValue(Spill, 0));
  }
  if (!Stores.empty()) {
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
    MF.getInfo<IA64MachineFunctionInfo>()->setSpillsVarArgs(true);
  }
  MF.getInfo<IA64MachineFunctionInfo>()->setVarArgsFrameIndex(VarArgsFI);
  return Chain;
}

// Arguments go in the output registers, which the callee sees as its input
// registers, and the 16-byte scratch area at SP belongs to the callee.
/// Whether the callee returns twice. A libcall has no CallBase and calls
/// whatever function of that name the module has.
static bool returnsTwice(const SelectionDAG &DAG, SDValue Callee,
                         const CallBase *CB) {
  if (CB)
    return CB->hasFnAttr(Attribute::ReturnsTwice);
  const Function *F = nullptr;
  if (const auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    F = dyn_cast<Function>(G->getGlobal());
  else if (const auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    F = DAG.getMachineFunction().getFunction().getParent()->getFunction(
        E->getSymbol());
  return F && F->hasFnAttribute(Attribute::ReturnsTwice);
}

/// Whether a call between functions of conventions \p A and \p B passes
/// arguments and preserves registers alike. The C, fast and cold conventions
/// all lower to the software conventions, and the optimizer gives an
/// internal function the fast one, so its callers in C may tail call it.
static bool sameConventions(CallingConv::ID A, CallingConv::ID B) {
  auto IsC = [](CallingConv::ID CC) {
    return CC == CallingConv::C || CC == CallingConv::Fast ||
           CC == CallingConv::Cold;
  };
  return A == B || (IsC(A) && IsC(B));
}

SDValue IA64TargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  const SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const IA64RegisterInfo *TRI = Subtarget.getRegisterInfo();

  // A function pointer is the address of its function descriptor: the entry
  // point followed by the function's GP.
  // Calling the address of a variable treats the variable's contents as the
  // descriptor.
  const auto *Direct = dyn_cast<GlobalAddressSDNode>(Callee);
  if (Direct && (Direct->getOffset() != 0 ||
                 !Direct->getGlobal()->getValueType()->isFunctionTy()))
    Direct = nullptr;
  bool Indirect = !Direct && !isa<ExternalSymbolSDNode>(Callee);
  SDValue Entry, CalleeGP;
  if (Indirect) {
    Entry = DAG.getLoad(MVT::i64, DL, Chain, Callee, MachinePointerInfo());
    CalleeGP =
        DAG.getLoad(MVT::i64, DL, Chain,
                    DAG.getMemBasePlusOffset(Callee, TypeSize::getFixed(8), DL),
                    MachinePointerInfo());
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Entry.getValue(1),
                        CalleeGP.getValue(1));
  }

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_IA64);

  // A tail call requires the callee to see the same GP, which holds only for
  // a DSO-local callee, and no stack arguments, so all arguments must be in
  // registers. An indirect tail call sets GP from the descriptor, so the
  // callee sees its own, but returns with it to a caller that may expect
  // this function's: that is right only with a constant GP, or when the
  // call must be a tail call anyway.
  if (CLI.IsTailCall) {
    const auto *G = Direct;
    const Function &Caller = MF.getFunction();
    bool IndirectOK = Indirect && ((CLI.CB && CLI.CB->isMustTailCall()) ||
                                   Subtarget.hasConstantGP());
    CLI.IsTailCall =
        ((G && getTargetMachine().shouldAssumeDSOLocal(G->getGlobal())) ||
         IndirectOK) &&
        CCInfo.getStackSize() == 0 && !Caller.isVarArg() &&
        !Caller.hasFnAttribute("syscall-linkage") &&
        sameConventions(Caller.getCallingConv(), CLI.CallConv) &&
        none_of(CLI.Outs,
                [](const ISD::OutputArg &Out) { return Out.Flags.isByVal(); });
  }
  if (!CLI.IsTailCall && CLI.CB && CLI.CB->isMustTailCall())
    reportFatalUsageError("failed to perform tail call elimination on a call "
                          "site marked musttail");

  // An unnamed FP argument is passed as raw bits; an f32 occupies the low
  // half of the slot. An f80 occupies two slots and is reloaded from a stack
  // temporary. In a varargs call, a possibly-named argument also gets a
  // raw-bits copy.
  SmallVector<SDValue, 8> OutVals;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Val = CLI.OutVals[VA.getValNo()];
    // A by-value aggregate is loaded slot by slot into registers, and the
    // stack-passed part is copied with memcpy. This happens before
    // CALLSEQ_START, since the memcpy may itself be a call, and SP does not
    // move in between.
    ISD::ArgFlagsTy Flags = CLI.Outs[VA.getValNo()].Flags;
    if (Flags.isByVal()) {
      unsigned Size = Flags.getByValSize();
      Align Alignment = Flags.getNonZeroByValAlign();
      SmallVector<SDValue, 8> Chains;
      unsigned Offset = 0;
      for (; I != E && ArgLocs[I].getValNo() == VA.getValNo() &&
             ArgLocs[I].isRegLoc();
           ++I, Offset += 8) {
        // The last slot may hold fewer than 8 bytes; do not read past the
        // end of the argument.
        SDValue Slot;
        unsigned Bytes = std::min(Size - Offset, 8u);
        for (unsigned Width : {8, 4, 2, 1}) {
          if (Bytes < Width)
            continue;
          unsigned At = Offset + std::min(Size - Offset, 8u) - Bytes;
          SDValue Piece = DAG.getExtLoad(
              ISD::ZEXTLOAD, DL, MVT::i64, Chain,
              DAG.getMemBasePlusOffset(Val, TypeSize::getFixed(At), DL),
              MachinePointerInfo(), MVT::getIntegerVT(8 * Width),
              commonAlignment(Alignment, At));
          Chains.push_back(Piece.getValue(1));
          if (At != Offset)
            Piece =
                DAG.getNode(ISD::SHL, DL, MVT::i64, Piece,
                            DAG.getConstant(8 * (At - Offset), DL, MVT::i64));
          Slot = Slot ? DAG.getNode(ISD::OR, DL, MVT::i64, Slot, Piece) : Piece;
          Bytes -= Width;
        }
        OutVals.push_back(Slot);
      }
      if (!Chains.empty()) {
        Chains.push_back(Chain);
        Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
      }
      if (I != E && ArgLocs[I].getValNo() == VA.getValNo()) {
        unsigned To =
            IA64FrameLowering::ScratchAreaSize + ArgLocs[I].getLocMemOffset();
        if (Size != Offset)
          Chain = DAG.getMemcpy(
              Chain, DL,
              DAG.getMemBasePlusOffset(
                  DAG.getCopyFromReg(Chain, DL, IA64::R12, MVT::i64),
                  TypeSize::getFixed(To), DL),
              DAG.getMemBasePlusOffset(Val, TypeSize::getFixed(Offset), DL),
              DAG.getConstant(Size - Offset, DL, MVT::i64), Align(8),
              commonAlignment(Alignment, Offset), /*isVol=*/false,
              /*AlwaysInline=*/false, /*CI=*/nullptr, std::nullopt,
              MachinePointerInfo::getStack(MF, To), MachinePointerInfo());
        OutVals.push_back(SDValue());
        ++I;
      }
      --I;
      continue;
    }
    if (VA.needsCustom() && Val.getValueType() == MVT::f80) {
      SDValue Slot =
          DAG.CreateStackTemporary(TypeSize::getFixed(16), Align(16));
      int FI = cast<FrameIndexSDNode>(Slot)->getIndex();
      SDValue Store = DAG.getStore(Chain, DL, Val, Slot,
                                   MachinePointerInfo::getFixedStack(MF, FI));
      SDValue Lo = DAG.getLoad(MVT::i64, DL, Store, Slot,
                               MachinePointerInfo::getFixedStack(MF, FI));
      SDValue Hi =
          DAG.getLoad(MVT::i64, DL, Store,
                      DAG.getMemBasePlusOffset(Slot, TypeSize::getFixed(8), DL),
                      MachinePointerInfo::getFixedStack(MF, FI, 8));
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain,
                          Lo.getValue(1), Hi.getValue(1));
      OutVals.append({Lo, Hi});
      ++I;
      continue;
    }
    if (isSignificandLoc(VA)) {
      OutVals.push_back(setSignificand(DAG, DL, Val));
      continue;
    }
    if (VA.getLocInfo() == CCValAssign::BCvt) {
      if (Val.getValueType() == MVT::f32)
        Val = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64,
                          DAG.getBitcast(MVT::i32, Val));
      else
        Val = DAG.getBitcast(MVT::i64, Val);
    } else if (VA.getLocInfo() == CCValAssign::AExtUpper) {
      Val = DAG.getNode(ISD::SHL, DL, MVT::i64,
                        DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64,
                                    DAG.getBitcast(MVT::i32, Val)),
                        DAG.getConstant(32, DL, MVT::i64));
    }
    OutVals.push_back(Val);
  }

  unsigned NumBytes =
      IA64FrameLowering::ScratchAreaSize + CCInfo.getStackSize();
  if (!CLI.IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // Store stack arguments first, so the copies to the output registers are
  // adjacent to the call.
  SmallVector<SDValue, 8> MemOps;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (!VA.isMemLoc() || !OutVals[I])
      continue;
    if (!StackPtr)
      StackPtr = DAG.getCopyFromReg(Chain, DL, IA64::R12, MVT::i64);
    unsigned Offset = IA64FrameLowering::ScratchAreaSize + VA.getLocMemOffset();
    MemOps.push_back(DAG.getStore(
        Chain, DL, OutVals[I],
        DAG.getMemBasePlusOffset(StackPtr, TypeSize::getFixed(Offset), DL),
        MachinePointerInfo::getStack(MF, Offset),
        commonAlignment(Align(16), Offset)));
  }
  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);

  // Two float HFA members may share one slot; combine the halves.
  SmallDenseMap<Register, unsigned, 16> LowHalf;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    if (!ArgLocs[I].isRegLoc())
      continue;
    auto [It, New] = LowHalf.try_emplace(ArgLocs[I].getLocReg(), I);
    if (New)
      continue;
    OutVals[I] = DAG.getNode(
        ISD::OR, DL, MVT::i64,
        DAG.getZeroExtendInReg(OutVals[It->second], DL, MVT::i32), OutVals[I]);
    OutVals[It->second] = SDValue();
  }

  SDValue Glue;
  unsigned TailInputs = 0;
  SmallVector<std::pair<Register, MVT>, 8> ArgRegs;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (!VA.isRegLoc() || !OutVals[I])
      continue;
    // The calling convention names the callee's input registers (r32-r39);
    // the caller writes the corresponding output registers instead, except
    // for a tail call, where the callee reuses this function's frame.
    Register Reg = VA.getLocReg();
    if (Reg >= IA64::R32 && Reg <= IA64::R39) {
      TailInputs = std::max(TailInputs, Reg - IA64::R32 + 1);
      if (!CLI.IsTailCall)
        Reg = IA64::R120 + (Reg - IA64::R32);
    }
    Chain = DAG.getCopyToReg(Chain, DL, Reg, OutVals[I], Glue);
    Glue = Chain.getValue(1);
    ArgRegs.push_back({Reg, VA.getLocVT()});
  }

  // A DSO-local callee shares this function's GP. Any other may return with
  // its own GP, unless the program has a constant GP and the call is direct.
  bool SameModule = false;
  if (Direct) {
    SameModule = Subtarget.hasConstantGP() ||
                 getTargetMachine().shouldAssumeDSOLocal(Direct->getGlobal());
    Callee = DAG.getTargetGlobalAddress(Direct->getGlobal(), DL, MVT::i64);
  } else if (const auto *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    SameModule = Subtarget.hasConstantGP();
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), MVT::i64);
  }

  if (CLI.IsTailCall) {
    SmallVector<SDValue, 12> Ops = {Chain};
    if (Indirect)
      Ops.append({Entry, CalleeGP});
    else
      Ops.push_back(Callee);
    // A preserve_none callee is code of ours, which does not count on
    // having been given a frame of nothing but its inputs, as a callee
    // that goes on to call without an alloc of its own would.
    if (CLI.CallConv == CallingConv::PreserveNone &&
        MF.getFunction().getCallingConv() == CallingConv::PreserveNone)
      TailInputs |= IA64II::TailKeepsFrame;
    Ops.push_back(DAG.getTargetConstant(TailInputs, DL, MVT::i64));
    for (auto [Reg, VT] : ArgRegs)
      Ops.push_back(DAG.getRegister(Reg, VT));
    if (Glue.getNode())
      Ops.push_back(Glue);
    MF.getFrameInfo().setHasTailCall();
    SDValue Ret =
        DAG.getNode(Indirect ? IA64ISD::TAILCALL_IND : IA64ISD::TAILCALL, DL,
                    MVT::Other, Ops);
    DAG.addNoMergeSiteInfo(Ret.getNode(), CLI.NoMerge);
    return Ret;
  }

  SDValue GP = DAG.getRegister(IA64::R0, MVT::i64);
  if (!SameModule) {
    Register GPReg = MRI.getLiveInVirtReg(IA64::R1);
    if (!GPReg)
      GPReg = MF.addLiveIn(IA64::R1, &IA64::GRSaveRegClass);
    GP = DAG.getCopyFromReg(DAG.getEntryNode(), DL, GPReg, MVT::i64);
  }

  // The call node takes the GP to restore, which lets it live in a register
  // the call clobbers. That does not work for a returns-twice callee, so
  // there GP is restored by a separate copy after the call.
  bool ReturnsTwice = returnsTwice(DAG, Callee, CLI.CB);
  SDValue GPAfter;
  if (ReturnsTwice && !SameModule) {
    GPAfter = GP;
    GP = DAG.getRegister(IA64::R0, MVT::i64);
  }

  SmallVector<SDValue, 12> Ops = {Chain};
  if (Indirect)
    Ops.append({Entry, CalleeGP});
  else
    Ops.push_back(Callee);
  Ops.push_back(GP);
  for (auto [Reg, VT] : ArgRegs)
    Ops.push_back(DAG.getRegister(Reg, VT));
  Ops.push_back(DAG.getRegisterMask(
      ReturnsTwice ? TRI->getReturnsTwiceCallPreservedMask()
                   : TRI->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue.getNode())
    Ops.push_back(Glue);

  Chain = DAG.getNode(Indirect ? IA64ISD::CALL_IND : IA64ISD::CALL, DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Ops);
  DAG.addNoMergeSiteInfo(Chain.getNode(), CLI.NoMerge);
  Glue = Chain.getValue(1);
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetCCInfo(CLI.CallConv, CLI.IsVarArg, MF, RVLocs, *DAG.getContext());
  RetCCInfo.AnalyzeCallResult(CLI.Ins, RetCC_IA64);
  for (const CCValAssign &VA : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue)
                .getValue(1);
    InVals.push_back(isSignificandLoc(VA)
                         ? getSignificand(DAG, DL, Chain.getValue(0))
                         : Chain.getValue(0));
    Glue = Chain.getValue(2);
  }
  if (GPAfter)
    Chain = DAG.getCopyToReg(Chain, DL, IA64::R1, GPAfter);
  return Chain;
}

// The front end lowers an HFA to an array of its element type, which is not
// passed the same way as that many separate FP arguments.
bool IA64TargetLowering::functionArgumentNeedsConsecutiveRegisters(
    Type *Ty, CallingConv::ID CallConv, bool IsVarArg,
    const DataLayout &DL) const {
  return Ty->isArrayTy() && Ty->getArrayElementType()->isFloatingPointTy();
}

// A return block that is only a tail call is worth keeping separate from the
// function's other returns.
bool IA64TargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  return CI->isTailCall();
}

// The tail call conditions of LowerCall, checked at the IR level. The callee
// must share GP, which only a DSO-local function does, so indirect calls do
// not qualify.
bool IA64TargetLowering::supportsTailCallFor(const CallBase *CB) const {
  const Function *Caller = CB->getCaller();
  const Function *Callee = CB->getCalledFunction();
  if (!Callee || !getTargetMachine().shouldAssumeDSOLocal(Callee) ||
      Caller->isVarArg() || Caller->hasFnAttribute("syscall-linkage") ||
      Caller->getCallingConv() != CB->getCallingConv() ||
      CB->getFunctionType()->isVarArg())
    return false;
  // No argument may go on the stack. Each argument of the types accepted
  // here takes one slot, so at most eight fit in registers.
  if (CB->arg_size() > 8)
    return false;
  for (unsigned I = 0, E = CB->arg_size(); I != E; ++I) {
    Type *Ty = CB->getArgOperand(I)->getType();
    if (CB->isByValArgument(I) || CB->isInAllocaArgument(I) ||
        CB->paramHasAttr(I, Attribute::Preallocated))
      return false;
    if (!Ty->isPointerTy() && !Ty->isFloatTy() && !Ty->isDoubleTy() &&
        !(Ty->isIntegerTy() && Ty->getIntegerBitWidth() <= 64))
      return false;
  }
  return true;
}

bool IA64TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_IA64);
}

SDValue
IA64TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_IA64);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "a result that is not in a register");
    SDValue Val = OutVals[I];
    if (isSignificandLoc(VA))
      Val = setSignificand(DAG, DL, Val);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  MachineFunction &MF = DAG.getMachineFunction();
  if (Register SRetReg = MF.getInfo<IA64MachineFunctionInfo>()->getSRetReg()) {
    SDValue Address = DAG.getCopyFromReg(Chain, DL, SRetReg, MVT::i64);
    Chain = DAG.getCopyToReg(Address.getValue(1), DL, IA64::R8, Address, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(IA64::R8, MVT::i64));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(IA64ISD::RET_GLUE, DL, MVT::Other, RetOps);
}

// fetchadd adds one of eight immediates (+/-1, 4, 8, 16) to a 4- or 8-byte
// location, and xchg stores a register. Everything else is a cmpxchg loop.
bool IA64TargetLowering::isFetchAddIncrement(int64_t Inc) {
  switch (Inc < 0 ? -uint64_t(Inc) : uint64_t(Inc)) {
  case 1:
  case 4:
  case 8:
  case 16:
    return true;
  default:
    return false;
  }
}

TargetLowering::AtomicExpansionKind
IA64TargetLowering::shouldExpandAtomicRMWInIR(const AtomicRMWInst *RMW) const {
  unsigned Bits = RMW->getType()->getPrimitiveSizeInBits();
  switch (RMW->getOperation()) {
  case AtomicRMWInst::Xchg:
    if (RMW->getType()->isIntegerTy() && Bits <= 64)
      return AtomicExpansionKind::None;
    break;
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
    if (const auto *C = dyn_cast<ConstantInt>(RMW->getValOperand()))
      if ((Bits == 32 || Bits == 64) && isFetchAddIncrement(C->getSExtValue()))
        return AtomicExpansionKind::None;
    break;
  default:
    break;
  }
  return AtomicExpansionKind::CmpXChg;
}

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

// These are GCC's IA-64 constraints: "a" is a register usable as the addl
// base (r0-r3), "b" a branch register and "f" an FP register. The capitals
// are immediates of various field widths, and "Q" and "S" are memory.
TargetLowering::ConstraintType
IA64TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'a':
    case 'b':
    case 'f':
      return C_RegisterClass;
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'R':
      return C_Immediate;
    case 'Q':
    case 'S':
      return C_Memory;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
IA64TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return {0U, &IA64::GRRegClass};
    case 'a':
      return {0U, &IA64::GR0_3RegClass};
    case 'b':
      return {0U, &IA64::BRRegClass};
    case 'f':
      if (VT == MVT::f80)
        return {0U, &IA64::FR80RegClass};
      return {0U, &IA64::FRRegClass};
    default:
      break;
    }
  }
  // ABI aliases for some registers.
  unsigned Reg = StringSwitch<unsigned>(Constraint.lower())
                     .Case("{gp}", IA64::R1)
                     .Case("{sp}", IA64::R12)
                     .Case("{tp}", IA64::R13)
                     .Case("{rp}", IA64::B0)
                     .Default(0);
  if (Reg)
    return {Reg, Reg == IA64::B0 ? &IA64::BRRegClass : &IA64::GRRegClass};
  // Application register names, which the register file knows only by
  // number.
  Reg = StringSwitch<unsigned>(Constraint.lower())
            .Case("{ar.ccv}", IA64::AR32)
            .Case("{ar.unat}", IA64::AR36)
            .Case("{ar.pfs}", IA64::AR64)
            .Case("{ar.lc}", IA64::AR65)
            .Case("{ar.ec}", IA64::AR66)
            .Default(0);
  if (Reg)
    return {Reg, &IA64::ARRegClass};
  // Predicate registers have no legal value type, which the generic lookup
  // by name requires, so match them here.
  StringRef Name = Constraint;
  unsigned N;
  if (Name.consume_front("{p") && Name.consume_back("}") &&
      !Name.getAsInteger(10, N) && N < 64)
    return {IA64::P0 + N, &IA64::PRRegClass};
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

// The register for a global register variable. It must be reserved, or the
// register allocator would clobber the variable.
Register
IA64TargetLowering::getRegisterByName(const char *RegName, LLT /*Ty*/,
                                      const MachineFunction &MF) const {
  StringRef Name(RegName);
  Register Reg = StringSwitch<Register>(Name)
                     .Case("gp", IA64::R1)
                     .Case("sp", IA64::R12)
                     .Case("tp", IA64::R13)
                     .Default(Register());
  unsigned N;
  if (!Reg && Name.consume_front("r") && !Name.getAsInteger(10, N) && N < 32)
    Reg = IA64::R0 + N;
  if (!Reg)
    reportFatalUsageError(Twine("invalid register name \"") + RegName + "\"");
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (!TRI->getReservedRegs(MF).test(Reg.id()))
    reportFatalUsageError(Twine("register \"") + RegName +
                          "\" is not reserved, so no variable can be kept in "
                          "it");
  return Reg;
}

void IA64TargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                      StringRef Constraint,
                                                      std::vector<SDValue> &Ops,
                                                      SelectionDAG &DAG) const {
  if (Constraint.size() != 1)
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);
  auto *C = dyn_cast<ConstantSDNode>(Op);
  int64_t Value = C ? C->getSExtValue() : 0;
  bool Fits;
  switch (Constraint[0]) {
  default:
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);
  case 'I': // adds immediate.
    Fits = isInt<14>(Value);
    break;
  case 'J': // addl immediate.
    Fits = isInt<22>(Value);
    break;
  case 'K': // Logical instruction immediate.
    Fits = isInt<8>(Value);
    break;
  case 'L': // Compare immediate plus one.
    Fits = Value >= -127 && Value <= 128;
    break;
  case 'M': // A shift count.
    Fits = isUInt<6>(Value);
    break;
  case 'N': // Load/store post-increment immediate.
    Fits = isInt<9>(Value);
    break;
  case 'O':
    Fits = Value == 0;
    break;
  case 'P': // dep immediate form.
    Fits = Value == 0 || Value == -1;
    break;
  case 'R': // shladd shift count.
    Fits = Value >= 1 && Value <= 4;
    break;
  }
  if (C && Fits)
    Ops.push_back(
        DAG.getSignedTargetConstant(Value, SDLoc(Op), Op.getValueType()));
}

InlineAsm::ConstraintCode
IA64TargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  if (ConstraintCode == "Q")
    return InlineAsm::ConstraintCode::Q;
  if (ConstraintCode == "S")
    return InlineAsm::ConstraintCode::S;
  return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
}

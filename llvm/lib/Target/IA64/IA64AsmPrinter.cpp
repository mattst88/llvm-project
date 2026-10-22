//===-- IA64AsmPrinter.cpp - IA64 LLVM Assembly Printer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format IA64 assembly language.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64TargetMachine.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64InstPrinter.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/EHStreamer.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

// Whether a function's unwind info names a personality routine and is
// followed by language-specific data for it. True for a function with a
// landing pad, and for one that needs an unwind table entry and whose
// personality routine is not a no-op without an invoke.
bool hasHandlerData(const MachineFunction &MF) {
  if (!MF.getLandingPads().empty())
    return true;
  const Function &F = MF.getFunction();
  if (!F.hasPersonalityFn() || !F.needsUnwindTableEntry())
    return false;
  return !isNoOpWithoutInvoke(
      classifyEHPersonality(F.getPersonalityFn()->stripPointerCasts()));
}

// The exception table is the last part of a function's unwind info. It is
// emitted at the end of the procedure, between .handlerdata and .endp.
class IA64Exception : public EHStreamer {
public:
  explicit IA64Exception(AsmPrinter *A) : EHStreamer(A) {}

  void endModule() override {}
  void beginFunction(const MachineFunction *MF) override {}

  void endFunction(const MachineFunction *MF) override {
    if (!hasHandlerData(*MF))
      return;
    MCStreamer &OS = *Asm->OutStreamer;
    auto &TS = static_cast<IA64TargetStreamer &>(*OS.getTargetStreamer());

    // Data emitted for the function, such as a jump table, may have left
    // another section current.
    OS.switchSection(MF->getSection());
    const Function &F = MF->getFunction();
    if (F.hasPersonalityFn())
      if (const auto *Per =
              dyn_cast<GlobalValue>(F.getPersonalityFn()->stripPointerCasts()))
        TS.emitPersonality(Asm->getSymbol(Per));
    TS.emitHandlerData();
    emitExceptionTable();
    OS.switchSection(MF->getSection());
    TS.emitEndp();
  }
};

class IA64AsmPrinter : public AsmPrinter {
public:
  explicit IA64AsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  static char ID;

  StringRef getPassName() const override { return "IA64 Assembly Printer"; }

  bool doInitialization(Module &M) override;
  void emitStartOfAsmFile(Module &M) override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitFunctionBodyEnd() override;
  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;
  void emitDebugInstrLabel(MCSymbol *Sym) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;
  void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                        const MCSubtargetInfo *EndInfo,
                        const MachineInstr *MI) override;
  void emitGlobalVariableInitializer(const GlobalVariable *GV) override;
  void emitFunctionDescriptors(const Constant *C);
  const MCExpr *lowerConstant(const Constant *CV, const Constant *BaseCV,
                              uint64_t Offset) override;
  void emitGlobalAlias(const Module &M, const GlobalAlias &GA) override;

private:
  IA64TargetStreamer &getTargetStreamer() const {
    return static_cast<IA64TargetStreamer &>(*OutStreamer->getTargetStreamer());
  }

  MCOperand lowerOperand(const MachineOperand &MO) const;
  void emitUnwindDirectives(const MachineInstr &MI);
  void emitProfilerCall();

  // The physical register that r120, the first output register as codegen
  // numbers it, maps to in this function.
  MCRegister FirstOutput;

  // The number of prologue instructions not yet emitted.
  unsigned PrologueLeft = 0;
  // Whether the prologue has emitted .vframe for the entry sp.
  bool HasVFrame = false;
  // The last .label_state number, and whether the following code must
  // .copy_state it back because an epilogue undid it.
  unsigned StateLabel = 0;
  bool CopyState = false;
  // Whether the last instruction emitted was a call followed by a stop, so
  // that a new bundle starts here.
  bool AfterCall = false;
  // Whether an alias's aliasee is being lowered.
  bool InAlias = false;
};

} // end anonymous namespace

char IA64AsmPrinter::ID = 0;

const MCExpr *IA64AsmPrinter::lowerConstant(const Constant *CV,
                                            const Constant *BaseCV,
                                            uint64_t Offset) {
  // A function pointer is the address of its function descriptor, which the
  // linker creates via @fptr.
  // An alias of a function names the code and has no descriptor of its own.
  if (const auto *GV = dyn_cast<GlobalValue>(CV))
    if (GV->getValueType()->isFunctionTy() && !InAlias)
      return MCSpecifierExpr::create(
          MCSymbolRefExpr::create(getSymbol(GV), OutContext), IA64::S_FPTR,
          OutContext);
  return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
}

void IA64AsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  InAlias = true;
  AsmPrinter::emitGlobalAlias(M, GA);
  InAlias = false;
}

void IA64AsmPrinter::emitGlobalVariableInitializer(const GlobalVariable *GV) {
  if (!GV->hasAttribute("function-descriptors"))
    return AsmPrinter::emitGlobalVariableInitializer(GV);
  emitFunctionDescriptors(GV->getInitializer());
}

// A vtable holds each function's descriptor inline rather than a pointer to
// it, and only the linker knows the descriptor contents. The IR marks a
// two-word descriptor slot by pointing its first word at the function. The
// vtable may have been wrapped in a struct, as a sanitizer appends a red
// zone.
void IA64AsmPrinter::emitFunctionDescriptors(const Constant *C) {
  const DataLayout &DL = getDataLayout();
  if (const auto *CS = dyn_cast<ConstantStruct>(C)) {
    const StructLayout *SL = DL.getStructLayout(CS->getType());
    for (unsigned I = 0, E = CS->getNumOperands(); I != E; ++I) {
      const Constant *Member = CS->getOperand(I);
      emitFunctionDescriptors(Member);
      uint64_t End =
          SL->getElementOffset(I) + DL.getTypeAllocSize(Member->getType());
      uint64_t Next =
          I + 1 == E ? SL->getSizeInBytes() : SL->getElementOffset(I + 1);
      OutStreamer->emitZeros(Next - End);
    }
    return;
  }

  const auto *CA = dyn_cast<ConstantArray>(C);
  if (!CA || !CA->getType()->getElementType()->isPointerTy())
    return emitGlobalConstant(DL, C);
  auto &TS =
      static_cast<IA64TargetStreamer &>(*OutStreamer->getTargetStreamer());
  for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I) {
    const auto *F = dyn_cast<GlobalValue>(CA->getOperand(I));
    if (!F || !F->getValueType()->isFunctionTy() || I + 1 == E ||
        !CA->getOperand(I + 1)->isNullValue()) {
      emitGlobalConstant(DL, CA->getOperand(I));
      continue;
    }
    TS.emitData16(MCSpecifierExpr::create(
        MCSymbolRefExpr::create(getSymbol(F), OutContext), IA64::S_IPLT,
        OutContext));
    ++I;
  }
}

MCOperand IA64AsmPrinter::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return MCOperand();
    if (MO.getReg() >= IA64::R120 && MO.getReg() <= IA64::R127)
      return MCOperand::createReg(FirstOutput + (MO.getReg() - IA64::R120));
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_GlobalAddress: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(getSymbol(MO.getGlobal()), OutContext);
    if (MO.getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), OutContext), OutContext);
    if (auto Spec = IA64::Specifier(MO.getTargetFlags()))
      Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
    return MCOperand::createExpr(Expr);
  }
  case MachineOperand::MO_BlockAddress: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        GetBlockAddressSymbol(MO.getBlockAddress()), OutContext);
    if (auto Spec = IA64::Specifier(MO.getTargetFlags()))
      Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
    return MCOperand::createExpr(Expr);
  }
  case MachineOperand::MO_JumpTableIndex: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(GetJTISymbol(MO.getIndex()), OutContext);
    if (auto Spec = IA64::Specifier(MO.getTargetFlags()))
      Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
    return MCOperand::createExpr(Expr);
  }
  case MachineOperand::MO_ConstantPoolIndex: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(GetCPISymbol(MO.getIndex()), OutContext);
    if (MO.getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), OutContext), OutContext);
    if (auto Spec = IA64::Specifier(MO.getTargetFlags()))
      Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
    return MCOperand::createExpr(Expr);
  }
  case MachineOperand::MO_ExternalSymbol: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        GetExternalSymbolSymbol(MO.getSymbolName()), OutContext);
    if (auto Spec = IA64::Specifier(MO.getTargetFlags()))
      Expr = MCSpecifierExpr::create(Expr, Spec, OutContext);
    return MCOperand::createExpr(Expr);
  }
  case MachineOperand::MO_RegisterMask:
    return MCOperand();
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), OutContext));
  default:
    llvm_unreachable("unknown operand type");
  }
}

// A register stack frame's outputs follow its inputs and locals, whose
// counts come from the prologue alloc.
bool IA64AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  FirstOutput = IA64::R120;
  for (const MachineInstr &MI : MF.front())
    if (MI.getOpcode() == IA64::ALLOC && MI.getFlag(MachineInstr::FrameSetup)) {
      FirstOutput =
          IA64::R32 + MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
      break;
    }
  return AsmPrinter::runOnMachineFunction(MF);
}

// Each function is an assembler procedure, which gives it an unwind table
// entry and, with GNU as, a size that includes its last bundle.
void IA64AsmPrinter::emitFunctionEntryLabel() {
  // Prologue data follows the label and the code follows it, so it must keep
  // bundles 16-byte aligned.
  const Function &F = MF->getFunction();
  if (F.hasPrologueData() &&
      !isAligned(Align(16), F.getDataLayout().getTypeAllocSize(
                                F.getPrologueData()->getType())))
    reportFatalUsageError("prologue data that is not a whole number of "
                          "bundles is not supported on IA-64");
  getTargetStreamer().emitProc(CurrentFnSym);
  AsmPrinter::emitFunctionEntryLabel();
}

// A procedure with an exception table ends after the table, which is not
// emitted yet. The table and the function size both need the end address of
// the last bundle, and GNU as places a label after an instruction inside that
// instruction's bundle unless the bundle has been closed.
//
// Debug info is emitted after this from the DBG_VALUE locations, and numbers
// registers by name. Until here an output register is named by its position
// among the outputs (r120 up), which is what tells when a call clobbers its
// value; rename it to its register in this frame before its DWARF number is
// requested.
void IA64AsmPrinter::emitFunctionBodyEnd() {
  if (FirstOutput != IA64::R120)
    for (MachineBasicBlock &MBB : *MF)
      for (MachineInstr &MI : MBB) {
        if (!MI.isDebugValue())
          continue;
        for (MachineOperand &MO : MI.debug_operands())
          if (MO.isReg() && MO.getReg() >= IA64::R120 &&
              MO.getReg() <= IA64::R127)
            MO.setReg(FirstOutput + (MO.getReg() - IA64::R120));
      }
  if (hasHandlerData(*MF))
    OutStreamer->emitValueToAlignment(Align(16));
  else
    getTargetStreamer().emitEndp();
}

bool IA64AsmPrinter::doInitialization(Module &M) {
  bool Changed = AsmPrinter::doInitialization(M);
  EHHandlers.push_back(std::make_unique<IA64Exception>(this));
  return Changed;
}

// The emitted stops are the instruction group boundaries. GNU as must be told
// so: otherwise it warns about each given stop and inserts stops where it
// infers they are needed. gcc avoids this by running it with "-x".
void IA64AsmPrinter::emitStartOfAsmFile(Module &M) {
  getTargetStreamer().emitExplicitMode();
}

// The profiler hook is called before the function does anything, and it
// preserves everything the function was entered with: the arguments, the
// frame, gp and b0. It is passed what it cannot find itself, in a frame with
// the eight argument registers as inputs and four outputs: ar.pfs, gp, b0,
// and the address of a counter, which is no longer read. The call sequence is
// its own prologue region, since the call clobbers b0.
void IA64AsmPrinter::emitProfilerCall() {
  Attribute Attr =
      MF->getFunction().getFnAttribute("ia64-instrument-function-entry");
  if (!Attr.isValid())
    return;
  IA64TargetStreamer &TS = getTargetStreamer();
  const MCRegister Out = IA64::R40;
  TS.emitPrologue();
  TS.emitSave(IA64TargetStreamer::UR_PFS, Out - IA64::R0);
  EmitToStreamer(*OutStreamer, MCInstBuilder(IA64::ALLOC)
                                   .addReg(Out)
                                   .addImm(8)
                                   .addImm(0)
                                   .addImm(4)
                                   .addImm(0)
                                   .addReg(IA64::P0));
  // The call allocates a frame like the alloc does, so they need a stop
  // between them.
  MCInst Counter = MCInstBuilder(IA64::ADDS)
                       .addReg(Out + 3)
                       .addImm(0)
                       .addReg(IA64::R0)
                       .addReg(IA64::P0);
  Counter.setFlags(IA64II::StopAfter);
  EmitToStreamer(*OutStreamer, Counter);
  TS.emitSave(IA64TargetStreamer::UR_RP, Out + 2 - IA64::R0);
  EmitToStreamer(*OutStreamer, MCInstBuilder(IA64::MOV_FROM_BR)
                                   .addReg(Out + 2)
                                   .addReg(IA64::B0)
                                   .addReg(IA64::P0));
  TS.emitBody();
  EmitToStreamer(*OutStreamer, MCInstBuilder(IA64::ADDS)
                                   .addReg(Out + 1)
                                   .addImm(0)
                                   .addReg(IA64::R1)
                                   .addReg(IA64::P0));
  MCInst Call = MCInstBuilder(IA64::BR_CALL)
                    .addReg(IA64::B0)
                    .addExpr(MCSymbolRefExpr::create(
                        OutContext.getOrCreateSymbol(Attr.getValueAsString()),
                        OutContext))
                    .addImm(IA64II::WhetherSPTK)
                    .addImm(1)
                    .addImm(0)
                    .addReg(IA64::P0);
  Call.setFlags(IA64II::StopAfter);
  EmitToStreamer(*OutStreamer, Call);
}

// Callee-saved registers are saved at function entry, in the prologue
// region. The rest is a body region, where the unwinder undoes the frame
// using the prologue's unwind directives.
void IA64AsmPrinter::emitFunctionBodyStart() {
  PrologueLeft = 0;
  HasVFrame = false;
  StateLabel = 0;
  CopyState = false;
  AfterCall = false;
  for (const MachineInstr &MI : MF->front())
    if (MI.getFlag(MachineInstr::FrameSetup) && !MI.isMetaInstruction())
      ++PrologueLeft;
  emitProfilerCall();
  getTargetStreamer().emitPrologue();
  if (unsigned Gap = MF->getInfo<IA64MachineFunctionInfo>()->getSpillAreaGap())
    getTargetStreamer().emitSpillBase(Gap - IA64FrameLowering::ScratchAreaSize);
  if (!PrologueLeft)
    getTargetStreamer().emitBody();
}

// A label ends the bundle, and debug info must not change bundling, so use
// a tag instead. A tag is the slot address of the next instruction, which is
// also what the line table uses. A call returns to the start of the next
// bundle, not to a slot, and the instruction after the call may be placed in
// a later slot, so after a call the tag is for the bundle.
void IA64AsmPrinter::emitDebugInstrLabel(MCSymbol *Sym) {
  getTargetStreamer().emitTag(Sym, SMLoc(), AfterCall);
}

// An epilogue undoes the frame only for the rest of its region, so the code
// after it is a new body region that copies the state from before the
// epilogue.
void IA64AsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  AsmPrinter::emitBasicBlockStart(MBB);
  if (!CopyState)
    return;
  getTargetStreamer().emitBody();
  getTargetStreamer().emitCopyState(StateLabel);
  CopyState = false;
}

// Whether any real instruction follows a block. The following blocks may be
// empty, and the assembler does not describe an empty region.
static bool isFollowedByCode(const MachineBasicBlock &MBB) {
  for (const MachineBasicBlock &Next :
       make_range(std::next(MBB.getIterator()), MBB.getParent()->end()))
    for (const MachineInstr &MI : Next)
      if (!MI.isMetaInstruction())
        return true;
  return false;
}

// Emit unwind directives for a prologue or epilogue instruction.
void IA64AsmPrinter::emitUnwindDirectives(const MachineInstr &MI) {
  IA64TargetStreamer &TS = getTargetStreamer();
  if (MI.getFlag(MachineInstr::FrameDestroy)) {
    if (!MI.modifiesRegister(IA64::R12, /*TRI=*/nullptr))
      return;
    if (isFollowedByCode(*MI.getParent())) {
      TS.emitLabelState(++StateLabel);
      CopyState = true;
    }
    TS.emitRestore();
    return;
  }
  // An alloc outside the prologue hands the frame to a tail callee, with
  // everything restored to its state on entry, so the rest of the function
  // is like a prologue that has not saved anything yet.
  if (MI.getOpcode() == IA64::ALLOC && !MI.getFlag(MachineInstr::FrameSetup)) {
    if (!CopyState && isFollowedByCode(*MI.getParent())) {
      TS.emitLabelState(++StateLabel);
      CopyState = true;
    }
    TS.emitPrologue();
    return;
  }
  if (!MI.getFlag(MachineInstr::FrameSetup))
    return;
  switch (MI.getOpcode()) {
  case IA64::ALLOC:
    TS.emitSave(IA64TargetStreamer::UR_PFS,
                MI.getOperand(0).getReg() - IA64::R0);
    break;
  case IA64::MOV_FROM_BR:
    if (MI.getOperand(1).getReg() == IA64::B0)
      TS.emitSave(IA64TargetStreamer::UR_RP,
                  MI.getOperand(0).getReg() - IA64::R0);
    else
      // .save.b with a GR also marks the BR as spilled in the memory spill
      // order, and an unwinder that follows that order would find no memory
      // slot for it. Use .spillreg instead.
      TS.emitSpillReg(IA64TargetStreamer::SR_BR +
                          (MI.getOperand(1).getReg() - IA64::B0),
                      MI.getOperand(0).getReg());
    break;
  case IA64::MOV_FROM_AR:
    if (MI.getOperand(1).getReg() == IA64::AR36)
      TS.emitSave(IA64TargetStreamer::UR_UNAT,
                  MI.getOperand(0).getReg() - IA64::R0);
    else if (MI.getOperand(1).getReg() == IA64::AR65)
      TS.emitSave(IA64TargetStreamer::UR_LC,
                  MI.getOperand(0).getReg() - IA64::R0);
    break;
  case IA64::MOV_FROM_PR:
    TS.emitSave(IA64TargetStreamer::UR_PR,
                MI.getOperand(0).getReg() - IA64::R0);
    break;
  // Callee-saved registers spilled to memory are at the location the
  // unwinder expects, so only the mask is recorded.
  case IA64::ST8_SPILL:
    TS.emitSaveG(1u << (MI.getOperand(1).getReg() - IA64::R4));
    break;
  case IA64::STF_SPILL: {
    MCRegister Reg = MI.getOperand(1).getReg();
    if (Reg <= IA64::F5)
      TS.emitSaveF(1u << (Reg - IA64::F2));
    else
      TS.emitSaveGF(0, 1u << (Reg - IA64::F16 + 4));
    break;
  }
  case IA64::ADDS:
    // A copy of sp made before sp moves locates the frame from then on.
    if (MI.getOperand(0).getReg() != IA64::R12) {
      if (!HasVFrame && MI.getOperand(1).isImm() &&
          MI.getOperand(1).getImm() == 0 && MI.getOperand(2).isReg() &&
          MI.getOperand(2).getReg() == IA64::R12) {
        TS.emitVFrame(MI.getOperand(0).getReg() - IA64::R0);
        HasVFrame = true;
      }
      break;
    }
    [[fallthrough]];
  case IA64::ADD:
    if (!HasVFrame && MI.modifiesRegister(IA64::R12, /*TRI=*/nullptr))
      TS.emitFFrame(MF->getFrameInfo().getStackSize());
    break;
  default:
    break;
  }
}

bool IA64AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS) {
  MachineOperand MO = MI->getOperand(OpNo);
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1])
      return true;
    switch (ExtraCode[0]) {
    case 'r':
      // A register, printing r0 for an immediate zero.
      if (MO.isImm() && MO.getImm() == 0) {
        OS << "r0";
        return false;
      }
      if (!MO.isReg())
        return true;
      break;
    case 'P':
      // The post-increment of a memory operand. There is none.
      return false;
    default:
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
    }
  }
  // A register named in the asm source is an implicit operand but is still
  // printed.
  if (MO.isReg())
    MO.setImplicit(false);
  MCOperand MCOp = lowerOperand(MO);
  if (MCOp.isReg())
    OS << IA64InstPrinter::getRegisterName(MCOp.getReg());
  else if (MCOp.isImm())
    OS << MCOp.getImm();
  else if (MCOp.isExpr())
    MAI.printExpr(OS, *MCOp.getExpr());
  else
    return true;
  return false;
}

bool IA64AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                           unsigned OpNo, const char *ExtraCode,
                                           raw_ostream &OS) {
  // "P" is the post-increment of the operand, which is nothing.
  if (ExtraCode && ExtraCode[0] == 'P' && !ExtraCode[1])
    return false;
  if (ExtraCode && ExtraCode[0])
    return true;
  OS << '[';
  if (PrintAsmOperand(MI, OpNo, nullptr, OS))
    return true;
  OS << ']';
  return false;
}

// Inline asm is its own instruction group: the stops pass ends the group
// before it, and this ends the group it leaves open.
void IA64AsmPrinter::emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                      const MCSubtargetInfo *EndInfo,
                                      const MachineInstr *MI) {
  AfterCall = false;
  if (MI)
    getTargetStreamer().emitStop();
}

void IA64AsmPrinter::emitInstruction(const MachineInstr *MI) {
  emitUnwindDirectives(*MI);
  // IA64PackGroups chose the template of the bundle this starts.
  const auto &Templates =
      MF->getInfo<IA64MachineFunctionInfo>()->getTemplates();
  if (auto It = Templates.find(MI); It != Templates.end())
    getTargetStreamer().emitTemplate(It->second);
  AfterCall = MI->isCall() && MI->getAsmPrinterFlag(IA64::StopAfter);
  // The prologue ends where its last instruction does.
  struct EndPrologue {
    IA64AsmPrinter &AP;
    bool Last;
    ~EndPrologue() {
      if (Last)
        AP.getTargetStreamer().emitBody();
    }
  } End{*this, MI->getFlag(MachineInstr::FrameSetup) && --PrologueLeft == 0};
  MCInst OutMI;
  if (MI->getOpcode() == TargetOpcode::PATCHABLE_FUNCTION_ENTER) {
    // The count is in instructions, as for gcc, so three make a bundle. The
    // stop is for the alloc that may follow, which must start an instruction
    // group. XRay, which uses the same opcode, is rejected earlier.
    unsigned Num = MF->getFunction().getFnAttributeAsParsedInteger(
        "patchable-function-entry");
    for (unsigned I = 0; I != Num; ++I) {
      MCInst Nop = MF->getSubtarget().getInstrInfo()->getNop();
      if (I + 1 == Num)
        Nop.setFlags(IA64II::StopAfter);
      EmitToStreamer(*OutStreamer, Nop);
    }
    return;
  }
  // The "patchable-function" attribute wants a two-byte first instruction
  // that a short jump can replace, and IA-64 has none.
  if (MI->getOpcode() == TargetOpcode::PATCHABLE_OP)
    reportFatalUsageError("the \"patchable-function\" attribute is not "
                          "supported on IA-64");
  if (MI->getOpcode() == IA64::BRUNCOND || MI->getOpcode() == IA64::BRLONG) {
    // An unconditional branch: "br.cond.sptk.few" under p0, or "brl" if that
    // is out of range.
    OutMI.setOpcode(MI->getOpcode() == IA64::BRLONG ? IA64::BRL_COND
                                                    : IA64::BR_COND);
    OutMI.addOperand(lowerOperand(MI->getOperand(0)));
    OutMI.addOperand(MCOperand::createImm(IA64II::WhetherSPTK));
    OutMI.addOperand(MCOperand::createImm(0));
    OutMI.addOperand(MCOperand::createImm(0));
    OutMI.addOperand(MCOperand::createReg(IA64::P0));
    if (MI->getAsmPrinterFlag(IA64::StopAfter))
      OutMI.setFlags(IA64II::StopAfter);
    EmitToStreamer(*OutStreamer, OutMI);
    return;
  }
  if (MI->getOpcode() == IA64::BRIND) {
    OutMI.setOpcode(IA64::MOV_BR);
    OutMI.addOperand(MCOperand::createReg(IA64::B6));
    OutMI.addOperand(lowerOperand(MI->getOperand(0)));
    OutMI.addOperand(MCOperand::createImm(0));
    OutMI.addOperand(MCOperand::createReg(IA64::P0));
    EmitToStreamer(*OutStreamer, OutMI);
    MCInst Branch;
    Branch.setOpcode(IA64::BR_COND_B);
    Branch.addOperand(MCOperand::createReg(IA64::B6));
    Branch.addOperand(MCOperand::createImm(IA64II::WhetherSPTK));
    Branch.addOperand(MCOperand::createImm(0));
    Branch.addOperand(MCOperand::createImm(0));
    Branch.addOperand(MCOperand::createReg(IA64::P0));
    if (MI->getAsmPrinterFlag(IA64::StopAfter))
      Branch.setFlags(IA64II::StopAfter);
    EmitToStreamer(*OutStreamer, Branch);
    return;
  }
  OutMI.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp = lowerOperand(MO);
    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
  if (MI->getAsmPrinterFlag(IA64::StopAfter))
    OutMI.setFlags(IA64II::StopAfter);
  EmitToStreamer(*OutStreamer, OutMI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64AsmPrinter() {
  RegisterAsmPrinter<IA64AsmPrinter> X(getTheIA64Target());
}

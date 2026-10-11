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
#include "IA64TargetMachine.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class IA64AsmPrinter : public AsmPrinter {
public:
  explicit IA64AsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  static char ID;

  StringRef getPassName() const override { return "IA64 Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyEnd() override;

private:
  IA64TargetStreamer &getTargetStreamer() const {
    return static_cast<IA64TargetStreamer &>(*OutStreamer->getTargetStreamer());
  }

  MCOperand lowerOperand(const MachineOperand &MO) const;
};

} // end anonymous namespace

char IA64AsmPrinter::ID = 0;

MCOperand IA64AsmPrinter::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return MCOperand();
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), OutContext));
  default:
    llvm_unreachable("unknown operand type");
  }
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

void IA64AsmPrinter::emitFunctionBodyEnd() { getTargetStreamer().emitEndp(); }

void IA64AsmPrinter::emitInstruction(const MachineInstr *MI) {
  MCInst OutMI;
  if (MI->getOpcode() == IA64::BRUNCOND) {
    // The branch nothing qualifies: "br.cond.sptk.few" under p0.
    OutMI.setOpcode(IA64::BR_COND);
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

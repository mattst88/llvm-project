//===-- IA64TargetStreamer.cpp - IA64 Target Streamer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64TargetStreamer.h"
#include "IA64BaseInfo.h"
#include "IA64MCTargetDesc.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

IA64TargetStreamer::IA64TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

IA64TargetStreamer::~IA64TargetStreamer() = default;

void IA64TargetStreamer::emitStop() {}

void IA64TargetStreamer::emitTemplate(unsigned Index) {}

void IA64TargetStreamer::emitTag(MCSymbol *Symbol, SMLoc Loc) {}

void IA64TargetStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {}

void IA64TargetStreamer::emitEndp() {}

void IA64TargetStreamer::emitPrologue(unsigned Mask, unsigned GRSave) {}

void IA64TargetStreamer::emitBody() {}

const char *IA64TargetStreamer::getUnwindRegName(UnwindReg Reg) {
  switch (Reg) {
  case UR_RP:
    return "rp";
  case UR_PFS:
    return "ar.pfs";
  case UR_PR:
    return "pr";
  case UR_UNAT:
    return "ar.unat";
  case UR_LC:
    return "ar.lc";
  case UR_FPSR:
    return "ar.fpsr";
  case UR_RNAT:
    return "ar.rnat";
  case UR_BSP:
    return "ar.bsp";
  case UR_BSPSTORE:
    return "ar.bspstore";
  case UR_PRIUNAT:
    return "@priunat";
  }
  llvm_unreachable("not a register that unwinding knows");
}

void IA64TargetStreamer::emitFFrame(uint64_t Size) {}

void IA64TargetStreamer::emitVFrame(unsigned GR) {}

void IA64TargetStreamer::emitVFrameSP(uint64_t Offset) {}

void IA64TargetStreamer::emitSpillBase(uint64_t Offset) {}

void IA64TargetStreamer::emitSave(UnwindReg Reg, unsigned GR) {}

void IA64TargetStreamer::emitSaveSP(UnwindReg Reg, uint64_t Offset) {}

void IA64TargetStreamer::emitSavePSP(UnwindReg Reg, uint64_t Offset) {}

void IA64TargetStreamer::emitAltRP(unsigned BR) {}

void IA64TargetStreamer::emitUnwABI(unsigned ABI, unsigned Context) {}

void IA64TargetStreamer::emitSaveG(unsigned Mask, std::optional<unsigned> GR) {}

void IA64TargetStreamer::emitSaveF(unsigned Mask) {}

void IA64TargetStreamer::emitSaveB(unsigned Mask, std::optional<unsigned> GR) {}

void IA64TargetStreamer::emitSaveGF(unsigned GRMask, unsigned FRMask) {}

void IA64TargetStreamer::emitRestore(unsigned ECount) {}

void IA64TargetStreamer::emitLabelState(uint64_t Label) {}

void IA64TargetStreamer::emitCopyState(uint64_t Label) {}

void IA64TargetStreamer::printSpilledReg(raw_ostream &OS, unsigned Reg) {
  static const char *const Others[] = {
      "pr",      "psp",     "@priunat", "rp",     "ar.bsp", "ar.bspstore",
      "ar.rnat", "ar.unat", "ar.fpsr",  "ar.pfs", "ar.lc"};
  if (Reg >= SR_PR) {
    assert(Reg - SR_PR < std::size(Others) && "not a register to spill");
    OS << Others[Reg - SR_PR];
    return;
  }
  OS << "rfb"[Reg >> 5] << (Reg & 0x1f);
}

void IA64TargetStreamer::emitSpillReg(unsigned Reg, MCRegister TReg,
                                      unsigned QP) {}

void IA64TargetStreamer::emitSpillSP(unsigned Reg, uint64_t Offset,
                                     unsigned QP) {}

void IA64TargetStreamer::emitSpillPSP(unsigned Reg, uint64_t Offset,
                                      unsigned QP) {}

void IA64TargetStreamer::emitRestoreReg(unsigned Reg, unsigned QP) {}

void IA64TargetStreamer::emitPersonality(const MCSymbol *Routine) {}

void IA64TargetStreamer::emitUnwEntry() {}

void IA64TargetStreamer::emitHandlerData() {}

void IA64TargetStreamer::emitData16(const MCExpr *Value, SMLoc Loc) {}

void IA64TargetStreamer::emitTLSCommon(MCSymbol *Sym, uint64_t Size,
                                       Align Alignment) {
  getStreamer().emitCommonSymbol(Sym, Size, Alignment);
  getStreamer().emitSymbolAttribute(Sym, MCSA_ELF_TypeTLS);
}

std::pair<MCSectionELF *, MCSectionELF *>
IA64TargetStreamer::getUnwindSections(MCContext &Ctx,
                                      const MCSectionELF &Text) {
  StringRef Name = Text.getName();
  std::string Table = ".IA_64.unwind", Info = ".IA_64.unwind_info";
  if (Name.consume_front(".gnu.linkonce.t.")) {
    Table = ".gnu.linkonce.ia64unw.";
    Info = ".gnu.linkonce.ia64unwi.";
  }
  if (Name != ".text") {
    Table += Name;
    Info += Name;
  }
  // The unwind sections join the code's section group, if it has one.
  unsigned Flags = ELF::SHF_ALLOC;
  if (Text.getGroup())
    Flags |= ELF::SHF_GROUP;
  return {Ctx.getELFSection(
              Table, ELF::SHT_IA_64_UNWIND, Flags | ELF::SHF_LINK_ORDER, 0,
              Text.getGroup(), Text.isComdat(), MCSection::NonUniqueID,
              static_cast<const MCSymbolELF *>(Text.getBeginSymbol())),
          Ctx.getELFSection(Info, ELF::SHT_PROGBITS, Flags, 0, Text.getGroup(),
                            Text.isComdat(), MCSection::NonUniqueID, nullptr)};
}

IA64TargetAsmStreamer::IA64TargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : IA64TargetStreamer(S), OS(OS) {}

void IA64TargetAsmStreamer::emitStop() { OS << "\t;;\n"; }

void IA64TargetAsmStreamer::emitTemplate(unsigned Index) {
  OS << "\t." << StringRef(IA64II::BundleTemplates[Index].Name).lower() << '\n';
}

void IA64TargetAsmStreamer::emitTag(MCSymbol *Symbol, SMLoc Loc) {
  OS << '[';
  Symbol->print(OS, &getStreamer().getContext().getAsmInfo());
  OS << ":]\n";
}

void IA64TargetAsmStreamer::printEntries() {
  const MCAsmInfo *MAI = &getStreamer().getContext().getAsmInfo();
  ListSeparator LS;
  for (const MCSymbol *Entry : Entries) {
    OS << LS;
    Entry->print(OS, MAI);
  }
  OS << '\n';
}

void IA64TargetAsmStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {
  this->Entries.assign(Entries.begin(), Entries.end());
  OS << "\t.proc\t";
  printEntries();
}

void IA64TargetAsmStreamer::emitEndp() {
  OS << "\t.endp\t";
  printEntries();
}

void IA64TargetAsmStreamer::emitPrologue(unsigned Mask, unsigned GRSave) {
  OS << "\t.prologue";
  if (Mask != 0)
    OS << '\t' << Mask << ", " << GRSave;
  OS << '\n';
}

void IA64TargetAsmStreamer::emitBody() { OS << "\t.body\n"; }

void IA64TargetAsmStreamer::emitFFrame(uint64_t Size) {
  OS << "\t.fframe\t" << Size << '\n';
}

void IA64TargetAsmStreamer::emitVFrame(unsigned GR) {
  OS << "\t.vframe\tr" << GR << '\n';
}

void IA64TargetAsmStreamer::emitVFrameSP(uint64_t Offset) {
  OS << "\t.vframesp\t" << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSpillBase(uint64_t Offset) {
  OS << "\t.spill\t" << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSave(UnwindReg Reg, unsigned GR) {
  OS << "\t.save\t" << getUnwindRegName(Reg) << ", r" << GR << '\n';
}

void IA64TargetAsmStreamer::emitSaveSP(UnwindReg Reg, uint64_t Offset) {
  OS << "\t.savesp\t" << getUnwindRegName(Reg) << ", " << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSavePSP(UnwindReg Reg, uint64_t Offset) {
  OS << "\t.savepsp\t" << getUnwindRegName(Reg) << ", " << Offset << '\n';
}

void IA64TargetAsmStreamer::emitAltRP(unsigned BR) {
  OS << "\t.altrp\tb" << BR << '\n';
}

void IA64TargetAsmStreamer::emitUnwABI(unsigned ABI, unsigned Context) {
  OS << "\t.unwabi\t" << ABI << ", " << Context << '\n';
}

void IA64TargetAsmStreamer::emitRestore(unsigned ECount) {
  OS << "\t.restore\tsp";
  if (ECount != 0)
    OS << ", " << ECount;
  OS << '\n';
}

void IA64TargetAsmStreamer::emitLabelState(uint64_t Label) {
  OS << "\t.label_state\t" << Label << '\n';
}

void IA64TargetAsmStreamer::emitCopyState(uint64_t Label) {
  OS << "\t.copy_state\t" << Label << '\n';
}

void IA64TargetAsmStreamer::emitSaveG(unsigned Mask,
                                      std::optional<unsigned> GR) {
  OS << "\t.save.g\t" << format_hex(Mask, 3);
  if (GR)
    OS << ", r" << *GR;
  OS << '\n';
}

void IA64TargetAsmStreamer::emitSaveF(unsigned Mask) {
  OS << "\t.save.f\t" << format_hex(Mask, 3) << '\n';
}

void IA64TargetAsmStreamer::emitSaveB(unsigned Mask,
                                      std::optional<unsigned> GR) {
  OS << "\t.save.b\t" << format_hex(Mask, 3);
  if (GR)
    OS << ", r" << *GR;
  OS << '\n';
}

void IA64TargetAsmStreamer::emitSaveGF(unsigned GRMask, unsigned FRMask) {
  OS << "\t.save.gf\t" << format_hex(GRMask, 3) << ", " << format_hex(FRMask, 3)
     << '\n';
}

void IA64TargetAsmStreamer::printSpill(StringRef Directive, unsigned Reg,
                                       unsigned QP) {
  OS << '\t' << Directive;
  if (QP != 0)
    OS << ".p\tp" << QP << ", ";
  else
    OS << '\t';
  printSpilledReg(OS, Reg);
}

void IA64TargetAsmStreamer::emitSpillReg(unsigned Reg, MCRegister TReg,
                                         unsigned QP) {
  const MCRegisterInfo &MRI = *getStreamer().getContext().getRegisterInfo();
  printSpill(".spillreg", Reg, QP);
  OS << ", "
     << (MRI.getRegClass(IA64::GRRegClassID).contains(TReg)   ? 'r'
         : MRI.getRegClass(IA64::FRRegClassID).contains(TReg) ? 'f'
                                                              : 'b')
     << MRI.getEncodingValue(TReg) << '\n';
}

void IA64TargetAsmStreamer::emitSpillSP(unsigned Reg, uint64_t Offset,
                                        unsigned QP) {
  printSpill(".spillsp", Reg, QP);
  OS << ", " << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSpillPSP(unsigned Reg, uint64_t Offset,
                                         unsigned QP) {
  printSpill(".spillpsp", Reg, QP);
  OS << ", " << int64_t(Offset) << '\n';
}

void IA64TargetAsmStreamer::emitRestoreReg(unsigned Reg, unsigned QP) {
  printSpill(".restorereg", Reg, QP);
  OS << '\n';
}

void IA64TargetAsmStreamer::emitPersonality(const MCSymbol *Routine) {
  OS << "\t.personality\t";
  Routine->print(OS, &getStreamer().getContext().getAsmInfo());
  OS << '\n';
}

void IA64TargetAsmStreamer::emitUnwEntry() { OS << "\t.unwentry\n"; }

void IA64TargetAsmStreamer::emitHandlerData() {
  MCStreamer &S = getStreamer();
  OS << "\t.handlerdata\n";
  // The directive implies the section switch, so switch without printing;
  // the switch back to the code section is then printed.
  S.switchSectionNoPrint(
      getUnwindSections(S.getContext(), static_cast<const MCSectionELF &>(
                                            *S.getCurrentSectionOnly()))
          .second);
}

void IA64TargetAsmStreamer::emitData16(const MCExpr *Value, SMLoc Loc) {
  OS << "\tdata16.ua\t";
  getStreamer().getContext().getAsmInfo().printExpr(OS, *Value);
  OS << '\n';
}

void IA64TargetAsmStreamer::emitTLSCommon(MCSymbol *Sym, uint64_t Size,
                                          Align Alignment) {
  OS << "\t.tls_common\t";
  Sym->print(OS, &getStreamer().getContext().getAsmInfo());
  OS << ',' << Size << ',' << Alignment.value() << '\n';
}

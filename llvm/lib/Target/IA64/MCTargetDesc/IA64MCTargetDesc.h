//===-- IA64MCTargetDesc.h - IA64 Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides IA64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H

#include "IA64BaseInfo.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInst;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

MCInstrInfo *createIA64MCInstrInfo();
MCCodeEmitter *createIA64MCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);
MCAsmBackend *createIA64AsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);
std::unique_ptr<MCObjectTargetWriter> createIA64ELFObjectWriter(uint8_t OSABI);

namespace IA64 {
/// The bytes the code emitter writes for one 41-bit instruction slot.
constexpr unsigned SlotBytes = 6;

/// The units that can access application register \p Number, one bit per
/// IA64II::Unit: the M unit has the low ones and the I unit ar64-ar111.
/// Either can access the ignored ones, ar48-ar63 and ar112-ar127.
unsigned getARUnits(unsigned Number);

/// The slot kinds that can hold \p MI, one bit per IA64II::Unit. Most
/// instructions have one, or two for an A-unit instruction, but a pseudo-op
/// such as "nop" takes the unit of the slot it lands in.
unsigned getSlotUnits(const MCInst &MI, const MCInstrInfo &MCII,
                      const MCRegisterInfo &MRI);

/// The form of pseudo-op \p Opcode for a slot of unit \p Unit, or 0 if it
/// has none. UnitL selects the X-unit form. Other opcodes are returned
/// unchanged.
unsigned getOpcodeForUnit(unsigned Opcode, IA64II::Unit Unit);

/// The default form of pseudo-op \p MI: the one for the first unit that can
/// execute it, M before I.
unsigned getDefaultOpcode(const MCInst &MI, const MCInstrInfo &MCII,
                          const MCRegisterInfo &MRI);
} // end namespace IA64
} // end namespace llvm

// Defines symbolic names for IA64 registers.
#define GET_REGINFO_ENUM
#include "IA64GenRegisterInfo.inc"

// Defines symbolic names for the IA64 instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "IA64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "IA64GenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H

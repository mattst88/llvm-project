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

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
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

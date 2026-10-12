//===-- IA64FixupKinds.h - IA64 Specific Fixup Entries ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64FIXUPKINDS_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace IA64 {

// A fixup names the instruction field that receives the value. Its offset is
// the bundle address plus the slot number, which is how ELF addresses an
// instruction. An X-unit instruction is addressed by its L slot.
enum Fixups {
  // The 14-bit immediate of adds and addp4.
  fixup_ia64_imm14 = FirstTargetFixupKind,
  // The 22-bit immediate of addl.
  fixup_ia64_imm22,
  // The 64-bit immediate of movl.
  fixup_ia64_imm64,
  // The target of a branch, a call or chk.a.
  fixup_ia64_pcrel21b,
  // The target of chk.s.
  fixup_ia64_pcrel21m,
  // The target of fchkf.
  fixup_ia64_pcrel21f,
  // The target of brl.
  fixup_ia64_pcrel60b,
  // The tag of brp and of a hinted move to a branch register: the address of
  // the branch being hinted.
  fixup_ia64_tag13b,
  // The same, in the tag field of a move to a branch register.
  fixup_ia64_tag13i,
  // The symbol of ld8.mov. It fills no field: the relocation marks the
  // instruction for linker relaxation.
  fixup_ia64_ldxmov,
  // The two data16 words that the dynamic linker fills with a function's
  // address and gp, forming a non-official function descriptor.
  fixup_ia64_iplt,
  // A bundle, which fills no field either: its address must be 16-byte
  // aligned, and the preceding code left its alignment unknown.
  fixup_ia64_bundle,

  fixup_ia64_invalid,
  NumTargetFixupKinds = fixup_ia64_invalid - FirstTargetFixupKind
};

} // end namespace IA64
} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64FIXUPKINDS_H

//===-- IA64TargetStreamer.h - IA64 Target Streamer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class formatted_raw_ostream;

class IA64TargetStreamer : public MCTargetStreamer {
public:
  explicit IA64TargetStreamer(MCStreamer &S);
  ~IA64TargetStreamer() override;

  /// End the current instruction group. Assembly writes this as ";;".
  virtual void emitStop();
};

class IA64TargetAsmStreamer : public IA64TargetStreamer {
  formatted_raw_ostream &OS;

public:
  IA64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitStop() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H

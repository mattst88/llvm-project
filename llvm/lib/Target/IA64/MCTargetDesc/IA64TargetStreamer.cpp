//===-- IA64TargetStreamer.cpp - IA64 Target Streamer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64TargetStreamer.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

IA64TargetStreamer::IA64TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

IA64TargetStreamer::~IA64TargetStreamer() = default;

void IA64TargetStreamer::emitStop() {}

IA64TargetAsmStreamer::IA64TargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : IA64TargetStreamer(S), OS(OS) {}

void IA64TargetAsmStreamer::emitStop() { OS << "\t;;\n"; }

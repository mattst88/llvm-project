//===------ ia64.cpp - Generic JITLink ia64 edge kinds, utilities ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing ia64 objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ia64.h"

#define DEBUG_TYPE "jitlink"

namespace llvm::jitlink::ia64 {

const char NullPointerContent[PointerSize] = {0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00};

const char NullDescriptorContent[DescriptorSize] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const uint8_t StubContent[StubEntrySize] = {
    // addl r15 = 0, r1 ;; ld8 r15 = [r15] ; nop.i 0 ;;
    0x0b,
    0x78,
    0x00,
    0x02,
    0x00,
    0x24,
    0xf0,
    0x00,
    0x3c,
    0x30,
    0x20,
    0x00,
    0x00,
    0x00,
    0x04,
    0x00,
    // ld8 r16 = [r15], 8 ;; ld8 r1 = [r15] ; mov b6 = r16 ;;
    0x0b,
    0x80,
    0x20,
    0x1e,
    0x18,
    0x14,
    0x10,
    0x00,
    0x3c,
    0x30,
    0x20,
    0xc0,
    0x00,
    0x09,
    0x00,
    0x07,
    // nop.m 0 ; nop.i 0 ; br b6 ;;
    0x11,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x02,
    0x00,
    0x00,
    0x60,
    0x00,
    0x80,
    0x00,
};

const char *getEdgeKindName(Edge::Kind K) {
  switch (K) {
  case Pointer64:
    return "Pointer64";
  case Pointer32:
    return "Pointer32";
  case Delta64:
    return "Delta64";
  case Delta32:
    return "Delta32";
  case Imm14:
    return "Imm14";
  case Imm22:
    return "Imm22";
  case Imm64:
    return "Imm64";
  case PCRel22:
    return "PCRel22";
  case PCRel64I:
    return "PCRel64I";
  case GPRel22:
    return "GPRel22";
  case GPRel64I:
    return "GPRel64I";
  case GPRel32:
    return "GPRel32";
  case GPRel64:
    return "GPRel64";
  case GlobalPointer64:
    return "GlobalPointer64";
  case PCRel21B:
    return "PCRel21B";
  case PCRel21M:
    return "PCRel21M";
  case PCRel21F:
    return "PCRel21F";
  case PCRel60B:
    return "PCRel60B";
  case SegRel64:
    return "SegRel64";
  case SegRel32:
    return "SegRel32";
  case FunctionDescriptor:
    return "FunctionDescriptor";
  case RequestDescriptor64:
    return "RequestDescriptor64";
  case RequestDescriptorImm64:
    return "RequestDescriptorImm64";
  case RequestGOTAndTransformToGPRel22:
    return "RequestGOTAndTransformToGPRel22";
  case RequestGOTAndTransformToGPRel64I:
    return "RequestGOTAndTransformToGPRel64I";
  case RequestDescriptorGOTAndTransformToGPRel22:
    return "RequestDescriptorGOTAndTransformToGPRel22";
  case RequestDescriptorGOTAndTransformToGPRel64:
    return "RequestDescriptorGOTAndTransformToGPRel64";
  case RequestDescriptorGOTAndTransformToGPRel64I:
    return "RequestDescriptorGOTAndTransformToGPRel64I";
  }
  return getGenericEdgeKindName(K);
}

} // namespace llvm::jitlink::ia64

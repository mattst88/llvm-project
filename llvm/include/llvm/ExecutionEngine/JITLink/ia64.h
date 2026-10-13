//===---- ia64.h - Generic JITLink ia64 edge kinds, utilities ---*- C++ -*-===//
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

#ifndef LLVM_EXECUTIONENGINE_JITLINK_IA64_H
#define LLVM_EXECUTIONENGINE_JITLINK_IA64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"

namespace llvm::jitlink::ia64 {

/// Represents ia64 fixups and other ia64-specific edge kinds.
///
/// An instruction occupies one of the three 41-bit slots of a 16-byte bundle.
/// An instruction fixup is at the bundle address plus the slot number;
/// Bundle in the expressions below is the bundle address.
enum EdgeKind_ia64 : Edge::Kind {

  /// A plain 64-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint64
  ///
  Pointer64 = Edge::FirstRelocation,

  /// A plain 32-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint32
  ///
  /// Errors:
  ///   - The result must fit in 32 bits, signed or unsigned.
  ///
  Pointer32,

  /// A 64-bit delta.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int64
  ///
  Delta64,

  /// A 32-bit delta.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result must be an in-range int32.
  ///
  Delta32,

  /// The 14-bit immediate of an adds.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : int14
  ///
  /// Errors:
  ///   - The result must be an in-range int14.
  ///
  Imm14,

  /// The 22-bit immediate of an addl.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : int22
  ///
  /// Errors:
  ///   - The result must be an in-range int22.
  ///
  Imm22,

  /// The 64-bit immediate of a movl, which occupies slots 1 and 2 of its
  /// bundle.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint64
  ///
  Imm64,

  /// A bundle-relative displacement in the immediate of an addl.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Bundle + Addend : int22
  ///
  /// Errors:
  ///   - The result must be an in-range int22.
  ///
  PCRel22,

  /// A bundle-relative displacement in the immediate of a movl.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Bundle + Addend : int64
  ///
  PCRel64I,

  /// A GP-relative displacement in the immediate of an addl.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GP + Addend : int22
  ///
  /// Errors:
  ///   - The result must be an in-range int22.
  ///
  GPRel22,

  /// A GP-relative displacement in the immediate of a movl.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GP + Addend : int64
  ///
  GPRel64I,

  /// A 32-bit GP-relative displacement.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GP + Addend : int32
  ///
  /// Errors:
  ///   - The result must be an in-range int32.
  ///
  GPRel32,

  /// A 64-bit GP-relative displacement.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GP + Addend : int64
  ///
  GPRel64,

  /// The GP value, as in the second word of a function descriptor. The edge
  /// target is ignored.
  ///
  /// Fixup expression:
  ///   Fixup <- GP : uint64
  ///
  GlobalPointer64,

  /// The displacement of a call or branch, in bundles.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Bundle + Addend) >> 4 : int21
  ///
  /// Errors:
  ///   - The result must be an in-range int21.
  ///   - The target must be at a bundle.
  ///
  PCRel21B,

  /// The displacement from a speculation check to its recovery code, encoded
  /// in different instruction bits than a branch displacement.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Bundle + Addend) >> 4 : int21
  ///
  /// Errors:
  ///   - The result must be an in-range int21.
  ///   - The target must be at a bundle.
  ///
  PCRel21M,

  /// The displacement of a floating-point check, encoded in yet other bits.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Bundle + Addend) >> 4 : int21
  ///
  /// Errors:
  ///   - The result must be an in-range int21.
  ///   - The target must be at a bundle.
  ///
  PCRel21F,

  /// The displacement of a long branch (brl), which occupies slots 1 and 2 of
  /// its bundle and has unlimited range.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Bundle + Addend) >> 4 : int60
  ///
  /// Errors:
  ///   - The target must be at a bundle.
  ///
  PCRel60B,

  /// A displacement from the lowest address of the graph, which serves as
  /// the segment base that an object's unwind table is relative to.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - SegmentBase + Addend : int64
  ///
  SegRel64,

  /// The 32-bit form of SegRel64.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - SegmentBase + Addend : uint32
  ///
  /// Errors:
  ///   - The result must be an in-range uint32.
  ///
  SegRel32,

  /// An in-place function descriptor: the function address followed by the
  /// GP to enter it with. Used in virtual tables.
  ///
  /// Fixup expression:
  ///   Fixup     <- Target + Addend : uint64
  ///   Fixup + 8 <- GP : uint64
  ///
  FunctionDescriptor,

  /// The address of the target's function descriptor.
  ///
  /// This is not a fixup. The descriptor-building pass retargets the edge at
  /// the descriptor and changes it to Pointer64.
  ///
  RequestDescriptor64,

  /// The address of the target's function descriptor, as the immediate of a
  /// movl.
  ///
  /// This is not a fixup. It becomes an Imm64 to the descriptor.
  ///
  RequestDescriptorImm64,

  /// A linkage table entry for the target, addressed GP-relative by an addl.
  ///
  /// This is not a fixup. The linkage-table-building pass retargets the edge
  /// at the entry and changes it to GPRel22.
  ///
  RequestGOTAndTransformToGPRel22,

  /// As above, but the ld8 that loads the entry is marked by a
  /// LinkageTableLoad so the pair can be relaxed together.
  ///
  /// This is not a fixup. The linkage-table-building pass retargets the edge
  /// at the entry and changes it to GPRel22Relaxable.
  ///
  RequestGOTAndTransformToGPRel22Relaxable,

  /// A GPRel22 to a linkage table entry. If the entry's value is within
  /// GPRel22 range of GP, relaxation retargets the edge at that value and
  /// changes it to GPRel22; otherwise it is applied as a GPRel22.
  ///
  GPRel22Relaxable,

  /// Marks the ld8 that loads the target's linkage table entry, whose address
  /// came from a GPRel22Relaxable. If that edge was relaxed, the register
  /// already holds the target address and relaxation turns the load into a
  /// move.
  ///
  /// The fixup is a no-op.
  ///
  LinkageTableLoad,

  /// A linkage table entry for the target, addressed by a movl.
  ///
  /// This is not a fixup. It becomes a GPRel64I to the entry.
  ///
  RequestGOTAndTransformToGPRel64I,

  /// A linkage table entry for the target's function descriptor, addressed
  /// by an addl.
  ///
  /// This is not a fixup. It becomes a RequestGOTAndTransformToGPRel22 to the
  /// descriptor.
  ///
  RequestDescriptorGOTAndTransformToGPRel22,

  /// A linkage table entry for the target's function descriptor, addressed
  /// by a 64-bit data word.
  ///
  /// This is not a fixup. It becomes a GPRel64 to an entry for the
  /// descriptor.
  ///
  RequestDescriptorGOTAndTransformToGPRel64,

  /// A linkage table entry for the target's function descriptor, addressed
  /// by a movl.
  ///
  /// This is not a fixup. It becomes a RequestGOTAndTransformToGPRel64I to
  /// the descriptor.
  ///
  RequestDescriptorGOTAndTransformToGPRel64I,

  /// A function descriptor to call the target through, addressed GP-relative
  /// by an addl. The descriptor must be within GP range, which that of a
  /// function in another graph is not, so for such a function it is a
  /// descriptor for a stub.
  ///
  /// This is not a fixup. It becomes a GPRel22 to the descriptor.
  ///
  RequestCallDescriptorAndTransformToGPRel22,

  /// The same, addressed by a movl. It becomes a GPRel64I.
  ///
  RequestCallDescriptorAndTransformToGPRel64I,

  /// The same, as a 64-bit data word. It becomes a GPRel64.
  ///
  RequestCallDescriptorAndTransformToGPRel64,
};

/// Returns a string name for the given ia64 edge. For debugging purposes
/// only.
LLVM_ABI const char *getEdgeKindName(Edge::Kind K);

/// The size of a bundle and of a function descriptor.
constexpr uint64_t BundleSize = 16;
constexpr uint64_t DescriptorSize = 16;
constexpr uint64_t PointerSize = 8;

/// Returns slot \p Slot of the bundle at \p Bundle.
inline uint64_t readSlot(const char *Bundle, unsigned Slot) {
  constexpr uint64_t SlotMask = (uint64_t(1) << 41) - 1;
  uint64_t Lo = support::endian::read64le(Bundle);
  uint64_t Hi = support::endian::read64le(Bundle + 8);
  switch (Slot) {
  case 0:
    return (Lo >> 5) & SlotMask;
  case 1:
    return ((Lo >> 46) | (Hi << 18)) & SlotMask;
  default:
    return Hi >> 23;
  }
}

inline void writeSlot(char *Bundle, unsigned Slot, uint64_t Insn) {
  constexpr uint64_t SlotMask = (uint64_t(1) << 41) - 1;
  uint64_t Lo = support::endian::read64le(Bundle);
  uint64_t Hi = support::endian::read64le(Bundle + 8);
  switch (Slot) {
  case 0:
    Lo = (Lo & ~(SlotMask << 5)) | (Insn << 5);
    break;
  case 1:
    Lo = (Lo & ~(SlotMask << 46)) | (Insn << 46);
    Hi = (Hi & ~(SlotMask >> 18)) | (Insn >> 18);
    break;
  default:
    Hi = (Hi & ~(SlotMask << 23)) | (Insn << 23);
    break;
  }
  support::endian::write64le(Bundle, Lo);
  support::endian::write64le(Bundle + 8, Hi);
}

/// Bits [From, From + Width) of V, moved to bit To of an instruction.
constexpr uint64_t field(uint64_t V, unsigned From, unsigned Width,
                         unsigned To) {
  return ((V >> From) & ((uint64_t(1) << Width) - 1)) << To;
}

/// Encoders that scatter an immediate into its instruction fields.
constexpr uint64_t encodeImm14(uint64_t V) {
  return field(V, 13, 1, 36) | field(V, 7, 6, 27) | field(V, 0, 7, 13);
}
constexpr uint64_t encodeImm22(uint64_t V) {
  return field(V, 21, 1, 36) | field(V, 16, 5, 22) | field(V, 7, 9, 27) |
         field(V, 0, 7, 13);
}
/// Slot 2 of a movl. Slot 1 holds bits 22-62.
constexpr uint64_t encodeImm64X(uint64_t V) {
  return field(V, 63, 1, 36) | field(V, 21, 1, 21) | field(V, 16, 5, 22) |
         field(V, 7, 9, 27) | field(V, 0, 7, 13);
}
constexpr uint64_t encodePCRel21B(uint64_t V) {
  return field(V, 24, 1, 36) | field(V, 4, 20, 13);
}
constexpr uint64_t encodePCRel21M(uint64_t V) {
  return field(V, 24, 1, 36) | field(V, 11, 13, 20) | field(V, 4, 7, 6);
}
constexpr uint64_t encodePCRel21F(uint64_t V) {
  return field(V, 24, 1, 36) | field(V, 4, 20, 6);
}
/// Slot 2 of a brl. Slot 1 holds displacement bits 24-62, starting at its
/// bit 2.
constexpr uint64_t encodePCRel60X(uint64_t V) {
  return field(V, 63, 1, 36) | field(V, 4, 20, 13);
}

/// Replace the bits of an instruction that Mask selects.
inline void setFields(char *Bundle, unsigned Slot, uint64_t Mask,
                      uint64_t Bits) {
  writeSlot(Bundle, Slot, (readSlot(Bundle, Slot) & ~Mask) | Bits);
}

inline void setImm64(char *Bundle, uint64_t V) {
  writeSlot(Bundle, 1, field(V, 22, 41, 0));
  setFields(Bundle, 2, encodeImm64X(~uint64_t(0)), encodeImm64X(V));
}

/// Apply fixup expression for edge to block content.
///
/// \p GP is the global pointer and \p SegmentBase the lowest address in the
/// graph.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        orc::ExecutorAddr GP, orc::ExecutorAddr SegmentBase) {
  using namespace support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  auto FixupAddress = B.getAddress() + E.getOffset();
  uint64_t Target = E.getTarget().getAddress().getValue() + E.getAddend();

  // Instruction fixups are located by slot number, and IP-relative values
  // are relative to the bundle address.
  unsigned Slot = E.getOffset() & 3;
  char *Bundle = FixupPtr - Slot;
  uint64_t BundleAddress = FixupAddress.getValue() - Slot;
  auto CheckBundle = [&](bool TwoSlots) -> Error {
    if ((E.getOffset() - Slot) % BundleSize != 0 || Slot == 3 ||
        (TwoSlots && Slot != 1) ||
        E.getOffset() - Slot + BundleSize > B.getSize())
      return make_error<JITLinkError>(
          "In graph " + G.getName() + ", section " + B.getSection().getName() +
          ": " + getEdgeKindName(E.getKind()) + " at " +
          formatv("{0:x}", FixupAddress.getValue()) +
          " is not at an instruction that can have it");
    return Error::success();
  };

  // Data fixups must lie within their block.
  auto CheckWord = [&](unsigned Size) -> Error {
    if (uint64_t(E.getOffset()) + Size > B.getSize())
      return make_error<JITLinkError>(
          "In graph " + G.getName() + ", section " + B.getSection().getName() +
          ": " + getEdgeKindName(E.getKind()) + " at " +
          formatv("{0:x}", FixupAddress.getValue()) +
          " does not fit in its block");
    return Error::success();
  };

  switch (E.getKind()) {
  case Pointer64:
  case Delta64:
  case GPRel64:
  case GlobalPointer64:
  case SegRel64:
    if (auto Err = CheckWord(8))
      return Err;
    break;
  case Pointer32:
  case Delta32:
  case GPRel32:
  case SegRel32:
    if (auto Err = CheckWord(4))
      return Err;
    break;
  default:
    break;
  }

  switch (E.getKind()) {
  case Pointer64:
    endian::write64le(FixupPtr, Target);
    break;
  case Pointer32:
    if (!isUInt<32>(Target) && !isInt<32>(static_cast<int64_t>(Target)))
      return makeTargetOutOfRangeError(G, B, E);
    endian::write32le(FixupPtr, static_cast<uint32_t>(Target));
    break;
  case Delta64:
    endian::write64le(FixupPtr, Target - FixupAddress.getValue());
    break;
  case Delta32: {
    int64_t Value = Target - FixupAddress.getValue();
    if (!isInt<32>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    endian::write32le(FixupPtr, static_cast<uint32_t>(Value));
    break;
  }
  case GPRel64:
    endian::write64le(FixupPtr, Target - GP.getValue());
    break;
  case GPRel32: {
    int64_t Value = Target - GP.getValue();
    if (!isInt<32>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    endian::write32le(FixupPtr, static_cast<uint32_t>(Value));
    break;
  }
  case GlobalPointer64:
    endian::write64le(FixupPtr, GP.getValue());
    break;
  case SegRel64:
    endian::write64le(FixupPtr, Target - SegmentBase.getValue());
    break;
  case SegRel32: {
    uint64_t Value = Target - SegmentBase.getValue();
    if (!isUInt<32>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    endian::write32le(FixupPtr, static_cast<uint32_t>(Value));
    break;
  }
  case FunctionDescriptor:
    if (E.getOffset() + DescriptorSize > B.getSize())
      return make_error<JITLinkError>(
          "In graph " + G.getName() + ", section " + B.getSection().getName() +
          ": a function descriptor at " +
          formatv("{0:x}", FixupAddress.getValue()) +
          " does not fit in its block");
    endian::write64le(FixupPtr, Target);
    endian::write64le(FixupPtr + 8, GP.getValue());
    break;

  case Imm14: {
    if (auto Err = CheckBundle(false))
      return Err;
    if (!isInt<14>(static_cast<int64_t>(Target)))
      return makeTargetOutOfRangeError(G, B, E);
    setFields(Bundle, Slot, encodeImm14(~uint64_t(0)), encodeImm14(Target));
    break;
  }
  case LinkageTableLoad:
    break;
  case Imm22:
  case GPRel22:
  case GPRel22Relaxable: {
    if (auto Err = CheckBundle(false))
      return Err;
    int64_t Value = E.getKind() == Imm22 ? Target : Target - GP.getValue();
    if (!isInt<22>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    setFields(Bundle, Slot, encodeImm22(~uint64_t(0)), encodeImm22(Value));
    break;
  }
  case PCRel22: {
    if (auto Err = CheckBundle(false))
      return Err;
    int64_t Value = Target - BundleAddress;
    if (!isInt<22>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    setFields(Bundle, Slot, encodeImm22(~uint64_t(0)), encodeImm22(Value));
    break;
  }
  case Imm64:
  case GPRel64I:
  case PCRel64I:
    if (auto Err = CheckBundle(true))
      return Err;
    setImm64(Bundle, E.getKind() == Imm64      ? Target
                     : E.getKind() == GPRel64I ? Target - GP.getValue()
                                               : Target - BundleAddress);
    break;
  case PCRel21B:
  case PCRel21M:
  case PCRel21F: {
    if (auto Err = CheckBundle(false))
      return Err;
    int64_t Value = Target - BundleAddress;
    if (Value % static_cast<int64_t>(BundleSize) != 0)
      return make_error<JITLinkError>(
          "In graph " + G.getName() + ", section " + B.getSection().getName() +
          ": the target of the branch at " +
          formatv("{0:x}", FixupAddress.getValue()) + " is not a bundle");
    if (!isInt<25>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    if (E.getKind() == PCRel21B)
      setFields(Bundle, Slot, encodePCRel21B(~uint64_t(0)),
                encodePCRel21B(Value));
    else if (E.getKind() == PCRel21M)
      setFields(Bundle, Slot, encodePCRel21M(~uint64_t(0)),
                encodePCRel21M(Value));
    else
      setFields(Bundle, Slot, encodePCRel21F(~uint64_t(0)),
                encodePCRel21F(Value));
    break;
  }
  case PCRel60B: {
    if (auto Err = CheckBundle(true))
      return Err;
    uint64_t Value = Target - BundleAddress;
    if (Value % BundleSize != 0)
      return make_error<JITLinkError>(
          "In graph " + G.getName() + ", section " + B.getSection().getName() +
          ": the target of the branch at " +
          formatv("{0:x}", FixupAddress.getValue()) + " is not a bundle");
    setFields(Bundle, 1, field(~uint64_t(0), 0, 39, 2),
              field(Value, 24, 39, 2));
    setFields(Bundle, 2, encodePCRel60X(~uint64_t(0)), encodePCRel60X(Value));
    break;
  }
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }

  return Error::success();
}

/// Relax the linkage table loads marked by the object. Where the target of a
/// GPRel22Relaxable is within GPRel22 range of \p GP, the addl computes the
/// target address directly and the paired LinkageTableLoad ld8 becomes a
/// move. Both edges name the same target, so they agree.
LLVM_ABI Error relaxLinkageTableLoads(LinkGraph &G, orc::ExecutorAddr GP);

/// The address range the graph occupies, from its lowest address.
/// Segment-relative displacements, including those in the unwind table, are
/// relative to its start.
LLVM_ABI orc::ExecutorAddrRange getSegmentRange(LinkGraph &G);

/// The GP of the graph: the linkage table, or the segment start if there is
/// none.
LLVM_ABI orc::ExecutorAddr getGlobalPointer(LinkGraph &G);

/// The section that gatherUnwindTables places the graph's unwind table in.
inline StringRef getUnwindTableSectionName() { return ".IA_64.unwind"; }

/// An unwind table entry: function start, function end and unwind info
/// address, each segment-relative.
constexpr uint64_t UnwindTableEntrySize = 24;

/// Merge the per-code-section unwind tables of the graph into one section so
/// they form a single table in memory, and keep each table alive with the
/// code it describes, since nothing else references it. A pre-prune pass.
LLVM_ABI Error gatherUnwindTables(LinkGraph &G);

/// Sort the unwind table entries by function address, as unwinders that
/// search the table require. A post-fixup pass.
LLVM_ABI Error sortUnwindTable(LinkGraph &G);

LLVM_ABI extern const char NullPointerContent[PointerSize];
LLVM_ABI extern const char NullDescriptorContent[DescriptorSize];

/// A linkage table entry: a word that holds an address.
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  auto &B = G.createContentBlock(PointerSection, NullPointerContent,
                                 orc::ExecutorAddr(), PointerSize, 0);
  if (InitialTarget)
    B.addEdge(Pointer64, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, PointerSize, false, false);
}

/// A function descriptor for the function at \p Entry: its address and the
/// GP of the graph containing it.
inline Block &createDescriptorBlock(LinkGraph &G, Section &DescriptorSection,
                                    Symbol &Entry, int64_t Addend = 0) {
  auto &B = G.createContentBlock(DescriptorSection, NullDescriptorContent,
                                 orc::ExecutorAddr(), DescriptorSize, 0);
  B.addEdge(Pointer64, 0, Entry, Addend);
  B.addEdge(GlobalPointer64, 8, Entry, 0);
  return B;
}

/// A stub calls a function known only by its descriptor, such as a function
/// in another graph. It loads the descriptor address from a linkage table
/// entry, then the function address and GP from the descriptor. A caller
/// that does not know its callee restores its own GP after the call.
///
///   addl r15 = @ltoff(descriptor), r1 ;;
///   ld8 r15 = [r15]
///   nop.i 0 ;;
///   ld8 r16 = [r15], 8 ;;
///   ld8 r1 = [r15]
///   mov b6 = r16 ;;
///   nop.m 0
///   nop.i 0
///   br b6 ;;
constexpr size_t StubEntrySize = 48;
LLVM_ABI extern const uint8_t StubContent[StubEntrySize];

inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol) {
  auto &B = G.createContentBlock(
      StubSection,
      ArrayRef<char>(reinterpret_cast<const char *>(StubContent),
                     StubEntrySize),
      orc::ExecutorAddr(), BundleSize, 0);
  B.addEdge(GPRel22, 0, PointerSymbol, 0);
  return G.addAnonymousSymbol(B, 0, StubEntrySize, true, false);
}

/// Target flags for IA-64 graph symbols.
enum TargetFlags_ia64 : TargetFlagsType {
  NoTargetFlags = 0,
  /// The symbol is exported via a function descriptor even though it is not
  /// of function type.
  DescriptorFlag = 1 << 0,
};

/// Function descriptor builder.
///
/// A function's address, both within a program and as seen from another
/// graph, is the address of its descriptor. So an externally visible function
/// is exported by its descriptor: its symbol is moved to the descriptor, and
/// references in its own graph that meant the code are pointed at the code.
class DescriptorManager {
public:
  static StringRef getSectionName() { return "$__OPD"; }

  /// Give every externally visible function a descriptor and move its name to
  /// it.
  void exportDescriptors(LinkGraph &G) {
    std::vector<Symbol *> Functions;
    for (auto *Sym : G.defined_symbols())
      if ((Sym->isCallable() || (Sym->getTargetFlags() & DescriptorFlag)) &&
          Sym->hasName() && Sym->getScope() != Scope::Local)
        Functions.push_back(Sym);
    for (auto *Sym : Functions) {
      Symbol &Code = G.addAnonymousSymbol(Sym->getBlock(), Sym->getOffset(),
                                          Sym->getSize(), true, Sym->isLive());
      Block &Descriptor = createDescriptorBlock(G, getSection(G), Code);
      G.transferDefinedSymbol(*Sym, Descriptor, 0, DescriptorSize);
      CodeOfExported[Sym] = &Code;
    }
  }

  /// The code symbol of a function whose symbol was moved to its descriptor;
  /// any other symbol is returned unchanged.
  Symbol &getCode(Symbol &Sym) {
    auto It = CodeOfExported.find(&Sym);
    return It == CodeOfExported.end() ? Sym : *It->second;
  }

  /// Retarget an edge that requests its target's descriptor. A function in
  /// another graph already is its descriptor, as is an exported function. A
  /// non-exported function gets a local descriptor here; if it was
  /// referenced as a section symbol plus offset, the offset goes into the
  /// descriptor.
  void setDescriptorTarget(LinkGraph &G, Edge &E) {
    Symbol &Sym = E.getTarget();
    if (!Sym.isDefined() || CodeOfExported.count(&Sym))
      return;
    auto &Entry = LocalDescriptors[{&Sym, E.getAddend()}];
    if (!Entry)
      Entry = &G.addAnonymousSymbol(
          createDescriptorBlock(G, getSection(G), Sym, E.getAddend()), 0,
          DescriptorSize, false, false);
    E.setTarget(*Entry);
    E.setAddend(0);
  }

  /// Retarget an edge that requests a descriptor to call its target through.
  /// The descriptor is addressed GP-relative, so for a function in another
  /// graph a local one is created here, which the stub builder turns into a
  /// descriptor for a stub.
  void setCallDescriptorTarget(LinkGraph &G, Edge &E) {
    Symbol &Sym = E.getTarget();
    if (Sym.isDefined())
      return setDescriptorTarget(G, E);
    auto &Entry = ImportedDescriptors[&Sym];
    if (!Entry) {
      auto &B = G.createContentBlock(getSection(G), NullDescriptorContent,
                                     orc::ExecutorAddr(), DescriptorSize, 0);
      B.addEdge(FunctionDescriptor, 0, Sym, 0);
      Entry = &G.addAnonymousSymbol(B, 0, DescriptorSize, false, false);
    }
    E.setTarget(*Entry);
    E.setAddend(0);
  }

private:
  Section &getSection(LinkGraph &G) {
    if (!DescriptorSection)
      DescriptorSection = &G.createSection(
          getSectionName(), orc::MemProt::Read | orc::MemProt::Write);
    return *DescriptorSection;
  }

  Section *DescriptorSection = nullptr;
  DenseMap<Symbol *, Symbol *> CodeOfExported;
  DenseMap<std::pair<Symbol *, int64_t>, Symbol *> LocalDescriptors;
  DenseMap<Symbol *, Symbol *> ImportedDescriptors;
};

/// Linkage table builder.
class GOTTableManager : public TableManager<GOTTableManager> {
public:
  static StringRef getSectionName() { return "$__GOT"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind KindToSet;
    switch (E.getKind()) {
    case RequestGOTAndTransformToGPRel22:
      KindToSet = GPRel22;
      break;
    case RequestGOTAndTransformToGPRel22Relaxable:
      KindToSet = GPRel22Relaxable;
      break;
    case RequestGOTAndTransformToGPRel64I:
      KindToSet = GPRel64I;
      break;
    case RequestDescriptorGOTAndTransformToGPRel64:
      KindToSet = GPRel64;
      break;
    default:
      return false;
    }
    // The entry holds target plus addend; the edge refers to the entry with
    // no addend.
    E.setTarget(getEntryForTargetAndAddend(G, E.getTarget(), E.getAddend()));
    E.setAddend(0);
    E.setKind(KindToSet);
    return true;
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointer(G, getGOTSection(G), &Target);
  }

private:
  // TableManager keys entries by target name, with no addend. Section
  // symbols have no name, so those entries, and entries with an addend, are
  // kept here.
  Symbol &getEntryForTargetAndAddend(LinkGraph &G, Symbol &Target,
                                     int64_t Addend) {
    if (Addend == 0 && Target.hasName())
      return getEntryForTarget(G, Target);
    auto &Entry = AddendEntries[{&Target, Addend}];
    if (!Entry)
      Entry = &createAnonymousPointer(G, getGOTSection(G), &Target, Addend);
    return *Entry;
  }

  Section &getGOTSection(LinkGraph &G) {
    if (!GOTSection)
      GOTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Write);
    return *GOTSection;
  }

  Section *GOTSection = nullptr;
  DenseMap<std::pair<Symbol *, int64_t>, Symbol *> AddendEntries;
};

/// Stub builder.
class PLTTableManager : public TableManager<PLTTableManager> {
public:
  PLTTableManager(GOTTableManager &GOT) : GOT(GOT) {}

  static StringRef getSectionName() { return "$__STUBS"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    // A function in the graph is called directly, with the caller's GP. A
    // function in another graph is called through a stub, and an in-place
    // descriptor for one becomes a descriptor for the stub.

    if (E.getTarget().isDefined())
      return false;
    switch (E.getKind()) {
    case PCRel21B:
    case PCRel60B:
    case FunctionDescriptor:
      E.setTarget(getEntryForTarget(G, E.getTarget()));
      return true;
    default:
      return false;
    }
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointerJumpStub(G, getStubsSection(G),
                                          GOT.getEntryForTarget(G, Target));
  }

private:
  Section &getStubsSection(LinkGraph &G) {
    if (!StubsSection)
      StubsSection = &G.createSection(getSectionName(),
                                      orc::MemProt::Read | orc::MemProt::Exec);
    return *StubsSection;
  }

  GOTTableManager &GOT;
  Section *StubsSection = nullptr;
};

} // namespace llvm::jitlink::ia64

#endif // LLVM_EXECUTIONENGINE_JITLINK_IA64_H

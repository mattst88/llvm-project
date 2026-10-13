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
  case SecRel64:
    return "SecRel64";
  case SecRel32:
    return "SecRel32";
  case FunctionDescriptor:
    return "FunctionDescriptor";
  case RequestDescriptor64:
    return "RequestDescriptor64";
  case RequestDescriptorImm64:
    return "RequestDescriptorImm64";
  case RequestGOTAndTransformToGPRel22:
    return "RequestGOTAndTransformToGPRel22";
  case RequestGOTAndTransformToGPRel22Relaxable:
    return "RequestGOTAndTransformToGPRel22Relaxable";
  case GPRel22Relaxable:
    return "GPRel22Relaxable";
  case LinkageTableLoad:
    return "LinkageTableLoad";
  case RequestGOTAndTransformToGPRel64I:
    return "RequestGOTAndTransformToGPRel64I";
  case RequestDescriptorGOTAndTransformToGPRel22:
    return "RequestDescriptorGOTAndTransformToGPRel22";
  case RequestDescriptorGOTAndTransformToGPRel64:
    return "RequestDescriptorGOTAndTransformToGPRel64";
  case RequestDescriptorGOTAndTransformToGPRel64I:
    return "RequestDescriptorGOTAndTransformToGPRel64I";
  case RequestCallDescriptorAndTransformToGPRel22:
    return "RequestCallDescriptorAndTransformToGPRel22";
  case RequestCallDescriptorAndTransformToGPRel64I:
    return "RequestCallDescriptorAndTransformToGPRel64I";
  case RequestCallDescriptorAndTransformToGPRel64:
    return "RequestCallDescriptorAndTransformToGPRel64";
  }
  return getGenericEdgeKindName(K);
}

orc::ExecutorAddrRange getSegmentRange(LinkGraph &G) {
  orc::ExecutorAddrRange R;
  bool Any = false;
  for (Section &Sec : G.sections()) {
    // Non-allocated sections have no address.
    if (Sec.getMemLifetime() == orc::MemLifetime::NoAlloc)
      continue;
    for (Block *B : Sec.blocks()) {
      if (!Any || B->getAddress() < R.Start)
        R.Start = B->getAddress();
      if (!Any || B->getAddress() + B->getSize() > R.End)
        R.End = B->getAddress() + B->getSize();
      Any = true;
    }
  }
  return R;
}

orc::ExecutorAddr getGlobalPointer(LinkGraph &G) {
  if (auto *GOTSection =
          G.findSectionByName(GOTTableManager::getSectionName())) {
    SectionRange SR(*GOTSection);
    if (!SR.empty())
      return SR.getStart();
  }
  return getSegmentRange(G).Start;
}

static bool isUnwindTableSection(StringRef Name) {
  // Each code section may have its own table, named after it.
  return Name == getUnwindTableSectionName() ||
         Name.starts_with(".IA_64.unwind.");
}

Error gatherUnwindTables(LinkGraph &G) {
  std::vector<Section *> Tables;
  for (Section &Sec : G.sections())
    if (isUnwindTableSection(Sec.getName()))
      Tables.push_back(&Sec);
  if (Tables.empty())
    return Error::success();

  Section *Gathered = G.findSectionByName(getUnwindTableSectionName());
  if (!Gathered)
    Gathered =
        &G.createSection(getUnwindTableSectionName(), orc::MemProt::Read);

  for (Section *Sec : Tables) {
    for (Block *B : Sec->blocks()) {
      if (B->getSize() % UnwindTableEntrySize != 0)
        return make_error<JITLinkError>(
            "In graph " + G.getName() + ", section " + Sec->getName() +
            ": the size of an unwind table is not a multiple of that of an "
            "entry");
      // Keep the table alive as long as any of the code it describes is.
      Symbol *Table = nullptr;
      DenseSet<Block *> Seen;
      for (Edge &E : B->edges()) {
        Symbol &Target = E.getTarget();
        if (!Target.isDefined())
          continue;
        Block &Code = Target.getBlock();
        if ((Code.getSection().getMemProt() & orc::MemProt::Exec) ==
                orc::MemProt::None ||
            !Seen.insert(&Code).second)
          continue;
        if (!Table)
          Table = &G.addAnonymousSymbol(*B, 0, B->getSize(), false, false);
        Code.addEdge(Edge::KeepAlive, 0, *Table, 0);
      }
    }
    G.mergeSections(*Gathered, *Sec);
  }
  return Error::success();
}

Error sortUnwindTable(LinkGraph &G) {
  Section *Sec = G.findSectionByName(getUnwindTableSectionName());
  if (!Sec || Sec->blocks().empty())
    return Error::success();

  std::vector<Block *> Blocks(Sec->blocks().begin(), Sec->blocks().end());
  llvm::sort(Blocks, [](const Block *L, const Block *R) {
    return L->getAddress() < R->getAddress();
  });

  struct Entry {
    uint64_t Start, End, Info;
  };
  std::vector<Entry> Entries;
  for (size_t I = 0; I != Blocks.size(); ++I) {
    Block *B = Blocks[I];
    if (I != 0 && Blocks[I - 1]->getAddress() + Blocks[I - 1]->getSize() !=
                      B->getAddress())
      return make_error<JITLinkError>(
          "In graph " + G.getName() +
          ": the unwind tables are not one table in memory");
    const char *P = B->getContent().data();
    for (size_t O = 0; O != B->getSize(); O += UnwindTableEntrySize)
      Entries.push_back({support::endian::read64le(P + O),
                         support::endian::read64le(P + O + 8),
                         support::endian::read64le(P + O + 16)});
  }

  // Entries hold no position-dependent values, so they can be reordered
  // freely within the table.
  llvm::stable_sort(Entries, [](const Entry &L, const Entry &R) {
    return L.Start < R.Start;
  });

  auto Next = Entries.begin();
  for (Block *B : Blocks) {
    char *P = B->getAlreadyMutableContent().data();
    for (size_t O = 0; O != B->getSize(); O += UnwindTableEntrySize, ++Next) {
      support::endian::write64le(P + O, Next->Start);
      support::endian::write64le(P + O + 8, Next->End);
      support::endian::write64le(P + O + 16, Next->Info);
    }
  }
  return Error::success();
}

Error relaxLinkageTableLoads(LinkGraph &G, orc::ExecutorAddr GP) {
  auto IsNear = [&](const Symbol &Target, int64_t Addend) {
    return isInt<22>(static_cast<int64_t>(Target.getAddress().getValue() +
                                          Addend - GP.getValue()));
  };

  for (Block *B : G.blocks())
    for (Edge &E : B->edges()) {
      if (E.getKind() == GPRel22Relaxable) {
        // A linkage table entry holds one address, described by its only
        // edge.
        Block &Entry = E.getTarget().getBlock();
        assert(Entry.edges_size() == 1 && "Not a linkage table entry");
        Edge &Held = *Entry.edges().begin();
        if (IsNear(Held.getTarget(), Held.getAddend())) {
          E.setTarget(Held.getTarget());
          E.setAddend(Held.getAddend());
        }
        E.setKind(GPRel22);
        continue;
      }

      if (E.getKind() != LinkageTableLoad ||
          !IsNear(E.getTarget(), E.getAddend()))
        continue;

      unsigned Slot = E.getOffset() & 3;
      Edge::OffsetT BundleOffset = E.getOffset() - Slot;
      uint64_t Insn = 0;
      bool IsLoad = BundleOffset % BundleSize == 0 && Slot != 3 &&
                    BundleOffset + BundleSize <= B->getSize();
      if (IsLoad) {
        Insn = readSlot(B->getContent().data() + BundleOffset, Slot);
        // ld8 r1 = [r3], with any hint and no speculation.
        IsLoad = (Insn & 0x1ffc8000000) == 0x080c0000000;
      }
      if (!IsLoad)
        return make_error<JITLinkError>(
            "In graph " + G.getName() + ", section " +
            B->getSection().getName() + ": " +
            getEdgeKindName(E.getKind()) + " at " +
            formatv("{0:x}", (B->getAddress() + E.getOffset()).getValue()) +
            " is not at a load of eight bytes");

      // The register holds the target address rather than the entry's, so
      // the load becomes a move, or a nop if r1 == r3.

      uint64_t QP = Insn & 0x3f;
      uint64_t R1 = (Insn >> 6) & 0x7f;
      uint64_t R3 = (Insn >> 20) & 0x7f;
      uint64_t Move = R1 == R3 ? uint64_t(1) << 27 // nop.m 0
                               : uint64_t(8) << 37 | uint64_t(2) << 34 |
                                     R3 << 20 | R1 << 6 | QP; // adds r1 = 0, r3
      writeSlot(B->getMutableContent(G).data() + BundleOffset, Slot, Move);
    }
  return Error::success();
}

} // namespace llvm::jitlink::ia64

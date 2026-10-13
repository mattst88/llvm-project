//===------ ELF_ia64.cpp - JIT linker implementation for ELF/ia64 ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/ia64 jit-link implementation.
//
// A graph is linked like a standalone program: it has a single GP, a linkage
// table addressed from GP, and a function descriptor for each function whose
// address is taken or that another graph can call. Its unwind tables are
// merged into one, which IA64UnwindTableRegistrationPlugin registers with the
// unwinder. TLS is not supported.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_ia64.h"
#include "llvm/ExecutionEngine/JITLink/ia64.h"
#include "llvm/Object/ELFObjectFile.h"

#include "DefineExternalSectionStartAndEndSymbols.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {

constexpr StringRef ELFGlobalPointerSymbolName = "__gp";

Error buildTables_ELF_ia64(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  // Build the descriptors first, since linkage table entries can hold
  // descriptor addresses.
  ia64::DescriptorManager Descriptors;
  Descriptors.exportDescriptors(G);

  std::vector<Block *> Blocks(G.blocks().begin(), G.blocks().end());
  for (Block *B : Blocks)
    for (Edge &E : B->edges()) {
      switch (E.getKind()) {
      case ia64::RequestDescriptor64:
        Descriptors.setDescriptorTarget(G, E);
        E.setKind(ia64::Pointer64);
        break;
      case ia64::RequestDescriptorImm64:
        Descriptors.setDescriptorTarget(G, E);
        E.setKind(ia64::Imm64);
        break;
      case ia64::RequestDescriptorGOTAndTransformToGPRel22:
        Descriptors.setDescriptorTarget(G, E);
        E.setKind(ia64::RequestGOTAndTransformToGPRel22);
        break;
      case ia64::RequestDescriptorGOTAndTransformToGPRel64I:
        Descriptors.setDescriptorTarget(G, E);
        E.setKind(ia64::RequestGOTAndTransformToGPRel64I);
        break;
      case ia64::RequestDescriptorGOTAndTransformToGPRel64:
        Descriptors.setDescriptorTarget(G, E);
        break;
      case ia64::RequestCallDescriptorAndTransformToGPRel22:
        Descriptors.setCallDescriptorTarget(G, E);
        E.setKind(ia64::GPRel22);
        break;
      case ia64::RequestCallDescriptorAndTransformToGPRel64I:
        Descriptors.setCallDescriptorTarget(G, E);
        E.setKind(ia64::GPRel64I);
        break;
      case ia64::RequestCallDescriptorAndTransformToGPRel64:
        Descriptors.setCallDescriptorTarget(G, E);
        E.setKind(ia64::GPRel64);
        break;
      default:
        // Any other relocation against a function refers to its code.
        if (E.isRelocation())
          E.setTarget(Descriptors.getCode(E.getTarget()));
        break;
      }
    }

  ia64::GOTTableManager GOT;
  ia64::PLTTableManager PLT(GOT);
  visitExistingEdges(G, GOT, PLT);
  return Error::success();
}

} // namespace

namespace llvm::jitlink {

class ELFJITLinker_ia64 : public JITLinker<ELFJITLinker_ia64> {
  friend class JITLinker<ELFJITLinker_ia64>;

public:
  ELFJITLinker_ia64(std::unique_ptr<JITLinkContext> Ctx,
                    std::unique_ptr<LinkGraph> G, PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {
    getPassConfig().PostAllocationPasses.push_back(
        [this](LinkGraph &G) { return defineGlobalPointer(G); });
  }

private:
  orc::ExecutorAddr GlobalPointer;
  orc::ExecutorAddr SegmentBase;

  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return ia64::applyFixup(G, B, E, GlobalPointer, SegmentBase);
  }

  // GP points at the linkage table, or at the lowest address of the graph if
  // there is none. GP-relative references must be within 2 MiB of it or the
  // fixup fails.
  Error defineGlobalPointer(LinkGraph &G) {
    bool Any = false;
    for (Block *B : G.blocks())
      if (!Any || B->getAddress() < SegmentBase) {
        SegmentBase = B->getAddress();
        Any = true;
      }
    GlobalPointer = SegmentBase;
    if (auto *GOTSection =
            G.findSectionByName(ia64::GOTTableManager::getSectionName())) {
      SectionRange SR(*GOTSection);
      if (!SR.empty())
        GlobalPointer = SR.getStart();
    }

    // An object may reference GP by name.
    Symbol *Named = nullptr;
    for (Symbol *Sym : G.external_symbols())
      if (Sym->hasName() && *Sym->getName() == ELFGlobalPointerSymbolName)
        Named = Sym;
    if (Named)
      G.makeAbsolute(*Named, GlobalPointer);
    return Error::success();
  }
};

class ELFLinkGraphBuilder_ia64 : public ELFLinkGraphBuilder<object::ELF64LE> {
private:
  using ELFT = object::ELF64LE;
  using Base = ELFLinkGraphBuilder<ELFT>;
  using Base::G;

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    using Self = ELFLinkGraphBuilder_ia64;
    for (const auto &RelSect : Base::Sections) {
      if (RelSect.sh_type == ELF::SHT_REL)
        return make_error<StringError>("No SHT_REL in valid " +
                                           G->getTargetTriple().getArchName() +
                                           " ELF object files",
                                       inconvertibleErrorCode());

      if (Error Err = Base::forEachRelaRelocation(RelSect, this,
                                                  &Self::addSingleRelocation))
        return Err;
    }

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rela &Rel,
                            const typename ELFT::Shdr &FixupSect,
                            Block &BlockToFix) {
    uint32_t ELFReloc = Rel.getType(false);

    // An ldxmov marks a load from the linkage table that a linker could
    // make a move of, where what the entry holds is near. The load is left.
    if (ELFReloc == ELF::R_IA64_NONE || ELFReloc == ELF::R_IA64_LDXMOV)
      return Error::success();

    uint32_t SymbolIndex = Rel.getSymbol(false);
    auto ObjSymbol = Base::Obj.getRelocationSymbol(Rel, Base::SymTabSec);
    if (!ObjSymbol)
      return ObjSymbol.takeError();

    Symbol *GraphSymbol = Base::getGraphSymbol(SymbolIndex);
    if (!GraphSymbol)
      return make_error<StringError>(
          formatv("Could not find symbol at given index, did you add it to "
                  "JITSymbolTable? index: {0}, shndx: {1} Size of table: {2}",
                  SymbolIndex, (*ObjSymbol)->st_shndx,
                  Base::GraphSymbols.size()),
          inconvertibleErrorCode());

    int64_t Addend = Rel.r_addend;
    Edge::Kind Kind = Edge::Invalid;

    switch (ELFReloc) {
    case ELF::R_IA64_DIR64LSB:
      Kind = ia64::Pointer64;
      break;
    case ELF::R_IA64_DIR32LSB:
      Kind = ia64::Pointer32;
      break;
    case ELF::R_IA64_PCREL64LSB:
      Kind = ia64::Delta64;
      break;
    case ELF::R_IA64_PCREL32LSB:
      Kind = ia64::Delta32;
      break;
    case ELF::R_IA64_GPREL64LSB:
      Kind = ia64::GPRel64;
      break;
    case ELF::R_IA64_GPREL32LSB:
      Kind = ia64::GPRel32;
      break;
    case ELF::R_IA64_SEGREL64LSB:
      Kind = ia64::SegRel64;
      break;
    case ELF::R_IA64_SEGREL32LSB:
      Kind = ia64::SegRel32;
      break;
    case ELF::R_IA64_FPTR64LSB:
      Kind = ia64::RequestDescriptor64;
      break;
    case ELF::R_IA64_FPTR64I:
      Kind = ia64::RequestDescriptorImm64;
      break;
    case ELF::R_IA64_LTOFF_FPTR64LSB:
      Kind = ia64::RequestDescriptorGOTAndTransformToGPRel64;
      break;
    case ELF::R_IA64_IPLTLSB:
      Kind = ia64::FunctionDescriptor;
      break;
    case ELF::R_IA64_IMM14:
      Kind = ia64::Imm14;
      break;
    case ELF::R_IA64_IMM22:
      Kind = ia64::Imm22;
      break;
    case ELF::R_IA64_IMM64:
      Kind = ia64::Imm64;
      break;
    case ELF::R_IA64_GPREL22:
      Kind = ia64::GPRel22;
      break;
    case ELF::R_IA64_GPREL64I:
      Kind = ia64::GPRel64I;
      break;
    case ELF::R_IA64_LTOFF22:
    case ELF::R_IA64_LTOFF22X:
      Kind = ia64::RequestGOTAndTransformToGPRel22;
      break;
    case ELF::R_IA64_LTOFF64I:
      Kind = ia64::RequestGOTAndTransformToGPRel64I;
      break;
    case ELF::R_IA64_LTOFF_FPTR22:
      Kind = ia64::RequestDescriptorGOTAndTransformToGPRel22;
      break;
    case ELF::R_IA64_LTOFF_FPTR64I:
      Kind = ia64::RequestDescriptorGOTAndTransformToGPRel64I;
      break;
    case ELF::R_IA64_PLTOFF22:
      Kind = ia64::RequestCallDescriptorAndTransformToGPRel22;
      break;
    case ELF::R_IA64_PLTOFF64I:
      Kind = ia64::RequestCallDescriptorAndTransformToGPRel64I;
      break;
    case ELF::R_IA64_PLTOFF64LSB:
      Kind = ia64::RequestCallDescriptorAndTransformToGPRel64;
      break;
    case ELF::R_IA64_PCREL22:
      Kind = ia64::PCRel22;
      break;
    case ELF::R_IA64_PCREL64I:
      Kind = ia64::PCRel64I;
      break;
    case ELF::R_IA64_PCREL21B:
    case ELF::R_IA64_PCREL21BI:
      Kind = ia64::PCRel21B;
      break;
    case ELF::R_IA64_PCREL21M:
      Kind = ia64::PCRel21M;
      break;
    case ELF::R_IA64_PCREL21F:
      Kind = ia64::PCRel21F;
      break;
    case ELF::R_IA64_PCREL60B:
      Kind = ia64::PCRel60B;
      break;
    case ELF::R_IA64_TPREL14:
    case ELF::R_IA64_TPREL22:
    case ELF::R_IA64_TPREL64I:
    case ELF::R_IA64_TPREL64MSB:
    case ELF::R_IA64_TPREL64LSB:
    case ELF::R_IA64_LTOFF_TPREL22:
    case ELF::R_IA64_DTPMOD64MSB:
    case ELF::R_IA64_DTPMOD64LSB:
    case ELF::R_IA64_LTOFF_DTPMOD22:
    case ELF::R_IA64_DTPREL14:
    case ELF::R_IA64_DTPREL22:
    case ELF::R_IA64_DTPREL64I:
    case ELF::R_IA64_DTPREL32MSB:
    case ELF::R_IA64_DTPREL32LSB:
    case ELF::R_IA64_DTPREL64MSB:
    case ELF::R_IA64_DTPREL64LSB:
    case ELF::R_IA64_LTOFF_DTPREL22:
      // TP-relative offsets are fixed at program load time, and the modules
      // __tls_get_addr knows about are created by the dynamic loader. JIT'd
      // code has neither.
      return make_error<JITLinkError>(
          "In " + G->getName() + ": " +
          object::getELFRelocationTypeName(ELF::EM_IA_64, ELFReloc) +
          " is for thread-local storage, which is not supported for ia64" +
          (GraphSymbol->hasName()
               ? " (the variable is " + *GraphSymbol->getName() + ")"
               : Twine()) +
          "; -femulated-tls makes code that needs none");
    default:
      return make_error<JITLinkError>(
          "In " + G->getName() + ": Unsupported ia64 relocation type " +
          object::getELFRelocationTypeName(ELF::EM_IA_64, ELFReloc));
    }

    auto FixupAddress = orc::ExecutorAddr(FixupSect.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();
    Edge GE(Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, ia64::getEdgeKindName(Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));
    return Error::success();
  }

public:
  ELFLinkGraphBuilder_ia64(StringRef FileName, const object::ELFFile<ELFT> &Obj,
                           std::shared_ptr<orc::SymbolStringPool> SSP,
                           Triple TT, SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(SSP), std::move(TT),
                                  std::move(Features), FileName,
                                  ia64::getEdgeKindName) {}
};

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ia64(MemoryBufferRef ObjectBuffer,
                                  std::shared_ptr<orc::SymbolStringPool> SSP) {
  LLVM_DEBUG({
    dbgs() << "Building jitlink graph for new input "
           << ObjectBuffer.getBufferIdentifier() << "...\n";
  });

  auto ELFObj = object::ObjectFile::createELFObjectFile(ObjectBuffer);
  if (!ELFObj)
    return ELFObj.takeError();

  // An assembler also makes big-endian objects, and 32-bit ones, for other
  // operating systems.
  if (!isa<object::ELFObjectFile<object::ELF64LE>>(**ELFObj))
    return make_error<JITLinkError>(
        "In " + ObjectBuffer.getBufferIdentifier() +
        ": only little-endian 64-bit ia64 objects are supported");

  auto Features = (*ELFObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  assert((*ELFObj)->getArch() == Triple::ia64 && "Only ia64 is supported");

  auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF64LE>>(**ELFObj);
  return ELFLinkGraphBuilder_ia64((*ELFObj)->getFileName(),
                                  ELFObjFile.getELFFile(), std::move(SSP),
                                  (*ELFObj)->makeTriple(), std::move(*Features))
      .buildGraph();
}

void link_ELF_ia64(std::unique_ptr<LinkGraph> G,
                   std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Build the function descriptors, the linkage table and the stubs.

    Config.PostPrunePasses.push_back(buildTables_ELF_ia64);

    // Resolve any external section start / end symbols.
    Config.PostAllocationPasses.push_back(
        createDefineExternalSectionStartAndEndSymbolsPass(
            identifyELFSectionStartAndEndSymbols));
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_ia64::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace llvm::jitlink

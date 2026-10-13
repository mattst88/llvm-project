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
    // The linkage table entries' targets have addresses by now.
    getPassConfig().PreFixupPasses.push_back([this](LinkGraph &G) {
      return ia64::relaxLinkageTableLoads(G, GlobalPointer);
    });
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
    SegmentBase = ia64::getSegmentRange(G).Start;
    GlobalPointer = ia64::getGlobalPointer(G);

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

  // The local symbol that branches to this object's copy of a COMDAT
  // function are retargeted to.
  DenseMap<Symbol *, Symbol *> OwnDefinitions;

  // Treat untyped global labels in code as functions, since hand-written
  // assembly often omits the type. Another graph knows a symbol by a single
  // address, which for a function must be its descriptor: both a call through
  // a stub and @fptr treat it as one.
  Error markUntypedCodeLabels() {
    if (!Base::SymTabSec)
      return Error::success();
    auto Symbols = Base::Obj.symbols(Base::SymTabSec);
    if (!Symbols)
      return Symbols.takeError();
    for (ELFSymbolIndex I = 0; I != Symbols->size(); ++I) {
      const auto &Sym = (*Symbols)[I];
      if (Sym.getType() != ELF::STT_NOTYPE ||
          (Sym.getBinding() != ELF::STB_GLOBAL &&
           Sym.getBinding() != ELF::STB_WEAK) ||
          Sym.st_shndx == ELF::SHN_UNDEF || Sym.st_shndx >= ELF::SHN_LORESERVE)
        continue;
      if (!(Base::Sections[Sym.st_shndx].sh_flags & ELF::SHF_EXECINSTR))
        continue;
      if (Symbol *GraphSym = Base::getGraphSymbol(I))
        if (GraphSym->isDefined())
          GraphSym->setTargetFlags(ia64::DescriptorFlag);
    }
    return Error::success();
  }

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    if (Error Err = markUntypedCodeLabels())
      return Err;

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

    if (ELFReloc == ELF::R_IA64_NONE)
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
    case ELF::R_IA64_SECREL64LSB:
      Kind = ia64::SecRel64;
      break;
    case ELF::R_IA64_SECREL32LSB:
      Kind = ia64::SecRel32;
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
      Kind = ia64::RequestGOTAndTransformToGPRel22;
      break;
    // An LTOFF22X marks an addl whose result is used only by a load marked
    // by LDXMOV, so if the entry's value is within GP range the pair can
    // bypass the entry.
    case ELF::R_IA64_LTOFF22X:
      Kind = ia64::RequestGOTAndTransformToGPRel22Relaxable;
      break;
    case ELF::R_IA64_LDXMOV:
      Kind = ia64::LinkageTableLoad;
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

    // A COMDAT function is identical in every object that has it and all but
    // one copy are discarded, so the name may resolve to the kept copy in
    // another graph with a different GP. A compiler that assumes its own
    // definition is the callee does not restore GP after the call, so
    // branches keep using this object's copy. Taken addresses still refer to
    // the kept copy.
    if ((Kind == ia64::PCRel21B || Kind == ia64::PCRel60B) &&
        GraphSymbol->isDefined() &&
        GraphSymbol->getLinkage() == Linkage::Weak &&
        (*ObjSymbol)->st_shndx < ELF::SHN_LORESERVE &&
        (Base::Sections[(*ObjSymbol)->st_shndx].sh_flags & ELF::SHF_GROUP)) {
      Symbol *&Own = OwnDefinitions[GraphSymbol];
      if (!Own)
        Own = &G->addAnonymousSymbol(GraphSymbol->getBlock(),
                                     GraphSymbol->getOffset(),
                                     GraphSymbol->getSize(), true, false);
      GraphSymbol = Own;
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

    // Merge the unwind tables into one and keep them alive.
    Config.PrePrunePasses.push_back(ia64::gatherUnwindTables);

    // Build the function descriptors, the linkage table and the stubs.

    Config.PostPrunePasses.push_back(buildTables_ELF_ia64);

    // Resolve any external section start / end symbols.
    Config.PostAllocationPasses.push_back(
        createDefineExternalSectionStartAndEndSymbolsPass(
            identifyELFSectionStartAndEndSymbols));

    Config.PostFixupPasses.push_back(ia64::sortUnwindTable);
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_ia64::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace llvm::jitlink

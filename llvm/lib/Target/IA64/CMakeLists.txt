add_llvm_component_group(IA64)

set(LLVM_TARGET_DEFINITIONS IA64.td)

tablegen(LLVM IA64GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM IA64GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM IA64GenCallingConv.inc -gen-callingconv)
tablegen(LLVM IA64GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM IA64GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM IA64GenGlobalISel.inc -gen-global-isel)
tablegen(LLVM IA64GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM IA64GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM IA64GenRegisterBank.inc -gen-register-bank)
tablegen(LLVM IA64GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM IA64GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(IA64CommonTableGen)

add_llvm_target(IA64CodeGen
  GISel/IA64CallLowering.cpp
  GISel/IA64InstructionSelector.cpp
  GISel/IA64LegalizerInfo.cpp
  GISel/IA64RegisterBankInfo.cpp
  IA64AsmPrinter.cpp
  IA64BreakAntiDeps.cpp
  IA64DeferMathErrno.cpp
  IA64FrameLowering.cpp
  IA64ExclusivePredicates.cpp
  IA64LoadBanks.cpp
  IA64ExpandBranches.cpp
  IA64FoldPredicatedCopies.cpp
  IA64HoistFallThrough.cpp
  IA64HoistLoadUses.cpp
  IA64HoistRestores.cpp
  IA64IfConversion.cpp
  IA64LoopAliasVersioning.cpp
  IA64LoadReuse.cpp
  IA64MergeByteStores.cpp
  IA64MulRecurrence.cpp
  IA64MemmoveVersioning.cpp
  IA64InsertStops.cpp
  IA64PackGroups.cpp
  IA64LocalDynamicTLS.cpp
  IA64InstrInfo.cpp
  IA64ISelDAGToDAG.cpp
  IA64ISelLowering.cpp
  IA64RegisterInfo.cpp
  IA64RematInCallLoops.cpp
  IA64ReplicateDispatch.cpp
  IA64WidenIndices.cpp
  IA64SetfPhis.cpp
  IA64SinkPastCalls.cpp
  IA64SpeculateChases.cpp
  IA64RotatingLoops.cpp
  IA64SelectionDAGInfo.cpp
  IA64PostIncPointers.cpp
  IA64PrefetchStreams.cpp
  IA64SpeculateLoads.cpp
  IA64SplitSingleAdds.cpp
  IA64Subtarget.cpp
  IA64TargetMachine.cpp
  IA64TargetObjectFile.cpp
  IA64HoistCommonInsts.cpp
  IA64ZextLoopSteps.cpp
  IA64TargetTransformInfo.cpp
  IA64WidenByteOps.cpp
  IA64ZextElim.cpp

  LINK_COMPONENTS
  IA64Desc
  IA64Info
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  Passes
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  IA64
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)

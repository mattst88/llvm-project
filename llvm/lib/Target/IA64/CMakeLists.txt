add_llvm_component_group(IA64)

set(LLVM_TARGET_DEFINITIONS IA64.td)

tablegen(LLVM IA64GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM IA64GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM IA64GenCallingConv.inc -gen-callingconv)
tablegen(LLVM IA64GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM IA64GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM IA64GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM IA64GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM IA64GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM IA64GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(IA64CommonTableGen)

add_llvm_target(IA64CodeGen
  IA64AsmPrinter.cpp
  IA64FrameLowering.cpp
  IA64ExpandBranches.cpp
  IA64IfConversion.cpp
  IA64InsertStops.cpp
  IA64InstrInfo.cpp
  IA64ISelDAGToDAG.cpp
  IA64ISelLowering.cpp
  IA64RegisterInfo.cpp
  IA64SelectionDAGInfo.cpp
  IA64Subtarget.cpp
  IA64TargetMachine.cpp
  IA64TargetObjectFile.cpp

  LINK_COMPONENTS
  IA64Desc
  IA64Info
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  IA64
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)

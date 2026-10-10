add_llvm_component_library(LLVMIA64Disassembler
  IA64Disassembler.cpp

  LINK_COMPONENTS
  IA64Desc
  IA64Info
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  IA64
  )

add_llvm_component_library(LLVMIA64AsmParser
  IA64AsmParser.cpp

  LINK_COMPONENTS
  IA64Desc
  IA64Info
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  IA64
  )

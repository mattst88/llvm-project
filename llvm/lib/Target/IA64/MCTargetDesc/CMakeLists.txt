add_llvm_component_library(LLVMIA64Desc
  IA64InstPrinter.cpp
  IA64MCAsmInfo.cpp
  IA64MCCodeEmitter.cpp
  IA64MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  IA64Info

  ADD_TO_COMPONENT
  IA64
  )

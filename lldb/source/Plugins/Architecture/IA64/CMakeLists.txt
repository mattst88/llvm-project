add_lldb_library(lldbPluginArchitectureIA64 PLUGIN
  ArchitectureIA64.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbPluginProcessUtility
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
  )

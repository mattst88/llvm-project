add_lldb_library(lldbPluginABIIA64 PLUGIN
  ABISysV_ia64.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )

add_llvm_component_library(LLVMNVPTXAsmParser
  NVPTXAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  NVPTXDesc
  NVPTXInfo

  ADD_TO_COMPONENT
  NVPTX
  )
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/OrcMCJITReplacement.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/TargetSelect.h>


// #include "llvm/Bitcode/BitcodeReader.h"
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>
#include <vector>

using namespace std;
using namespace llvm;
using namespace llvm::orc;
using namespace llvm::object;

static LLVMContext GlobalContext;

std::unique_ptr<Module> loadModuleAtPath(const std::string &path) {
  auto bufferOrError = MemoryBuffer::getFile(path);
  if (!bufferOrError) {
    cerr << "Can't load module " << path << '\n';
    exit(1);
    return nullptr;
  }

  auto module = parseBitcodeFile(bufferOrError->get()->getMemBufferRef(), GlobalContext);
  if (!module) {
    cerr << "Can't parse bitcode " << path << '\n';
    exit(1);
    return nullptr;
  }

  return std::move(module.get());
}

class Resolver : public RuntimeDyld::SymbolResolver {
public:

  RuntimeDyld::SymbolInfo findSymbol(const std::string &Name) {
    if (auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
      return RuntimeDyld::SymbolInfo(SymAddr, JITSymbolFlags::Exported);

    return RuntimeDyld::SymbolInfo(nullptr);
  }

  RuntimeDyld::SymbolInfo findSymbolInLogicalDylib(const std::string &Name) {
    return RuntimeDyld::SymbolInfo(nullptr);
  }
};

int main(int argc, char **argv) {
  if (argc != 2) {
    cerr << "Usage: \n"
      "\t./jitter path_to_bitcode_file.bc\n";
    exit(1);
  }

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

  std::unique_ptr<TargetMachine> targetMachine(
                                EngineBuilder().selectTarget(Triple(), "", "",
                                SmallVector<std::string, 1>()));

  auto module = loadModuleAtPath(argv[1]);

  if (module->getDataLayout().isDefault()) {
    module->setDataLayout(targetMachine->createDataLayout());
  }

  SimpleCompiler compiler(*targetMachine);

  OwningBinary<ObjectFile> objectFile = compiler(*module);

  ObjectLinkingLayer<> objectLayer;

  vector<ObjectFile *> objectFiles;
  objectFiles.push_back(objectFile.getBinary());

  auto handle = objectLayer.addObjectSet(objectFiles,
                                        make_unique<SectionMemoryManager>(),
                                        make_unique<Resolver>());

  JITSymbol symbol = objectLayer.findSymbol("_main", false);

  void *mainPointer =
    reinterpret_cast<void *>(static_cast<uintptr_t>(symbol.getAddress()));

  if (mainPointer == nullptr) {
    cerr << "CustomTestRunner> Can't find pointer to _main\n";
    exit(1);
  }

  const char *jitArgv[] = { "jit", NULL };
  int jitArgc = 1;
  auto mainFunction = ((int (*)(int, const char**))(intptr_t)mainPointer);
  int exitStatus = mainFunction(jitArgc, jitArgv);

  objectLayer.removeObjectSet(handle);

  return 0;
}


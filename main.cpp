#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/OrcMCJITReplacement.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/TargetSelect.h>


#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>
#include <vector>

/// Monkey-patch MacOS 10.13 API
#include <fcntl.h> /* Definition of AT_* constants */
#include <sys/stat.h>
extern "C" int futimens(int fd, const struct timespec times[2]) { return 0; }

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

class Resolver : public JITSymbolResolver {
public:

  JITSymbol findSymbol(const std::string &Name) {
    if (auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
      return JITSymbol(SymAddr, JITSymbolFlags::Exported);

    return JITSymbol(nullptr);
  }

  JITSymbol findSymbolInLogicalDylib(const std::string &Name) {
    return JITSymbol(nullptr);
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

  auto objectFile = std::make_shared<OwningBinary<ObjectFile>>(compiler(*module));

  RTDyldObjectLinkingLayer objectLayer([]() { return std::make_shared<SectionMemoryManager>(); });

  auto handle = objectLayer.addObject(std::move(objectFile),
                                      make_unique<Resolver>());

  JITSymbol symbol = objectLayer.findSymbol("_main", false);

  void *mainPointer =
    reinterpret_cast<void *>(static_cast<uintptr_t>(symbol.getAddress().get()));

  if (mainPointer == nullptr) {
    cerr << "CustomTestRunner> Can't find pointer to _main\n";
    exit(1);
  }

  const char *jitArgv[] = { "jit", NULL };
  int jitArgc = 1;
  auto mainFunction = ((int (*)(int, const char**))(intptr_t)mainPointer);
  int exitStatus = mainFunction(jitArgc, jitArgv);

  objectLayer.removeObject(handle.get());

  return 0;
}


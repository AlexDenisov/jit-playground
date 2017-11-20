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

#include <sys/time.h>
#include <sys/resource.h>


#if 1
/// Monkey-patch MacOS 10.13 API
#include <fcntl.h> /* Definition of AT_* constants */
#include <sys/stat.h>
extern "C" int futimens(int fd, const struct timespec times[2]) { return 0; }
#endif

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

void reportMemoryUsage(const char *context) {
  struct rusage r;
  getrusage(RUSAGE_SELF, &r);
  printf("[%s] max rss: %ldmb\n", context, r.ru_maxrss / 1024 / 1024);
}

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

  for (int i = 0; i < 1000; i++) {
    reportMemoryUsage("before");
    __unused auto objectFile = compiler(*module);
    reportMemoryUsage("after");
  }


  return 0;
}


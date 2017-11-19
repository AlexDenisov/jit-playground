LLVM_VERSION=5.0
CXX=/opt/llvm-$(LLVM_VERSION)/bin/clang++
LLVM_CONFIG=/opt/llvm-$(LLVM_VERSION)/bin/llvm-config

CXX_FLAGS=$(shell $(LLVM_CONFIG) --cxxflags)
LD_FLAGS=$(shell $(LLVM_CONFIG) --ldflags) -lz -lncurses -lc
LIBS=$(shell $(LLVM_CONFIG) --libs)
BITCODE_FLAGS=-c -emit-llvm

jitter: main.cpp
	$(CXX) $(CXX_FLAGS) $(LD_FLAGS) $(LIBS) main.cpp -o jitter

bitcode:
	$(CXX) $(BITCODE_FLAGS) atexit_crash.cpp


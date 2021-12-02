#ifndef POINT_TO_ANALYSIS_H
#define POINT_TO_ANALYSIS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "utils.h"

using namespace llvm;

namespace {
struct PointToAnalysis : public ModulePass {
private:
  // XX应该是个什么类型？
  std::map<XXX, std::set<XXX>> pointToSets;

public:
  static char ID;
  PointToAnalysis() : ModulePass(ID) {}

private:
  void printResults() const { errs() << ""; }

  void handleCallInst(CallInst *callInst) const {
    // ...
  }

  void handleFunction(Function &F) const {
    for (const BasicBlock &bb : F) {
      for (const Instruction &inst : bb) {
        if (isa<DbgInfoIntrinsic>(inst)) {
          continue;
        } else if (StoreInst *storeInst = dyn_cast<StoreInst>(&inst)) {
          // ...
        } else if (LoadInst *loadInst = dyn_cast<LoadInst>(&inst)) {
          // ...
        }
      }
    }
  }

public:
  bool runOnModule(Module &M) override {
    LOG_DEBUG("We are now in PointToAnalysis struct...");
    // LOG_DEBUG("Module dump: \n" << M);

    for (const Function &F : M) {
      if (F.isIntrinsic()) {
        continue;
      }
      LOG_DEBUG("Function: " << F);
    }

    printResults();

    return false;
  }
}; // end of struct PointToAnalysis
} // end of anonymous namespace

#endif // POINT_TO_ANALYSIS_H
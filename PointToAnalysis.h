#ifndef POINT_TO_ANALYSIS_H
#define POINT_TO_ANALYSIS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/IR/IntrinsicInst.h>

#include "Dataflow.h"
#include "utils.h"

using namespace llvm;

namespace {

// 注意：PointToSets没有全局的实体，都是作为临时变量和参数存在，
// 结果只保存在DataflowResult中。
struct PointToSets {
  // {v: {v1, v2, ...}}
  std::map<Value *, std::set<Value *>> sets;

  PointToSets() : sets() {} // 或许用不到这个

  bool operator==(const PointToSets &pts) const {
    const auto &rsets = pts.sets; // right sets
    return sets == rsets;
  }

  bool operator!=(const PointToSets &pts) const {
    const auto &rsets = pts.sets; // right sets
    return sets != rsets;
  }
};

inline raw_ostream &operator<<(raw_ostream &out, const PointToSets &pts) {
  for (const auto v : pts.sets) {
    if (v.first->hasName()) {
      out << v.first->getName();
    } else {
      out << "%*"; // 临时变量的数字标号是打印时生成的，无法获取
    }
    out << ": {";

    const auto &s = v.second;
    for (auto iter = s.begin(); iter != s.end(); iter++) {
      if (iter != s.begin()) {
        out << ", ";
      }
      out << (*iter)->getName();
    }
    out << "}\n";
  }
  return out;
}

class PointToVisitor : public DataflowVisitor<struct PointToSets> {
private:
  // 保存函数调用结果，即行号和对应被调用函数名的映射
  std::map<unsigned, std::set<std::string>> functionCallResult;

public:
  PointToVisitor() {}

  void merge(PointToSets *dest, const PointToSets &src) override {
    auto &destSets = dest->sets;
    const auto &srcSets = src.sets;

    for (const auto &pts : srcSets) {
      Value *k = pts.first; // 为啥不能用 const 修饰这个指针？？
      const std::set<Value *> &s = pts.second;

      auto result = destSets.find(k);
      if (result == destSets.end()) {
        destSets.insert(std::make_pair(k, s));
      } else {
        result->second.insert(s.begin(), s.end());
      }
    }
  }

  ///
  /// 根据每条指令的操作对指向集做相应的更新
  /// 更新保存到参数 dfval 中
  ///
  /// @param inst 要处理的指令
  /// @param dfval 处理当前指令时的 pts 集合，可能会被更新
  /// @param targetEntryAndIncomings 只在处理 CallInst 时起作用，将找到的 target
  /// entry 和参数绑定后得到的 incomings 向上进行回传
  ///
  void compDFVal(
      Instruction *inst, PointToSets *dfval,
      std::map<BasicBlock *, PointToSets> *targetEntryAndIncomings) override {
    // 不处理调试相关的指令
    if (isa<DbgInfoIntrinsic>(inst)) {
      return;
    }

    // 根据指令的类型去进行相应的处理操作
    if (StoreInst *storeInst = dyn_cast<StoreInst>(inst)) {
      handleStoreInst(storeInst, dfval);
    } else if (LoadInst *loadInst = dyn_cast<LoadInst>(inst)) {
      handleLoadInst(loadInst, dfval);
    } else if (CallInst *callInst = dyn_cast<CallInst>(inst)) {
      handleCallInst(callInst, dfval, targetEntryAndIncomings);

      // CallInst处理完后，要在这里进行结果的保存，将指令的行号和被调用函数名的映射关系
      // 保存在 unctionCallResult 变量中。
      for (auto target : *targetEntryAndIncomings) {
        // target entry 所在的函数
        Function *fn = target.first->getParent();

        // 获取当前CallInst的行号
        unsigned lineno = callInst->getDebugLoc().getLine();

        // 保存结果
        auto result = functionCallResult.find(lineno);
        if (result == functionCallResult.end()) {
          std::set<std::string> fnName = {fn->getName()};
          functionCallResult.insert(std::make_pair(lineno, fnName));
        } else {
          result->second.insert(fn->getName());
        }
      }
    }
  }

  void printResults(raw_ostream &out) const {
    for (const auto &result : functionCallResult) {
      out << result.first << " : ";
      const auto &funcNames = result.second;
      for (auto iter = funcNames.begin(); iter != funcNames.end(); iter++) {
        if (iter != funcNames.begin()) {
          out << ", ";
        }
        out << *iter;
      }
      out << "\n";
    }
  }

private:
  /// *x = y
  /// store <ty> <value>, <ty>* <pointer>
  void handleStoreInst(StoreInst *storeInst, PointToSets *dfval) {
    Value *value = storeInst->getValueOperand();
    Value *pointer = storeInst->getPointerOperand();

    // pts[pointer] = {value}
    // 忽略 null 赋值
    if (!isa<ConstantPointerNull>(value)) {
      dfval->sets[pointer].clear();
      dfval->sets[pointer].insert(value);
    }
  }

  /// x = *y
  /// <result> = load <ty>, <ty>* <pointer>
  void handleLoadInst(LoadInst *loadInst, PointToSets *dfval) {
    Value *pointer = loadInst->getPointerOperand();
    Value *result = dyn_cast<Value>(loadInst);

    // pts[result] = pts[pointer]
    /// TODO: 如果让set指向同一个对象，或许可以解决别名指针的更新问题
    dfval->sets[result] = dfval->sets[pointer];
  }

  /// <result> = call <ty> <fnptrval>(<function args>)
  ///
  void
  handleCallInst(CallInst *callInst, PointToSets *dfval,
                 std::map<BasicBlock *, PointToSets> *targetEntryAndIncomings) {
    LOG_DEBUG("Call instruction: " << *callInst);
    Value *result = dyn_cast<Value>(callInst);
    Value *fnptrval = callInst->getCalledOperand();

    // 注意：entry基本块是没有前驱的
    // 方案1：获取被调用函数的entry基本块，把参数值加入基本块的income
    //       还得把目标entry基本块再加回到worklist里

    // CalledOperand 是一个 Value，一般来说一定是Call的上一条Load指令，
    // 这个Load指令的Point-to
    // set已经存到dfval中，set的每个元素即是调用的目标函数。
    // 可能没有考虑到间接调用的情况，遇到bug再说。

    // fnptrval的取值有两种情况，一般情况下，会由如下的指令装载，其指向的就是要调用的函数。
    //
    //    %2 = load i32 (i32, i32)*, i32 (i32, i32)** %pf_ptr
    //
    //    fnptrval -- pf_ptr
    //    fnvals -> {plus, minus}
    //
    // 但是如果被调用的函数指针是由参数传进来的，那么会存在下面这样的情况，
    // 即装载得到的fnptrval并不是直接指向被调用函数，而是指向一个a_fptr指针，
    // a_fptr指针再指向被调用的函数。
    //
    //    store i32 (i32, i32)* %a_fptr, i32 (i32, i32)** %a_fptr.addr
    //    %0 = load i32 (i32, i32)*, i32 (i32, i32)** %a_fptr.addr
    //
    //    fnptrval -- %0
    //    fnvals -> {a_fptr}
    //    a_fptr -> {plus, minus}
    //
    // 对于第二种情况，需要取出其内部实际指向的被调用函数进行处理，而不是处理a_fptr指针。

    std::set<Value *> *fnvals = &(dfval->sets[fnptrval]);
    if (fnvals->size() == 1 && !isa<Function>(*(fnvals->begin()))) {
      fnvals = &(dfval->sets[*(fnvals->begin())]);
    }

    // 遍历每一个可能被调用的函数
    for (Value *fnval : *fnvals) {
      // LOG_DEBUG("fnval: " << *fnval);
      Function *fn = dyn_cast<Function>(fnval);

      // 由于是迭代式遍历，可能有作为参数传入的函数指针还未进行绑定，此时先不做处理
      if (!fn) {
        LOG_DEBUG("Unbounded fnval.");
        continue;
      }

      // 获得 target entry
      BasicBlock *targetEntry = &(fn->getEntryBlock());
      LOG_DEBUG("Found target entry: " << *targetEntry);

      PointToSets targetIncoming;

      // 将调用函数和被调用函数进行参数绑定
      for (unsigned i = 0, num = callInst->getNumArgOperands(); i < num; i++) {
        Value *callerArg = callInst->getArgOperand(i);
        // LOG_DEBUG("Caller argument: " << *callerArg);

        // 只需要处理指针类型的参数，对于常量类型的不需要考虑
        if (callerArg->getType()->isPointerTy()) {
          Value *calleeArg = fn->getArg(i);
          // LOG_DEBUG("Callee argument: " << *calleeArg);
          targetIncoming.sets[calleeArg] = dfval->sets[callerArg];
        } else {
          // LOG_DEBUG("Nonpointer argument type.");
        }
      }

      // 借助targetEntryAndIncomings参数向上回传结果
      (*targetEntryAndIncomings)[targetEntry] = targetIncoming;
    }
  }
};

// class PointToAnalysis : public FunctionPass {
// public:
//   static char ID;
//   PointToAnalysis() : FunctionPass(ID) {}

//   bool runOnFunction(Function &F) override {
//     LOG_DEBUG("Running on function: " << F);
//     LOG_DEBUG("==================== Function dump ends
//     ====================");

//     PointToVisitor visitor;

//     // 可能要放到外面
//     DataflowResult<PointToSets>::Type result; // {basicblock: (pts_in,
//     pts_out)}

//     PointToSets initval;

//     // 对每一个函数去计算它的前向数据流
//     compForwardDataflow(&F, &visitor, &result, initval);
//     LOG_DEBUG("Here are the dataflow result of this function: ");
//     printDataflowResult<PointToSets>(errs(), result);
//     // ...
//     return false;
//   }
// };

class PointToAnalysis : public ModulePass {
public:
  static char ID;
  PointToAnalysis() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {

    DataflowResult<PointToSets>::Type result; // {basicblock: (pts_in, pts_out)}
    PointToVisitor visitor;

    for (Function &F : M) {
      LOG_DEBUG("Running on function: " << F);
      LOG_DEBUG("==================== Function dump ends ====================");

      PointToSets initval;

      // 对每一个函数去计算它的前向数据流
      compForwardDataflow(&F, &visitor, &result, initval);
    }

    LOG_DEBUG("Here is the dataflow result of the module: ");
    printDataflowResult<PointToSets>(errs(), result);

    LOG_DEBUG("Results: ");
    visitor.printResults(errs());
    // ...
    return false;
  }
};

} // end of anonymous namespace

#endif // POINT_TO_ANALYSIS_H
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

// 注意：PointToSets没有全局的实体，都是作为临时变量和参数存在
struct PointToSets {
  // 需要确保这两个map的key是互不相交的
  // 需要注意这俩map虽然形式一样但存储的内容是不同的
  // pointToSets: 一个变量它指向什么
  // binding: 一个变量它等同于什么，可以理解为别名
  std::map<Value *, std::set<Value *>> pointToSets;
  std::map<Value *, std::set<Value *>> bindings; // 存储临时变量绑定关系

  friend raw_ostream &operator<<(raw_ostream &out, const PointToSets &pts);

  bool operator==(const PointToSets &pts) const {
    return pointToSets == pts.pointToSets && bindings == pts.bindings;
  }

  bool operator!=(const PointToSets &pts) const {
    return pointToSets != pts.pointToSets || bindings != pts.bindings;
  }

  bool hasBinding(Value *value) {
    return bindings.find(value) != bindings.end();
  }

  void setBinding(Value *pointer, std::set<Value *> values) {
    bindings[pointer] = values;
  }

  std::set<Value *> getBinding(Value *tmp) {
    // assert(hasBinding(tmp));
    return bindings[tmp];
  }

  bool hasPTS(Value *pointer) {
    return pointToSets.find(pointer) != pointToSets.end();
  }

  /// TODO: 这个函数或许可以优化一下
  /// 最好是去掉自动查找binding的部分，不然会让后面的部分难以理解
  /// 可能会分不清数据到底是从哪里来的
  std::set<Value *> getPTS(Value *pointer) {
    auto tmp = bindings.find(pointer);
    if (tmp != bindings.end()) {
      std::set<Value *> result;
      // LOG_DEBUG("Found binding for: " << *(tmp->first));
      for (Value *v : tmp->second) {
        // LOG_DEBUG("Binding target: " << *v);
        // std::set<Value *> &pts = pointToSets.at(v);
        if (pointToSets.find(v) == pointToSets.end()) {
          LOG_DEBUG("Warn: Empty pts for binding target " << *v);
        }
        std::set<Value *> &pts = pointToSets[v];
        result.insert(pts.begin(), pts.end());
      }
      return result;
    } else {
      // LOG_DEBUG("Found in pts: " << *pointer);
      return pointToSets.at(pointer);
    }
  }

  void setPTS(Value *pointer, std::set<Value *> &set) {
    pointToSets[pointer] = set;
  }
};

// Example:
//   {%a_fptr, %b_fptr, %*}
//   {@plus, @minus}
inline raw_ostream &operator<<(raw_ostream &out,
                               const std::set<Value *> &setOfValues) {
  out << "{";
  for (auto iter = setOfValues.begin(); iter != setOfValues.end(); iter++) {
    if (iter != setOfValues.begin()) {
      out << ", ";
    }
    if ((*iter)->hasName()) {
      if (isa<Function>(*iter)) {
        out << "@" << (*iter)->getName();
      } else {
        out << "%" << (*iter)->getName();
      }
    } else {
      out << "%*";
    }
  }
  out << "}";
  return out;
}

// Example:
// Point-to sets:
//         %a.addr: {%a}
//         %b.addr: {%b}
//         %a_fptr.addr: {%*}
//         %b_fptr.addr: {%*, %*, %*}
//         %t_fptr: {@plus}
//         %*: {@plus, @minus}
//         %*: {@plus, @minus}
//         %*: {@plus}
// Temp value bindings:
//         %a_fptr= {%*}
//         %b_fptr= {%*, %*, %*}
//         %*= {%*}
//         %*= {@plus}
//         %*= {%*, %*, %*}
//         ......
inline raw_ostream &operator<<(raw_ostream &out, const PointToSets &pts) {
  out << "Point-to sets: \n";
  for (const auto v : pts.pointToSets) {
    out << "\t%";
    if (v.first->hasName()) {
      out << v.first->getName();
    } else {
      out << "*"; // 临时变量的数字标号是打印时生成的，无法获取
    }
    out << ": " << v.second << "\n";
  }
  out << "Temp value bindings: \n";
  for (const auto v : pts.bindings) {
    out << "\t%";
    if (v.first->hasName()) {
      out << v.first->getName();
    } else {
      out << "*"; // 临时变量的数字标号是打印时生成的，无法获取
    }
    out << "= " << v.second << "\n";
  }

  return out;
}

class PointToVisitor : public DataflowVisitor<struct PointToSets> {
public:
  // 保存函数调用结果，即行号和对应被调用函数名的映射
  std::map<unsigned, std::set<std::string>> functionCallResult;

public:
  PointToVisitor() {}

  void merge(PointToSets *dest, const PointToSets &src) override {
    // 合并 pointToSets
    auto &destSets = dest->pointToSets;
    const auto &srcSets = src.pointToSets;

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

    // 合并 bindings
    // 一般情况下绑定信息是不需要在基本块之间传递的，但是为了能够解决引用型参数和函数返回问题，
    // 在这里也进行合并，不影响结果，但是可能会让调试信息更杂乱。
    auto &destBindings = dest->bindings;
    const auto &srcBindings = src.bindings;

    for (const auto &binding : srcBindings) {
      Value *k = binding.first;
      const std::set<Value *> &s = binding.second;

      auto result = destBindings.find(k);
      if (result == destBindings.end()) {
        destBindings.insert(std::make_pair(k, s));
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
  ///
  void compDFVal(Instruction *inst, PointToSets *dfval) override {
    // 不处理调试相关的指令
    if (isa<DbgInfoIntrinsic>(inst)) {
      return;
    }

    LOG_DEBUG("Current instruction: " << *inst);

    // 根据指令的类型去进行相应的处理操作
    if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(inst)) {
      // 好像用不着，嘿嘿
      // handleAllocaInst(allocaInst, dfval);
    } else if (StoreInst *storeInst = dyn_cast<StoreInst>(inst)) {
      handleStoreInst(storeInst, dfval);
    } else if (LoadInst *loadInst = dyn_cast<LoadInst>(inst)) {
      handleLoadInst(loadInst, dfval);
    } else if (GetElementPtrInst *getElementPtrInst =
                   dyn_cast<GetElementPtrInst>(inst)) {
      handleGetElementPtrInst(getElementPtrInst, dfval);
    } else if (BitCastInst *bitCastInst = dyn_cast<BitCastInst>(inst)) {
      // 也用不着，嘿嘿
      // handleBitCastInst(bitCastInst, dfval);
    } else if (MemCpyInst *memCpyInst = dyn_cast<MemCpyInst>(inst)) {
      handleMemCpyInst(memCpyInst, dfval);
    } else if (MemSetInst *memSetInst = dyn_cast<MemSetInst>(inst)) {
      // 捕获但不需要处理，防止它被后面CallInst的处理逻辑捕获
    } else if (ReturnInst *returnInst = dyn_cast<ReturnInst>(inst)) {
      handleReturnInst(returnInst, dfval);
    } else if (CallInst *callInst = dyn_cast<CallInst>(inst)) {
      handleCallInst(callInst, dfval);
    } else {
      LOG_DEBUG("Unhandled instruction: " << *inst);
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

    // https://llvm.org/doxygen/classllvm_1_1Constant.html
    if (isa<ConstantData>(value)) {
      LOG_DEBUG("Skipped constant data " << *value << " in StoreInst.");
      return;
    }

    // pointer可能指向多个目标，要依次对每一个进行指向
    std::set<Value *> queue = {pointer};
    std::set<Value *> targets;
    while (!queue.empty()) {
      Value *v = *queue.begin();
      queue.erase(v);
      if (dfval->hasBinding(v)) {
        std::set<Value *> s = dfval->getBinding(v);
        queue.insert(s.begin(), s.end());
      } else {
        targets.insert(v);
      }
    }

    std::set<Value *> values;
    if (dfval->hasBinding(value)) {
      values = dfval->getBinding(value);
    } else {
      values.insert(value);
    }

    // 考虑PPT中store语句的规则2，当存在多个可能的目标时，并不确定实际运行时指向哪一个，
    // 因此不但要依次处理每个目标的指向，还不能将目标原先的指向清空。
    if (targets.size() == 1) {
      dfval->setPTS(*targets.begin(), values);
    } else {
      for (Value *target : targets) {
        std::set<Value *> oldPTS = dfval->getPTS(target);
        oldPTS.insert(values.begin(), values.end());
        dfval->setPTS(target, oldPTS);
      }
    }
  }

  /// x = *y
  /// <result> = load <ty>, <ty>* <pointer>
  void handleLoadInst(LoadInst *loadInst, PointToSets *dfval) {
    Value *pointer = loadInst->getPointerOperand();
    Value *result = dyn_cast<Value>(loadInst);

    // 只处理二级指针及以上，因为一级指针总是指向常数
    // https://stackoverflow.com/a/12954400/15851567
    if (!pointer->getType()->getContainedType(0)->isPointerTy()) {
      return;
    }

    std::set<Value *> s = dfval->getPTS(pointer);
    dfval->setBinding(result, s);
  }

  /// <result> = getelementptr inbounds <ty>* <ptrval>{, <ty> <idx>}*
  void handleGetElementPtrInst(GetElementPtrInst *getElementPtrInst,
                               PointToSets *dfval) {
    Value *ptrval = getElementPtrInst->getPointerOperand();
    Value *result = dyn_cast<Value>(getElementPtrInst);

    if (dfval->hasBinding(ptrval)) {
      dfval->setBinding(result, dfval->getBinding(ptrval));
    } else {
      dfval->setBinding(result, {ptrval});
    }
  }

  void handleMemCpyInst(MemCpyInst *memCpyInst, PointToSets *dfval) {
    // getSource()和getDest()函数可以自动处理BitCast，提取出最终的操作数
    Value *source = memCpyInst->getSource();
    Value *dest = memCpyInst->getDest();

    // LOG_DEBUG("Source of MemCpyInst: " << *source);
    // LOG_DEBUG("Dest of MemCpyInst: " << *dest);

    if (dfval->hasBinding(dest)) {
      assert(dfval->getBinding(dest).size() == 1);
      dest = *(dfval->getBinding(dest).begin());
    }

    std::set<Value *> s = dfval->getPTS(source);
    dfval->setPTS(dest, s);
  }

  void handleReturnInst(ReturnInst *returnInst, PointToSets *dfval) {
    Value *value = returnInst->getReturnValue();
    Value *func = returnInst->getFunction();

    if (dfval->hasBinding(func)) {
      // 把返回值直接绑定到所在函数上
      if (dfval->hasBinding(value)) {
        dfval->setBinding(func, dfval->getBinding(value));
      } else {
        dfval->setBinding(func, {value});
      }
    }
  }


  ///
  /// 将近200行的函数，比较复杂。
  /// 主要流程是在要进行调用之前进行参数和返回值的绑定，并记录绑定关系，
  /// 在函数调用完成后从目标函数最后一个基本块的outcoming中获得处理结果，
  /// 把结果与之前记录的进行对比，发生改变则更新。
  /// 需要格外注意的是内层的改变，即一个变量的指向集或者绑定没有改变，但它所指向的目标的
  /// 指向集或者绑定关系可能已经改变了。
  /// 
  void handleCallInst(CallInst *callInst, PointToSets *dfval) {
    Value *callResult = dyn_cast<Value>(callInst);
    Value *fnptrval = callInst->getCalledOperand();
    unsigned lineno = callInst->getDebugLoc().getLine();

    // 用于后面保存调用结果
    std::set<std::string> &funcNameSet = functionCallResult[lineno];

    // 对malloc函数调用做特殊处理
    if (isa<Function>(fnptrval) && fnptrval->getName() == "malloc") {
      funcNameSet.insert(fnptrval->getName());
      return;
    }

    PointToSets initval;
    PointToVisitor visitor; // visitor在不同的被调函数中共享

    LOG_DEBUG("Current dfval in CallInst: \n" << *dfval);

    // 可能是直接调用一个函数，比如@clever，也可能是一个指向多个函数的绑定，比如
    // %1 = @plus, @minus
    std::set<Value *> fnvals;
    if (isa<Function>(fnptrval)) {
      fnvals.insert(fnptrval);
    } else {
      fnvals = dfval->getBinding(fnptrval);
    }

    // 对每一个被调用函数都进行参数绑定和递归处理
    for (Value *fnval : fnvals) {
      Function *func = dyn_cast<Function>(fnval);
      std::string funcName = func->getName();
      BasicBlock *targetEntry = &(func->getEntryBlock());
      BasicBlock *targetExit = &(func->back());
      PointToSets calleeArgBindings;
      std::set<std::pair<Value *, Value *>> argPairs;
      DataflowResult<PointToSets>::Type result;

      funcNameSet.insert(funcName);

      // 进行参数的绑定
      for (unsigned i = 0, num = callInst->getNumArgOperands(); i < num; i++) {
        Value *callerArg = callInst->getArgOperand(i);

        // 只处理指针传递就可以了
        if (callerArg->getType()->isPointerTy()) {
          Value *calleeArg = func->getArg(i);

          argPairs.insert(std::make_pair(callerArg, calleeArg));

          if (dfval->hasBinding(callerArg)) {
            std::set<Value *> bindingTarget = dfval->getBinding(callerArg);
            calleeArgBindings.setBinding(calleeArg, bindingTarget);

            /// TODO: 可以和下面进行合并
            std::set<Value *> queue = bindingTarget;
            while (!queue.empty()) {
              Value *v = *queue.begin();
              queue.erase(queue.begin());
              // LOG_DEBUG("Finding dependency for " << *v);
              if (dfval->hasPTS(v)) {
                std::set<Value *> s = dfval->getPTS(v);
                // LOG_DEBUG("Dependencies found: " << s);
                calleeArgBindings.setPTS(v, s);
                //
                argPairs.insert(std::make_pair(v, v));
                queue.insert(s.begin(), s.end());
              }
            }
          } else {
            calleeArgBindings.setBinding(calleeArg, {callerArg});

            // callerArg可能会依赖其他的值，找出这些指向关系，一并进行绑定
            std::set<Value *> queue = {callerArg};
            while (!queue.empty()) {
              Value *v = *queue.begin();
              queue.erase(queue.begin());
              // LOG_DEBUG("Finding dependency for " << *v);
              if (dfval->hasPTS(v)) {
                std::set<Value *> s = dfval->getPTS(v);
                // LOG_DEBUG("Dependencies found: " << s);
                calleeArgBindings.setPTS(v, s);
                //
                argPairs.insert(std::make_pair(v, v));
                queue.insert(s.begin(), s.end());
              }
            }
          }
        }
      }

      // 返回值绑定
      if (func->getReturnType()->isPointerTy()) {
        LOG_DEBUG("Function " << func->getName()
                              << " has a pointer return type.");
        calleeArgBindings.setBinding(func, {func});
        argPairs.insert(std::make_pair(callResult, func));
      }

      result[targetEntry].first =
          calleeArgBindings; // incomings of target entry

      LOG_DEBUG("Now recursively handling function: " << func->getName());
      compForwardDataflow(func, &visitor, &result, initval);

      PointToSets &calleeOutBindings =
          result[targetExit].second; // outcomings of target exit

      /// 调用完成后根据目标函数最终的outcoming更新当前函数内的变量指向
      /// TODO: 这块看起来很复杂实际上很多内容可以合并精简，我懒得搞了
      for (auto &pair : argPairs) {
        // 参数返回
        if (calleeOutBindings.hasBinding(pair.second)) {
          const std::set<Value *> &outBinding =
              calleeOutBindings.getBinding(pair.second);
          const std::set<Value *> &inBinding =
              calleeArgBindings.getBinding(pair.second);
          if (outBinding != inBinding) {
            std::set<Value *> binding;
            if (dfval->hasBinding(pair.first)) {
              const std::set<Value *> &oldBinding =
                  dfval->getBinding(pair.first);
              binding = oldBinding;
            }
            const std::set<Value *> &newBinding =
                calleeOutBindings.getBinding(pair.second);
            binding.insert(newBinding.begin(), newBinding.end());
            dfval->setBinding(pair.first, binding);
          } else {
            std::set<Value *> queue = outBinding;
            while (!queue.empty()) {
              Value *v = *queue.begin();
              queue.erase(v);
              if (calleeOutBindings.hasPTS(v) &&
                  (!calleeArgBindings.hasPTS(v) ||
                   calleeOutBindings.getPTS(v) !=
                       calleeArgBindings.getPTS(v))) {
                std::set<Value *> s = calleeOutBindings.getPTS(v);
                LOG_DEBUG("s: " << s);
                dfval->setPTS(v, s);
                queue.insert(s.begin(), s.end());
              }
            }
          }
        } else {
          std::set<Value *> queue = {pair.second};
          while (!queue.empty()) {
            Value *v = *queue.begin();
            queue.erase(v);

            if (calleeOutBindings.hasPTS(v) &&
                (!calleeArgBindings.hasPTS(v) ||
                 calleeOutBindings.getPTS(v) != calleeArgBindings.getPTS(v))) {
              std::set<Value *> s = calleeOutBindings.getPTS(v);
              dfval->setPTS(v, s);
              queue.insert(s.begin(), s.end());
            } else {
              if (calleeOutBindings.hasPTS(v)) {
                std::set<Value *> s = calleeOutBindings.getPTS(v);
                queue.insert(s.begin(), s.end());
              }
            }
          }
        }
      }
    }

    // 更新输出结果集
    for (auto functionCalls : visitor.functionCallResult) {
      auto result = functionCallResult.find(functionCalls.first);
      if (result == functionCallResult.end()) {
        functionCallResult.insert(functionCalls);
      } else {
        result->second.insert(functionCalls.second.begin(),
                              functionCalls.second.end());
      }
    }
  }
};

class PointToAnalysis : public ModulePass {
public:
  static char ID;
  PointToAnalysis() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {

    DataflowResult<PointToSets>::Type result; // {basicblock: (pts_in, pts_out)}
    PointToVisitor visitor;
    PointToSets initval;

    // 假设最后一个函数是程序的入口函数
    auto f = M.rbegin(), e = M.rend();
    for (; (f->isIntrinsic() || f->size() == 0) && f != e; f++) {
    }

    LOG_DEBUG("Entry function: " << f->getName());
    compForwardDataflow(&*f, &visitor, &result, initval);

    LOG_DEBUG("Results: ");
    visitor.printResults(errs());

    return false;
  }
};

} // end of anonymous namespace

#endif // POINT_TO_ANALYSIS_H
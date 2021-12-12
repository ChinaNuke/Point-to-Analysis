/************************************************************************
 *
 * @file Dataflow.h
 *
 * General dataflow framework
 *
 ***********************************************************************/

#ifndef _DATAFLOW_H_
#define _DATAFLOW_H_

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <map>

#include "utils.h"

using namespace llvm;

/// Base dataflow visitor class, defines the dataflow function

template <class T> class DataflowVisitor {
public:
  virtual ~DataflowVisitor() {}

  ///
  /// Dataflow Function invoked for each basic block
  ///
  /// @param block the Basic Block
  /// @param dfval the input dataflow value
  /// @param isforward true to compute dfval forward, otherwise backward
  ///
  virtual void compDFVal(BasicBlock *block, T *dfval, bool isforward) {
    if (isforward == true) {
      for (BasicBlock::iterator ii = block->begin(), ie = block->end();
           ii != ie; ++ii) {
        Instruction *inst = &*ii;
        compDFVal(inst, dfval);
      }
    } else {
      for (BasicBlock::reverse_iterator ii = block->rbegin(),
                                        ie = block->rend();
           ii != ie; ++ii) {
        Instruction *inst = &*ii;
        compDFVal(inst, dfval);
      }
    }
  }

  ///
  /// Dataflow Function invoked for each instruction
  ///
  /// @param inst the Instruction
  /// @param dfval the input dataflow value
  ///
  virtual void compDFVal(Instruction *inst, T *dfval) = 0;

  ///
  /// Merge of two dfvals, dest will be ther merged result
  /// @return true if dest changed
  ///
  virtual void merge(T *dest, const T &src) = 0;
};

///
/// Dummy class to provide a typedef for the detailed result set
/// For each basicblock, we compute its input dataflow val and its output
/// dataflow val
///
template <class T> struct DataflowResult {
  typedef typename std::map<BasicBlock *, std::pair<T, T>> Type;
};

///
/// Compute a forward iterated fixedpoint dataflow function, using a
/// user-supplied visitor function. Note that the caller must ensure that the
/// function is in fact a monotone function, as otherwise the fixedpoint may not
/// terminate.
///
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow
/// @param initval the Initial dataflow value
///
template <class T>
void compForwardDataflow(Function *fn, DataflowVisitor<T> *visitor,
                         typename DataflowResult<T>::Type *result,
                         const T &initval) {

  std::set<BasicBlock *> worklist;

  // 初始化worklist，把所有基本块加入进去
  for (Function::iterator bi = fn->begin(); bi != fn->end(); ++bi) {
    BasicBlock *bb = &*bi;

    // 允许传入非空的result值以初始化
    if (result->find(bb) != result->end()) {
      result->insert(std::make_pair(bb, std::make_pair(initval, initval)));
    }
    worklist.insert(bb);
  }

  while (!worklist.empty()) {
    BasicBlock *bb = *worklist.begin();
    worklist.erase(worklist.begin());

    // 合并前驱基本块的输出值
    // 当前节点basicblock的income += 所有前驱节点的outcome
    // 这里的T是PointToSets
    T bbenterval = (*result)[bb].first; // incoming value
    for (pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; pi++) {
      BasicBlock *pred = *pi;
      visitor->merge(&bbenterval, (*result)[pred].second);
    }
    (*result)[bb].first = bbenterval;

    LOG_DEBUG("Now handling basic block " << bb->getName() << " in function " << bb->getParent()->getName());
    LOG_DEBUG("Incoming values: \n" << (*result)[bb].first);

    visitor->compDFVal(bb, &bbenterval, true);

    (*result)[bb].second = bbenterval;
    // 如果经过计算后outcome发生改变，那么在CFG中进行传播（把所有后继节点重新加入队列）
    if (bbenterval != (*result)[bb].first) {
      // 好像没有必要？后集节点本来也是在队列里面的
      for (succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se;
           si++) {
        worklist.insert(*si);
      }
    }

    LOG_DEBUG("Basic block " << bb->getName() << " in function " << bb->getParent()->getName() << " finished. ");
    LOG_DEBUG("Incoming values: \n" << (*result)[bb].first);
    LOG_DEBUG("Outcoming values: \n" << (*result)[bb].second);
  }
}
///
/// Compute a backward iterated fixedpoint dataflow function, using a
/// user-supplied visitor function. Note that the caller must ensure that the
/// function is in fact a monotone function, as otherwise the fixedpoint may not
/// terminate.
///
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow
/// @initval The initial dataflow value
template <class T>
void compBackwardDataflow(Function *fn, DataflowVisitor<T> *visitor,
                          typename DataflowResult<T>::Type *result,
                          const T &initval) {

  std::set<BasicBlock *> worklist;

  // Initialize the worklist with all exit blocks
  for (Function::iterator bi = fn->begin(); bi != fn->end(); ++bi) {
    BasicBlock *bb = &*bi;
    result->insert(std::make_pair(bb, std::make_pair(initval, initval)));
    worklist.insert(bb);
  }

  // Iteratively compute the dataflow result
  while (!worklist.empty()) {
    BasicBlock *bb = *worklist.begin();
    worklist.erase(worklist.begin());

    // Merge all incoming value
    // 当前节点basicblock的outcome += 所有后继节点的income
    T bbexitval = (*result)[bb].second;
    for (auto si = succ_begin(bb), se = succ_end(bb); si != se; si++) {
      BasicBlock *succ = *si;
      visitor->merge(&bbexitval, (*result)[succ].first);
    }

    (*result)[bb].second = bbexitval;

    // 计算基本块内的数据流
    visitor->compDFVal(bb, &bbexitval, false);

    // If outgoing value changed, propagate it along the CFG
    if (bbexitval == (*result)[bb].first)
      continue;
    (*result)[bb].first = bbexitval;

    for (pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; pi++) {
      worklist.insert(*pi);
    }
  }
}

template <class T>
void printDataflowResult(raw_ostream &out,
                         const typename DataflowResult<T>::Type &dfresult) {
  for (typename DataflowResult<T>::Type::const_iterator it = dfresult.begin();
       it != dfresult.end(); ++it) {
    if (it->first == NULL) {
      out << "*";
    } else {
      out << *(it->first);
    }
    out << "\nin : \n"
        << it->second.first << "\nout :  \n"
        << it->second.second << "\n";
  }
}

#endif /* !_DATAFLOW_H_ */

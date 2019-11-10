#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Transforms/Utils/Cloning.h"

// to load file with branch info give command line arg with name (might need multiple for different loops, so json file?)
// https://stackoverflow.com/questions/13626993/is-it-possible-to-add-arguments-for-user-defined-passes-in-llvm

using namespace llvm;

namespace {

  struct Trace {
    // keep track of the live-ins to this trace
    // used to generate function signature to actually run this thing
    std::vector<Type*> _signature;

    // A trace is a single basic block, store every instruction in this
    BasicBlock *_body;

    // The program/module the trace exists in (global variables are stored here)
    Module *_theModule;

    // Store the name of the trace to generate function name with
    std::string _traceName;

    // we need to generate a fallback routine to call when fails
    // TODO don't know how to do this without making it a branch instruction
    void initTrace() {

    }

    // list of instructions, safe to delete in the current block
    std::vector<Instruction*> getInstPtrsInBlk(BasicBlock *blk) {
      std::vector<Instruction*> instPtrs;
      for (auto& I : *blk) {
          instPtrs.push_back(&I);
      }
      return instPtrs;
    }

    void mergeBlks(BasicBlock *traceBB, BasicBlock *otherBB) {
      auto instPtrs = getInstPtrsInBlk(otherBB);
      for (int i = 0; i < instPtrs.size(); i++) {
        Instruction *movedInst = instPtrs[i];
        movedInst->removeFromParent();
        traceBB->getInstList().push_back(movedInst);
      }
      otherBB->eraseFromParent();
    }

    void generate(BasicBlock *curBB, std::vector<bool> pathArray) {
      bool done;
      int brIdx = 0;

      do {
        // set done flag
        done = true;

        // get pointers to each instrcution, so when delete in curBB the iteration isn't messed up...
        std::vector<Instruction*> instPtrs = getInstPtrsInBlk(curBB);

        // loop through all instructions (could prob make this more efficient by remembering we're we left off)
        for (int i = 0; i < instPtrs.size(); i++) {

          // get the current instruction that we're looking at
          Instruction *I = instPtrs[i];

          // check if branch (if return then we should finish the algorithm... for now)
          //BranchInst *branchInst = dyn_cast<BranchInst>(I);
          //if (branchInst != nullptr) {
          if (BranchInst *branchInst = dyn_cast<BranchInst>(I)) {
            if (branchInst->isConditional()) {
              // take a branch direction if conditional
              errs() << "found conditional " << *I << "\n";
              BasicBlock* nt = cast<BasicBlock>(branchInst->getOperand(2));
              BasicBlock* t  = cast<BasicBlock>(branchInst->getOperand(1));

              bool tracedOutcome = pathArray[brIdx];
              // continue tracing on the path that was taken and delete the other
              if (tracedOutcome) {
                errs() << "erase not taken\n";
                nt->eraseFromParent();
                mergeBlks(curBB, t);
              }
              else {
                errs() << "erase taken\n";
                t->eraseFromParent(); 
                mergeBlks(curBB, nt);
              }
            }
            // unconditional, note can't jump outside of function, so not really inlining
            // TODO what if multiple basic blocks go to this label... don't want to fully delete?
            // BUT is this a problem if it's a single forward path?
            else {
              BasicBlock* t  = cast<BasicBlock>(branchInst->getOperand(0));
              mergeBlks(curBB, t);
            }

            // remove the branch from the end of the block
            branchInst->eraseFromParent();

            // if this was a branch then still more work to do
            done = false;
            brIdx++;
          }
          // inline function calls along the path
          else if (CallInst *callInst = dyn_cast<CallInst>(I)) {
            // only inline if it's an llvm intrinsic (inserted when you inline the original function, so ignore)
            // These intrinsics are deleted when compile to machine code (not real function calls)
            // verified with objdump -dC
            Function *fun = callInst->getCalledFunction();
            if (fun->getName().str().compare(0, 5, "llvm.", 0, 5) == 0) {
              continue;
            }

            InlineFunctionInfo ifi;
            InlineFunction(callInst, ifi);
            done = false;
          }
          // remove phi nodes and update refs
          else if (PHINode *phiInst = dyn_cast<PHINode>(I)) {

          }

        }
      } while (!done);
    } 
  };


  // TODO potentially want a LoopPass b/c want to trace across within a loop nest and not outside
  struct SkeletonPass : public FunctionPass {
    static char ID;
    SkeletonPass() : FunctionPass(ID) {}

    virtual bool runOnFunction(Function &F) {

      // cheat in branch vector (TODO from file)
      std::vector<bool> branchOutcomes;
      for (int i = 0; i < 10; i++) {
        branchOutcomes.push_back(0);
      }

      // the current trace in llvm instructions (treat as a single basic block)
      Trace trace;

      // TODO just do on known function
      if (F.getName() != "test") return false;

      // get the first block of the function
      auto &bb = F.getEntryBlock();

      // generate a trace starting from a basic block
      trace.generate(&bb, branchOutcomes);

      errs() << F << "\n";

      // whether code was modified or not
      return true;
    }
  };
}

char SkeletonPass::ID = 0;

// Register the pass so `opt -skeleton` runs it.
static RegisterPass<SkeletonPass> X("skeleton", "a useless pass");

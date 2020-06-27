//=--AVarBoundsInfo.cpp-------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of methods in AVarBoundsInfo.h.
//
//===----------------------------------------------------------------------===//

#include "clang/CConv/AVarBoundsInfo.h"
#include "clang/CConv/ProgramInfo.h"
#include <boost/graph/breadth_first_search.hpp>

void AVarBoundsStats::print(llvm::raw_ostream &O) const {
  O << "Array Bounds Inference Stats:\n";
  O << "NamePrefixMatch:" << NamePrefixMatch.size() << "\n";
  O << "AllocatorMatch:" << AllocatorMatch.size() << "\n";
  O << "VariableNameMatch:" << VariableNameMatch.size() << "\n";
  O << "NeighbourParamMatch:" << NeighbourParamMatch.size() << "\n";
  O << "DataflowMatch:" << DataflowMatch.size() << "\n";
}

bool AVarBoundsInfo::isValidBoundVariable(clang::Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
    return !VD->getNameAsString().empty();
  }
  if (ParmVarDecl *PD = dyn_cast<ParmVarDecl>(D)) {
    return !PD->getNameAsString().empty();
  }
  if(FieldDecl *FD = dyn_cast<FieldDecl>(D)) {
    return !FD->getNameAsString().empty();
  }
  return false;
}

void AVarBoundsInfo::insertDeclaredBounds(clang::Decl *D, ABounds *B) {
  assert(isValidBoundVariable(D) && "Declaration not a valid bounds variable");
  BoundsKey BK;
  tryGetVariable(D, BK);
  if (B != nullptr) {
    // If there is already bounds information, release it.
    if (BInfo.find(BK) != BInfo.end()) {
      delete (BInfo[BK]);
    }
    BInfo[BK] = B;
  } else {
    // Set bounds to be invalid.
    InvalidBounds.insert(BK);
  }
}

bool AVarBoundsInfo::tryGetVariable(clang::Decl *D, BoundsKey &R) {
  if (isValidBoundVariable(D)) {
    if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
      R = getVariable(VD);
    }
    if (ParmVarDecl *PD = dyn_cast<ParmVarDecl>(D)) {
      R = getVariable(PD);
    }
    if (FieldDecl *FD = dyn_cast<FieldDecl>(D)) {
      R = getVariable(FD);
    }
    return true;
  }
  return false;
}

bool AVarBoundsInfo::tryGetVariable(clang::Expr *E,
                                    const ASTContext &C,
                                    BoundsKey &Res) {
  llvm::APSInt ConsVal;
  bool Ret = false;
  if (E != nullptr) {
    E = E->IgnoreParenCasts();
    if (E->getType()->isArithmeticType() &&
        E->isIntegerConstantExpr(ConsVal, C)) {
      Res = getVarKey(ConsVal);
      Ret = true;
    } else if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
      auto *D = DRE->getDecl();
      Ret = tryGetVariable(D, Res);
      if (!Ret) {
        assert(false && "Invalid declaration found inside bounds expression");
      }
    } else if (MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      return tryGetVariable(ME->getMemberDecl(), Res);
    }
    else {
      // assert(false && "Variable inside bounds declaration is an expression");
    }
  }
  return Ret;
}

bool AVarBoundsInfo::mergeBounds(BoundsKey L, ABounds *B) {
  bool RetVal = false;
  if (BInfo.find(L) != BInfo.end()) {
    // If previous computed bounds are not same? Then release the old bounds.
    if (!BInfo[L]->areSame(B)) {
      InvalidBounds.insert(L);
      delete (BInfo[L]);
      BInfo.erase(L);
    }
  } else {
    BInfo[L] = B;
    RetVal = true;
  }
  return RetVal;
}

bool AVarBoundsInfo::removeBounds(BoundsKey L) {
  bool RetVal = false;
  if (BInfo.find(L) != BInfo.end()) {
    delete (BInfo[L]);
    BInfo.erase(L);
    RetVal = true;
  }
  return RetVal;
}

bool AVarBoundsInfo::replaceBounds(BoundsKey L, ABounds *B) {
  removeBounds(L);
  return mergeBounds(L, B);
}

ABounds *AVarBoundsInfo::getBounds(BoundsKey L) {
  if (InvalidBounds.find(L) == InvalidBounds.end() &&
      BInfo.find(L) != BInfo.end()) {
    return BInfo[L];
  }
  return nullptr;
}

void AVarBoundsInfo::insertVariable(clang::Decl *D) {
  BoundsKey Tmp;
  tryGetVariable(D, Tmp);
}

BoundsKey AVarBoundsInfo::getVariable(clang::VarDecl *VD) {
  assert(isValidBoundVariable(VD) && "Not a valid bound declaration.");
  PersistentSourceLoc PSL = PersistentSourceLoc::mkPSL(VD, VD->getASTContext());
  if (!hasVarKey(PSL)) {
    BoundsKey NK = ++BCount;
    insertVarKey(PSL, NK);
    ProgramVarScope *PVS = nullptr;
    if (VD->hasGlobalStorage()) {
      PVS = GlobalScope::getGlobalScope();
    } else {
      FunctionDecl *FD =
          dyn_cast<FunctionDecl>(VD->getParentFunctionOrMethod());
      if (FD != nullptr) {
        PVS = FunctionScope::getFunctionScope(FD->getNameAsString(),
                                              FD->isStatic());
      }
    }
    assert(PVS != nullptr && "Context not null");
    auto *PVar = new ProgramVar(NK, VD->getNameAsString(), PVS);
    insertProgramVar(NK, PVar);
    if (VD->getType()->isPointerType())
      PointerBoundsKey.insert(NK);
  }
  return getVarKey(PSL);
}

BoundsKey AVarBoundsInfo::getVariable(clang::ParmVarDecl *PVD) {
  assert(isValidBoundVariable(PVD) && "Not a valid bound declaration.");
  FunctionDecl *FD = dyn_cast<FunctionDecl>(PVD->getDeclContext());
  int ParamIdx = -1;
  // Get parameter index.
  for (unsigned i=0; i<FD->getNumParams(); i++) {
    if (FD->getParamDecl(i) == PVD) {
      ParamIdx = i;
      break;
    }
  }
  auto Psl = PersistentSourceLoc::mkPSL(FD, FD->getASTContext());
  std::string FileName = Psl.getFileName();
  auto ParamKey = std::make_tuple(FD->getNameAsString(), FileName,
                                  FD->isStatic(), ParamIdx);
  assert(ParamIdx >= 0 && "Unable to find parameter.");
  if (ParamDeclVarMap.left.find(ParamKey) == ParamDeclVarMap.left.end()) {
    BoundsKey NK = ++BCount;
    FunctionParamScope *FPS =
        FunctionParamScope::getFunctionParamScope(FD->getNameAsString(),
                                                  FD->isStatic());
    auto *PVar = new ProgramVar(NK, PVD->getNameAsString(), FPS);
    insertProgramVar(NK, PVar);
    ParamDeclVarMap.insert(ParmMapItemType(ParamKey, NK));
    if (PVD->getType()->isPointerType())
      PointerBoundsKey.insert(NK);
  }
  return ParamDeclVarMap.left.at(ParamKey);
}

BoundsKey AVarBoundsInfo::getVariable(clang::FieldDecl *FD) {
  assert(isValidBoundVariable(FD) && "Not a valid bound declaration.");
  PersistentSourceLoc PSL = PersistentSourceLoc::mkPSL(FD, FD->getASTContext());
  if (!hasVarKey(PSL)) {
    BoundsKey NK = ++BCount;
    insertVarKey(PSL, NK);
    std::string StName = FD->getParent()->getNameAsString();
    StructScope *SS = StructScope::getStructScope(StName);
    auto *PVar = new ProgramVar(NK, FD->getNameAsString(), SS);
    insertProgramVar(NK, PVar);
    if (FD->getType()->isPointerType())
      PointerBoundsKey.insert(NK);
  }
  return getVarKey(PSL);
}

bool AVarBoundsInfo::addAssignment(clang::Decl *L, clang::Decl *R) {
  BoundsKey BL, BR;
  if (tryGetVariable(L, BL) && tryGetVariable(R, BR)) {
    return addAssignment(BL, BR);
  }
  return false;
}

bool AVarBoundsInfo::addAssignment(clang::DeclRefExpr *L,
                                   clang::DeclRefExpr *R) {
  return addAssignment(L->getDecl(), R->getDecl());
}

bool AVarBoundsInfo::addAssignment(BoundsKey L, BoundsKey R) {
  ProgVarGraph.addEdge(L, R);
  return true;
}

ProgramVar *AVarBoundsInfo::getProgramVar(BoundsKey VK) {
  ProgramVar *Ret = nullptr;
  if (PVarInfo.find(VK) != PVarInfo.end()) {
    Ret = PVarInfo[VK];
  }
  return Ret;
}

bool AVarBoundsInfo::hasVarKey(PersistentSourceLoc &PSL) {
  return DeclVarMap.left.find(PSL) != DeclVarMap.left.end();
}

BoundsKey AVarBoundsInfo::getVarKey(PersistentSourceLoc &PSL) {
  assert (hasVarKey(PSL) && "VarKey doesn't exist");
  return DeclVarMap.left.at(PSL);
}

BoundsKey AVarBoundsInfo::getConstKey(uint64_t value) {
  if (ConstVarKeys.find(value) == ConstVarKeys.end()) {
    BoundsKey NK = ++BCount;
    ConstVarKeys[value] = NK;
    std::string ConsString = std::to_string(value);
    ProgramVar *NPV = new ProgramVar(NK, ConsString,
                                     GlobalScope::getGlobalScope(), true);
    insertProgramVar(NK, NPV);
  }
  return ConstVarKeys[value];
}

BoundsKey AVarBoundsInfo::getVarKey(llvm::APSInt &API) {
  return getConstKey(API.abs().getZExtValue());
}

void AVarBoundsInfo::insertVarKey(PersistentSourceLoc &PSL, BoundsKey NK) {
  DeclVarMap.insert(DeclMapItemType(PSL, NK));
}

void AVarBoundsInfo::insertProgramVar(BoundsKey NK, ProgramVar *PV) {
  if (getProgramVar(NK) != nullptr) {
    // Free the already created variable.
    auto *E = PVarInfo[NK];
    delete (E);
  }
  PVarInfo[NK] = PV;
}

bool hasArray(std::set<ConstraintVariable *> &CSet, Constraints &CS) {
  for (auto *CK : CSet) {
    if (PVConstraint *PV = dyn_cast<PVConstraint>(CK)) {
      if (!PV->getCvars().empty()) {
        auto *CA = *(PV->getCvars().begin());
        auto *CAssign = CS.getAssignment(CA);
        if (CAssign == CS.getArr() || CAssign == CS.getNTArr()) {
          return true;
        }
      }
    }
  }
  return false;
}

// This class picks variables that are in the same scope as the provided scope.
class ScopeVisitor : public boost::default_bfs_visitor {
public:
  ScopeVisitor(ProgramVarScope *S, std::set<ProgramVar *> &R,
               std::map<BoundsKey, ProgramVar *> &VarM,
               std::set<BoundsKey> &P): TS(S), Res(R), VM(VarM)
               , PtrAtoms(P) { }
  template < typename Vertex, typename Graph >
  void discover_vertex(Vertex u, const Graph &g) const {
    BoundsKey V = g[u];
    // If the variable is non-pointer?
    if (VM.find(V) != VM.end() && PtrAtoms.find(V) == PtrAtoms.end()) {
      auto *S = VM[V];
      // If the variable is constant or in the same scope?
      if (S->IsNumConstant() ||
          (*(TS) == *(S->getScope()))) {
        Res.insert(S);
      }
    }
  }

  void filterOutBKeys(std::set<BoundsKey> &Src) {
    for (auto BK : Src) {
      // If the variable non-pointer?
      if (PtrAtoms.find(BK) == PtrAtoms.end()) {
        auto *S = VM[BK];
        // If the variable is constant or in the same scope?
        if (S->IsNumConstant() || (*(TS) == *(S->getScope()))) {
          Res.insert(S);
        }
      }
    }
  }
  ProgramVarScope *TS;
  std::set<ProgramVar *> &Res;
  std::map<BoundsKey, ProgramVar *> &VM;
  std::set<BoundsKey> &PtrAtoms;
};

bool AvarBoundsInference::intersectBounds(std::set<ProgramVar *> &ProgVars,
                                          ABounds::BoundsKind BK,
                                          std::set<ABounds *> &CurrB) {
  std::set<ABounds *> CommonNewBounds;
  for (auto *PVar : ProgVars) {
    ABounds *NewB = nullptr;
    if (BK == ABounds::CountBoundKind) {
      NewB = new CountBound(PVar->getKey());
    } else if (BK == ABounds::ByteBoundKind) {
      NewB = new ByteBound(PVar->getKey());
    } else {
      continue;
    }
    assert(NewB != nullptr && "New Bounds cannot be nullptr");
    if (CurrB.empty()) {
      CommonNewBounds.insert(NewB);
    } else {
      bool found = false;
      for (auto *OB : CurrB) {
        if (OB->areSame(NewB)) {
          found = true;
          CommonNewBounds.insert(NewB);
          break;
        }
      }
      if (!found) {
        delete (NewB);
      }
    }
  }

  for (auto *D : CurrB) {
    delete(D);
  }
  CurrB.clear();
  CurrB.insert(CommonNewBounds.begin(), CommonNewBounds.end());
  return !CurrB.empty();
}

bool AvarBoundsInference::inferPossibleBounds(BoundsKey K, ABounds *SB,
                                              std::set<ABounds *> &EB,
                                              bool IsSucc) {
  bool RetVal = false;
  if (SB != nullptr) {
    auto &VarG = BI->ProgVarGraph;
    auto *Kvar = BI->getProgramVar(K);
    bool ValidB = false;
    auto BKind = SB->getKind();
    BoundsKey SBKey;
    if (CountBound *CB = dyn_cast<CountBound>(SB)) {
      ValidB = true;
      SBKey = CB->getCountVar();
    } else if (ByteBound *BB = dyn_cast<ByteBound>(SB)) {
      ValidB = true;
      SBKey = BB->getByteVar();
    }

    std::set<ProgramVar *> PotentialB;
    // If we can handle the bounds?
    if (ValidB) {
      // First, find all the in-scope variable to which the SBKey flow to.
      auto *SBVar = BI->getProgramVar(SBKey);
      if (SBVar->IsNumConstant()) {
        PotentialB.insert(SBVar);
      } else {
        ScopeVisitor TV(Kvar->getScope(), PotentialB, BI->PVarInfo,
                        BI->PointerBoundsKey);
        if (!IsSucc) {
          // If we are looking at predecessor ARR atom? get all variables
          // that the predecessors bound information flows to.
          auto Vidx = VarG.addVertex(SBKey);
          boost::breadth_first_search(VarG.CG, Vidx, boost::visitor(TV));
        } else {
          // If we are looking at sucessors ARR Atom? The find all the
          // variables that flow into the sucessors atom.
          std::set<BoundsKey> AllPredKeys;
          VarG.getPredecessors(SBKey, AllPredKeys);
          TV.filterOutBKeys(AllPredKeys);
        }
      }

      RetVal = intersectBounds(PotentialB, BKind, EB);
    }
  }

  return RetVal;
}

bool AvarBoundsInference::getRelevantBounds(std::set<BoundsKey> &RBKeys,
                                            std::set<BoundsKey> &ArrAtoms,
                                            std::set<ABounds *> &ResBounds,
                                            bool IsSucc) {
  std::set<BoundsKey> IncomingArrs;
  // First, get all the related boundskeys that are arrays.
  std::set_intersection(RBKeys.begin(), RBKeys.end(),
                        ArrAtoms.begin(), ArrAtoms.end(),
                        std::inserter(IncomingArrs, IncomingArrs.end()));

  // Next, try to get their bounds.
  bool ValidB = true;
  for (auto PrevBKey : IncomingArrs) {
    auto *PrevBounds = BI->getBounds(PrevBKey);
    // Does the parent arr has bounds?
    if (!IsSucc && PrevBounds  == nullptr) {
      ValidB = false;
      break;
    }
    if (PrevBounds != nullptr)
      ResBounds.insert(PrevBounds);
  }
  return ValidB;
}

bool AvarBoundsInference::predictBounds(BoundsKey K,
                                        std::set<BoundsKey> &Neighbours,
                                        std::set<BoundsKey> &ArrAtoms,
                                        ABounds **KB,
                                        bool IsSucc) {
  std::set<ABounds *> ResBounds;
  std::set<ABounds *> KBounds;
  *KB = nullptr;
  bool IsValid = true;
  // Get all the relevant bounds from the neighbour ARRs
  if (getRelevantBounds(Neighbours, ArrAtoms, ResBounds, IsSucc)) {
    if (!ResBounds.empty()) {
      // Find the intersection?
      for (auto *B : ResBounds) {
        inferPossibleBounds(K, B, KBounds, IsSucc);
        //TODO: check this
        // This is stricter version i.e., there should be at least one common
        // bounds information from an incoming ARR.
        /*if (!inferPossibleBounds(K, B, KBounds)) {
          ValidB = false;
          break;
        }*/
      }
      // If we converge to single bounds information? We found the bounds.
      if (KBounds.size() == 1) {
        *KB = *KBounds.begin();
        KBounds.clear();
      } else {
        IsValid = false;
        // TODO: Should we give up when we have multiple bounds?
        for (auto *T : KBounds) {
          delete(T);
        }
      }
    }
  }
  return IsValid;
}
bool AvarBoundsInference::inferBounds(BoundsKey K,
                                      std::set<BoundsKey> &ArrAtoms) {
  bool IsChanged = false;

  if (BI->InvalidBounds.find(K) == BI->InvalidBounds.end()) {
    std::set<BoundsKey> TmpBkeys;
    // Try to predict bounds from successors.
    BI->ProgVarGraph.getSuccessors(K, TmpBkeys);

    ABounds *KB = nullptr;
    if (!predictBounds(K, TmpBkeys, ArrAtoms, &KB, true) ||
        KB == nullptr) {
      // If it is not possible to predict from successors?
      // Try to predict from predecessors.
      TmpBkeys.clear();
      BI->ProgVarGraph.getPredecessors(K, TmpBkeys);
      predictBounds(K, TmpBkeys, ArrAtoms, &KB, false);
    }
    if (KB != nullptr) {
      BI->replaceBounds(K, KB);
      IsChanged = true;
    }
  }
  return IsChanged;
}

bool AVarBoundsInfo::performFlowAnalysis(ProgramInfo *PI) {
  bool RetVal = false;
  auto &CS = PI->getConstraints();
  // First get all the pointer vars which are ARRs
  std::set<BoundsKey> ArrPointers;
  for (auto Bkey : PointerBoundsKey) {
    auto &BkeyToPSL = DeclVarMap.right;
    if (BkeyToPSL.find(Bkey) != BkeyToPSL.end()) {
      auto &PSL = BkeyToPSL.at(Bkey);
      if (hasArray(PI->getVarMap()[PSL], CS)) {
        ArrPointers.insert(Bkey);
      }
      continue;
    }
    auto &ParmBkeyToPSL = ParamDeclVarMap.right;
    if (ParmBkeyToPSL.find(Bkey) != ParmBkeyToPSL.end()) {
      auto &ParmTup = ParmBkeyToPSL.at(Bkey);
      std::string FuncName = std::get<0>(ParmTup);
      std::string FileName = std::get<1>(ParmTup);
      bool IsStatic = std::get<2>(ParmTup);
      unsigned ParmNum = std::get<3>(ParmTup);
      FVConstraint *FV = nullptr;
      if (IsStatic) {
        FV = getOnly(*(PI->getStaticFuncConstraintSet(FuncName, FileName)));
      } else {
        FV = getOnly(*(PI->getExtFuncDefnConstraintSet(FuncName)));
      }

      if (hasArray(FV->getParamVar(ParmNum), CS)) {
        ArrPointers.insert(Bkey);
      }
      continue;
    }
  }


  // Next, get the ARR pointers that has bounds.

  // These are pointers with bounds.
  std::set<BoundsKey> ArrWithBounds;
  for (auto &T : BInfo) {
    ArrWithBounds.insert(T.first);
  }

  // This are the array atoms that need bounds.
  // i.e., ArrNeededBounds = ArrPtrs - ArrPtrsWithBounds.
  std::set<BoundsKey> ArrNeededBounds;
  std::set_difference(ArrPointers.begin(), ArrPointers.end(),
                      ArrWithBounds.begin(), ArrWithBounds.end(),
                      std::inserter(ArrNeededBounds, ArrNeededBounds.end()));

  // Now compute the bounds information of all the ARR pointers that need it.
  std::set<BoundsKey> WorkList;
  // First, populate worklist with all the ARRs that need bounds.
  WorkList.insert(ArrNeededBounds.begin(), ArrNeededBounds.end());

  AvarBoundsInference BI(this);
  std::set<BoundsKey> NextIterArrs;
  bool Changed = true;
  while (Changed) {
    Changed = false;
    NextIterArrs.clear();
    // Are there any ARR atoms that need bounds?
    while (!WorkList.empty()) {
      BoundsKey CurrArrKey = *WorkList.begin();
      // Remove the bounds key from the worklist.
      WorkList.erase(CurrArrKey);
      // Can we find bounds for this Arr?
      if (BI.inferBounds(CurrArrKey, ArrPointers)) {
        // Record the stats.
        BoundsInferStats.DataflowMatch.insert(CurrArrKey);
        // We found the bounds.
        ArrNeededBounds.erase(CurrArrKey);
        Changed = true;
        // Get all the successors of the ARR whose bounds we just found.
        ProgVarGraph.getSuccessors(CurrArrKey, NextIterArrs);
      }
    }
    if (Changed) {
      std::set_intersection(ArrNeededBounds.begin(), ArrNeededBounds.end(),
                            NextIterArrs.begin(), NextIterArrs.end(),
                            std::inserter(WorkList, WorkList.end()));
    }
  }

  return RetVal;
}
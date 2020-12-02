//=--ConstraintBuilder.cpp----------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Implementation of visitor methods for the FunctionVisitor class. These
// visitors create constraints based on the AST of the program.
//===----------------------------------------------------------------------===//

#include "clang/CConv/ConstraintBuilder.h"
#include "clang/CConv/ConstraintResolver.h"
#include "clang/CConv/ArrayBoundsInferenceConsumer.h"
#include "clang/CConv/CCGlobalOptions.h"

using namespace llvm;
using namespace clang;

// Used to keep track of in-line struct defs
unsigned int lastRecordLocation = -1;

void processRecordDecl(RecordDecl *Declaration, ProgramInfo &Info,
    ASTContext *Context, ConstraintResolver CB) {
  if (RecordDecl *Definition = Declaration->getDefinition()) {
    // store current record's location to cross-ref later in a VarDecl
    lastRecordLocation = Definition->getBeginLoc().getRawEncoding();
    FullSourceLoc FL = Context->getFullLoc(Definition->getBeginLoc());
    if (FL.isValid()) {
      SourceManager &SM = Context->getSourceManager();
      FileID FID = FL.getFileID();
      const FileEntry *FE = SM.getFileEntryForID(FID);
      if (FE && FE->isValid()) {
        // We only want to re-write a record if it contains
        // any pointer types, to include array types.
        for (const auto &D : Definition->fields()) {
          Info.getABoundsInfo().insertVariable(D);
          if (D->getType()->isPointerType() || D->getType()->isArrayType()) {
            Info.addVariable(D, Context);
            if(FL.isInSystemHeader() || Definition->isUnion()) {
              CVarSet C = Info.getVariable(D, Context);
              std::string Rsn = "External struct field or union encountered";
              CB.constraintAllCVarsToWild(C, Rsn, nullptr);
            }
          }
        }
      }
    }
  }
}

// This class visits functions and adds constraints to the
// Constraints instance assigned to it.
// Each VisitXXX method is responsible for looking inside statements
// and imposing constraints on variables it uses
class FunctionVisitor : public RecursiveASTVisitor<FunctionVisitor> {
public:
  explicit FunctionVisitor(ASTContext *C,
                           ProgramInfo &I,
                           FunctionDecl *FD,
                           TypeVariableBindingsMapT &TVMap)
      : Context(C), Info(I), Function(FD), CB(Info, Context),
        TypeVariableBindings(TVMap) {}

  // T x = e
  bool VisitDeclStmt(DeclStmt *S) {
    // Introduce variables as needed.
    for (const auto &D : S->decls()) {
      if(RecordDecl *RD = dyn_cast<RecordDecl>(D)) {
        processRecordDecl(RD, Info, Context, CB);
      }
      if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
        if (VD->isLocalVarDecl()) {
          Info.getABoundsInfo().insertVariable(VD);
          FullSourceLoc FL = Context->getFullLoc(VD->getBeginLoc());
          SourceRange SR = VD->getSourceRange();
          if (SR.isValid() && FL.isValid() &&
              (VD->getType()->isPointerType() ||
               VD->getType()->isArrayType())) {
            Info.addVariable(VD, Context);
            if (lastRecordLocation == VD->getBeginLoc().getRawEncoding()) {
              CVarSet C = Info.getVariable(VD, Context);
              CB.constraintAllCVarsToWild(C, "Inline struct encountered.", nullptr);
            }
          }
        }
      }
    }

    // Process inits even for non-pointers because structs and union values
    // can contain pointers
    for (const auto &D : S->decls()) {
      if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
        Expr *InitE = VD->getInit();
        CB.constrainLocalAssign(S, VD, InitE);
      }
    }

    return true;
  }

  // (T)e
  bool VisitCStyleCastExpr(CStyleCastExpr *C) {
    // Is cast compatible with LHS type?
    QualType SrcT = C->getSubExpr()->getType();
    QualType DstT = C->getType();
    if (!isCastSafe(DstT, SrcT)) {
      auto CVs = CB.getExprConstraintVars(C->getSubExpr());
      std::string Rsn = "Casted from " +
                        SrcT.getAsString() +  " to " +
                        DstT.getAsString();
      CB.constraintAllCVarsToWild(CVs, Rsn, C);
    }
    return true;
  }

  // Cast expressions must be visited to find generic functions where the return
  // can be given a concrete type.
  bool VisitCastExpr(CastExpr *CE){
    Expr *SubExpr = CE->getSubExpr();
    if (CHKCBindTemporaryExpr *TempE = dyn_cast<CHKCBindTemporaryExpr>(SubExpr))
      SubExpr = TempE->getSubExpr();

    if (auto *Call = dyn_cast<CallExpr>(SubExpr))
      if (auto *FD = dyn_cast_or_null<FunctionDecl>(Call->getCalleeDecl()))
        if (const auto *TyVar = getTypeVariableType(FD))
          insertTypeParamBinding(Call, TyVar, CE->getType().getTypePtr());
    return true;
  }

  // x += e
  bool VisitCompoundAssignOperator(CompoundAssignOperator *O) {
    switch(O->getOpcode()) {
    case BO_AddAssign:
    case BO_SubAssign:
      arithBinop(O);
      break;
    // rest shouldn't happen on pointers, so we ignore
    default:
      break;
    }
    return true;
  }

  // x = e
  bool VisitBinAssign(BinaryOperator *O) {
    Expr *LHS = O->getLHS();
    Expr *RHS = O->getRHS();
    CB.constrainLocalAssign(O, LHS, RHS, Same_to_Same);
    return true;
  }

  // e(e1,e2,...)
  bool VisitCallExpr(CallExpr *E) {
    Decl *D = E->getCalleeDecl();
    PersistentSourceLoc PL = PersistentSourceLoc::mkPSL(E, *Context);
    auto &CS = Info.getConstraints();
    CVarSet FVCons;
    std::string FuncName = "";
    FunctionDecl *TFD = nullptr;

    // figure out who we are calling
    if (D == nullptr) {
      // If the callee declaration could not be found, then we're doing some
      // sort of indirect call through an array or conditional.
      Expr *CalledExpr = E->getCallee();
      FVCons = CB.getExprConstraintVars(CalledExpr);
      // When multiple function variables are used in the same expression, they
      // must have the same type.
      if (FVCons.size() > 1) {
        PersistentSourceLoc PL =
            PersistentSourceLoc::mkPSL(CalledExpr, *Context);
        constrainConsVarGeq(FVCons, FVCons, Info.getConstraints(), &PL,
                            Same_to_Same, false, &Info);
      }
    } else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      FuncName = FD->getNameAsString();
      FVCons = Info.getVariable(FD, Context);
      TFD = FD;
    } else if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
      FVCons = Info.getVariable(DD, Context);
      FuncName = DD->getNameAsString();
    }

    // Now do the call: Constrain arguments to parameters (but ignore returns)
    if (FVCons.empty()) {
      // Don't know who we are calling; make args WILD
      constraintAllArgumentsToWild(E);
    } else if (!ConstraintResolver::canFunctionBeSkipped(FuncName)) {
      // FIXME: realloc comparison is still required. See issue #176.
      // If we are calling realloc, ignore it, so as not to constrain the first arg
      // Else, for each function we are calling ...
      for (auto *TmpC : FVCons) {
        if (PVConstraint *PVC = dyn_cast<PVConstraint>(TmpC)) {
          TmpC = PVC->getFV();
          assert(TmpC != nullptr && "Function pointer with null FVConstraint.");
        }
        // and for each arg to the function ...
        if (FVConstraint *TargetFV = dyn_cast<FVConstraint>(TmpC)) {
          // Collect type parameters for this function call that are
          // consistently instantiated as single type in this function call.
          std::set<unsigned int> consistentTypeParams;
          if (TFD != nullptr)
            getConsistentTypeParams(E, TFD, consistentTypeParams);

          unsigned i = 0;
          for (const auto &A : E->arguments()) {
            CVarSet ArgumentConstraints;
            if(TFD != nullptr && i < TFD->getNumParams()) {
              // Remove casts to void* on polymorphic types that are used
              // consistently.
              const auto *Ty = getTypeVariableType(TFD->getParamDecl(i));
              if (Ty != nullptr && consistentTypeParams.find(Ty->GetIndex())
                  != consistentTypeParams.end())
                ArgumentConstraints =
                    CB.getExprConstraintVars(A->IgnoreImpCasts());
              else
                ArgumentConstraints = CB.getExprConstraintVars(A);
            } else
              ArgumentConstraints = CB.getExprConstraintVars(A);

            // constrain the arg CV to the param CV
            if (i < TargetFV->numParams()) {
              CVarSet ParameterDC =
                  TargetFV->getParamVar(i);
              constrainConsVarGeq(ParameterDC, ArgumentConstraints, CS, &PL,
                                  Wild_to_Safe, false, &Info);
              if (AllTypes && TFD != nullptr &&
                  !CB.containsValidCons(ParameterDC) &&
                  !CB.containsValidCons(ArgumentConstraints)) {
                auto *PVD = TFD->getParamDecl(i);
                auto &ABI = Info.getABoundsInfo();
                BoundsKey PVKey, AGKey;
                if ((CB.resolveBoundsKey(ParameterDC, PVKey) ||
                     ABI.tryGetVariable(PVD, PVKey)) &&
                    (CB.resolveBoundsKey(ArgumentConstraints, AGKey) ||
                     ABI.tryGetVariable(A, *Context, AGKey))) {
                  ABI.addAssignment(PVKey, AGKey);
                }
              }
            } else {
              // The argument passed to a function ith varargs; make it wild
              if (HandleVARARGS) {
                CB.constraintAllCVarsToWild(ArgumentConstraints,
                                            "Passing argument to a function "
                                            "accepting var args.",
                                            E);
              } else {
                if (Verbose) {
                  std::string FuncName = TargetFV->getName();
                  errs() << "Ignoring function as it contains varargs:"
                         << FuncName << "\n";
                }
              }
            }
            i++;
          }
        }
      }
    }
    return true;
  }

  // e1[e2]
  bool VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    Constraints &CS = Info.getConstraints();
    constraintInBodyVariable(E->getBase(), CS.getArr());
    return true;
  }

  // return e;
  bool VisitReturnStmt(ReturnStmt *S) {
    // Get function variable constraint of the body
    PersistentSourceLoc PL =
        PersistentSourceLoc::mkPSL(S, *Context);
    CVarSet Fun =
        Info.getVariable(Function, Context);

    // Constrain the value returned (if present) against the return value
    // of the function.
    Expr *RetExpr = S->getRetValue();

    CVarSet RconsVar = CB.getExprConstraintVars(RetExpr);
    // Constrain the return type of the function
    // to the type of the return expression.
    for (const auto &F : Fun) {
      if (FVConstraint *FV = dyn_cast<FVConstraint>(F)) {
        // This is to ensure that the return type of the function is same
        // as the type of return expression.
        constrainConsVarGeq(FV->getReturnVars(), RconsVar,
                            Info.getConstraints(), &PL, Same_to_Same, false,
                            &Info);
      }
    }
    return true;
  }

  // ++x
  bool VisitUnaryPreInc(UnaryOperator *O) {
    constraintPointerArithmetic(O->getSubExpr());
    return true;
  }

  // x++
  bool VisitUnaryPostInc(UnaryOperator *O) {
    constraintPointerArithmetic(O->getSubExpr());
    return true;
  }

  // --x
  bool VisitUnaryPreDec(UnaryOperator *O) {
    constraintPointerArithmetic(O->getSubExpr());
    return true;
  }

  // x--
  bool VisitUnaryPostDec(UnaryOperator *O) {
    constraintPointerArithmetic(O->getSubExpr());
    return true;
  }

  // e1 + e2
  bool VisitBinAdd(BinaryOperator *O) {
    arithBinop(O);
    return true;
  }

  // e1 - e2
  bool VisitBinSub(BinaryOperator *O) {
    arithBinop(O);
    return true;
  }

private:

  // Constraint all the provided vars to be
  // equal to the provided type i.e., (V >= type).
  void constrainVarsTo(CVarSet &Vars,
                       ConstAtom *CAtom) {
    Constraints &CS = Info.getConstraints();
    for (const auto &I : Vars)
      if (PVConstraint *PVC = dyn_cast<PVConstraint>(I)) {
        PVC->constrainOuterTo(CS, CAtom);
      }
  }

  // Constraint helpers.
  void constraintInBodyVariable(Expr *e, ConstAtom *CAtom) {
    CVarSet Var = CB.getExprConstraintVars(e);
    constrainVarsTo(Var, CAtom);
  }

  // Constraint all the argument of the provided
  // call expression to be WILD.
  void constraintAllArgumentsToWild(CallExpr *E) {
    PersistentSourceLoc psl = PersistentSourceLoc::mkPSL(E, *Context);
    for (const auto &A : E->arguments()) {
      // Get constraint from within the function body
      // of the caller.
      CVarSet ParameterEC = CB.getExprConstraintVars(A);

      // Assign WILD to each of the constraint variables.
      FunctionDecl *FD = E->getDirectCallee();
      std::string Rsn = "Argument to function " +
                        (FD != nullptr ? FD->getName().str() : "pointer call");
      Rsn += " with out Constraint vars.";
      CB.constraintAllCVarsToWild(ParameterEC, Rsn, E);
    }
  }

  void arithBinop(BinaryOperator *O) {
      constraintPointerArithmetic(O->getLHS());
      constraintPointerArithmetic(O->getRHS());
  }

  // Pointer arithmetic constrains the expression to be at least ARR,
  // unless it is on a function pointer. In this case the function pointer
  // is WILD.
  void constraintPointerArithmetic(Expr *E) {
    if (E->getType()->isFunctionPointerType()) {
      CVarSet Var = CB.getExprConstraintVars(E);
      std::string Rsn = "Pointer arithmetic performed on a function pointer.";
      CB.constraintAllCVarsToWild(Var, Rsn, E);
    } else {
      constraintInBodyVariable(E, Info.getConstraints().getArr());
    }
  }

  // Collect the set of TypeVariableTypes that are always used for arguments
  // with the same type. These are type variables that can be instantiated with
  // a concrete type, so it is correct to remove casts to void* on their
  // arguments.
  void getConsistentTypeParams(CallExpr *CE,
                               FunctionDecl *FD,
                               std::set<unsigned int> &Types) {
    assert("Must provide nonnull FunctionDecl." && FD);
    // Construct a map from TypeVariables to a single type they are consistently
    // used as. If there is no single consistent type for a variable, then it
    // maps to nullptr.
    unsigned int I = 0;
    for (auto *const A : CE->arguments()) {
      // This can happen with varargs
      if (I >= FD->getNumParams())
        break;
      if (const auto *TyVar = getTypeVariableType(FD->getParamDecl(I))) {
        const clang::Type *Ty = A->IgnoreImpCasts()->getType().getTypePtr();
        insertTypeParamBinding(CE, TyVar, Ty);
      }
      ++I;
    }

    // Gather consistent TypeVariables into output set
    auto &CallTypeVarBindings = TypeVariableBindings[CE];
    for (const auto &TVEntry : CallTypeVarBindings)
      if(TVEntry.second != nullptr)
        Types.insert(TVEntry.first);
  }

  void insertTypeParamBinding(CallExpr *CE, const TypeVariableType *TyVar,
                              const clang::Type *Ty) {
    assert("Type param must go to pointer." && Ty->isPointerType());

    auto &CallTypeVarBindings = TypeVariableBindings[CE];
    QualType PointeeType = Ty->getPointeeType();
    if (PointeeType->isRecordType() &&
        !(PointeeType->getAsRecordDecl()->getIdentifier() ||
            PointeeType->getAsRecordDecl()->getTypedefNameForAnonDecl())) {
      // We'll need a name to provide the type arguments during rewriting, so
      // no anonymous things here.
      CallTypeVarBindings[TyVar->GetIndex()] = nullptr;
    } if (CallTypeVarBindings.find(TyVar->GetIndex())
        == CallTypeVarBindings.end()) {
      // If the type variable hasn't been seen before, add it to the map.
      CallTypeVarBindings[TyVar->GetIndex()] = Ty;
    } else if (CallTypeVarBindings[TyVar->GetIndex()] != Ty) {
      // If it has previously been instantiated as a different type, its use
      // is not consistent.
      CallTypeVarBindings[TyVar->GetIndex()] = nullptr;
    }
    // If neither branch is taken, then the type variable has been
    // encountered before with the same type. Nothing needs to be done.
  }

  ASTContext *Context;
  ProgramInfo &Info;
  FunctionDecl *Function;
  ConstraintResolver CB;
  TypeVariableBindingsMapT &TypeVariableBindings;
};

// This class visits a global declaration, generating constraints
//   for functions, variables, types, etc. that are visited
class GlobalVisitor : public RecursiveASTVisitor<GlobalVisitor> {
public:
  explicit GlobalVisitor(ASTContext *Context,
                         ProgramInfo &I,
                         TypeVariableBindingsMapT &TVMap)
      : Context(Context), Info(I), CB(Info, Context),
        TypeVariableBindings(TVMap) {}

  bool VisitVarDecl(VarDecl *G) {

    if (G->hasGlobalStorage() &&
        (G->getType()->isPointerType() || G->getType()->isArrayType())) {
      Info.getABoundsInfo().insertVariable(G);
      Info.addVariable(G, Context);
      if (G->hasInit()) {
        CB.constrainLocalAssign(nullptr, G, G->getInit());
      }
      // If the location of the previous RecordDecl and the current VarDecl
      // coincide with one another, we constrain the VarDecl to be wild
      // in order to allow the fields of the RecordDecl to be converted
      unsigned int BeginLoc = G->getBeginLoc().getRawEncoding();
      unsigned int EndLoc = G->getEndLoc().getRawEncoding();
      if (lastRecordLocation >= BeginLoc && lastRecordLocation <= EndLoc) {
        CVarSet C = Info.getVariable(G, Context);
        CB.constraintAllCVarsToWild(C, "Inline struct encountered.", nullptr);
      }
    }

    return true;
  }

  bool VisitInitListExpr(InitListExpr *E){
    if (E->getType()->isStructureType()) {
      const RecordDecl *Definition =
          E->getType()->getAsStructureType()->getDecl()->getDefinition();

      unsigned int initIdx = 0;
      const auto fields = Definition->fields();
      for (auto it = fields.begin(); initIdx < E->getNumInits() && it != fields.end(); initIdx++, it++) {
        Expr *InitExpr = E->getInit(initIdx);
        CB.constrainLocalAssign(nullptr, *it, InitExpr);
      }
    }
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *D) {
    FullSourceLoc FL = Context->getFullLoc(D->getBeginLoc());

    if (Verbose)
      errs() << "Analyzing function " << D->getName() << "\n";

    if (FL.isValid()) { // TODO: When would this ever be false?
      Info.addVariable(D, Context);
      if (D->hasBody() && D->isThisDeclarationADefinition()) {
        Stmt *Body = D->getBody();
        FunctionVisitor
            FV = FunctionVisitor(Context, Info, D, TypeVariableBindings);
        FV.TraverseStmt(Body);
        if (AllTypes) {
          // Only do this, if all types is enabled.
          LengthVarInference LVI(Info, Context, D);
          LVI.Visit(Body);
        }
      }
    }

    if (Verbose)
      errs() << "Done analyzing function\n";

    return true;
  }

  bool VisitRecordDecl(RecordDecl *Declaration) {
    processRecordDecl(Declaration, Info, Context, CB);
    return true;
  }

private:
  ASTContext *Context;
  ProgramInfo &Info;
  ConstraintResolver CB;
  TypeVariableBindingsMapT &TypeVariableBindings;
};

// Store type param bindings persistently in ProgramInfo so they are available
// during rewriting.
void ConstraintBuilderConsumer::SetProgramInfoTypeVars
   (TypeVariableBindingsMapT TypeVariableBindings, ASTContext &C) {
  for (const auto &TVEntry : TypeVariableBindings) {
    bool AllNull = true;
    for (auto TVCallEntry : TVEntry.second)
      AllNull &= TVCallEntry.second == nullptr;
    if (!AllNull) {
      for (auto TVCallEntry : TVEntry.second)
        if (TVCallEntry.second != nullptr) {
          std::string TyStr = TVCallEntry.second->getPointeeType().getAsString();
          Info.setTypeParamBinding(TVEntry.first, TVCallEntry.first, TyStr, &C);
        } else
          Info.setTypeParamBinding(TVEntry.first, TVCallEntry.first, "void", &C);
    }
  }
}

void ConstraintBuilderConsumer::HandleTranslationUnit(ASTContext &C) {
  Info.enterCompilationUnit(C);
  if (Verbose) {
    SourceManager &SM = C.getSourceManager();
    FileID MainFileId = SM.getMainFileID();
    const FileEntry *FE = SM.getFileEntryForID(MainFileId);
    if (FE != nullptr)
      errs() << "Analyzing file " << FE->getName() << "\n";
    else
      errs() << "Analyzing\n";
  }
  TypeVariableBindingsMapT TypeVariableBindings;
  GlobalVisitor GV = GlobalVisitor(&C, Info, TypeVariableBindings);
  TranslationUnitDecl *TUD = C.getTranslationUnitDecl();
  // Generate constraints.
  for (const auto &D : TUD->decls()) {
    GV.TraverseDecl(D);
  }

  SetProgramInfoTypeVars(TypeVariableBindings, C);

  if (Verbose)
    outs() << "Done analyzing\n";

  Info.exitCompilationUnit();
  return;
}



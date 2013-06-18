//===- Scope.cpp - Lexical scope information ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Scope class, which is used for recording information
// about a lexical scope.
//
//===----------------------------------------------------------------------===//

#include "flang/Sema/Scope.h"
#include "flang/AST/Expr.h"
#include <limits>

namespace flang {

static StmtLabelInteger GetStmtLabelValue(Expr *E) {
  if(IntegerConstantExpr *IExpr =
     dyn_cast<IntegerConstantExpr>(E)) {
    return StmtLabelInteger(
          IExpr->getValue().getLimitedValue(
            std::numeric_limits<StmtLabelInteger>::max()));
  } else {
    llvm_unreachable("Invalid stmt label expression");
    return 0;
  }
}

/// \brief Declares a new statement label.
void StmtLabelScope::Declare(Expr *StmtLabel, Stmt *Statement) {
  auto Key = GetStmtLabelValue(StmtLabel);
  StmtLabelDeclsInScope.insert(std::make_pair(Key,Statement));
}

/// \brief Tries to resolve a statement label reference.
Stmt *StmtLabelScope::Resolve(Expr *StmtLabel) const {
  auto Key = GetStmtLabelValue(StmtLabel);
  auto Result = StmtLabelDeclsInScope.find(Key);
  if(Result == StmtLabelDeclsInScope.end()) return 0;
  else return Result->second;
}

/// \brief Declares a forward reference of some statement label.
void StmtLabelScope::DeclareForwardReference(StmtLabelForwardDecl Reference) {
  ForwardStmtLabelDeclsInScope.append(1,Reference);
}

void StmtLabelScope::reset() {
  StmtLabelDeclsInScope.clear();
  ForwardStmtLabelDeclsInScope.clear();
}

void Scope::Init(Scope *parent, unsigned flags) {
  AnyParent = parent;
  Flags = flags;

  if (parent) {
    Depth          = parent->Depth + 1;
    PrototypeDepth = parent->PrototypeDepth;
    PrototypeIndex = 0;
    FnParent       = parent->FnParent;
  } else {
    Depth = 0;
    PrototypeDepth = 0;
    PrototypeIndex = 0;
    FnParent = 0;
  }

  // If this scope is a function or contains breaks/continues, remember it.
  if (flags & FnScope) FnParent = this;

  DeclsInScope.clear();
  Entity = 0;
  ErrorTrap.reset();
}

} //namespace flang

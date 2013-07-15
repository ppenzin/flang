//===--- ASTContext.cpp - Context to hold long-lived AST nodes ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#include "flang/AST/ASTContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/Support/ErrorHandling.h"


namespace flang {

ASTContext::ASTContext(llvm::SourceMgr &SM, LangOptions LangOpts)
  : SrcMgr(SM), LastSDM(0), LanguageOptions(LangOpts) {
  TUDecl = TranslationUnitDecl::Create(*this);
  InitBuiltinTypes();
}

ASTContext::~ASTContext() {
  // Release the DenseMaps associated with DeclContext objects.
  // FIXME: Is this the ideal solution?
  ReleaseDeclContextMaps();
}

void ASTContext::InitBuiltinType(QualType &R, BuiltinType::TypeSpec K) {
  BuiltinType *Ty = new (*this, TypeAlignment) BuiltinType(K);
  R = QualType(Ty, 0);
  Types.push_back(Ty);
}

void ASTContext::InitBuiltinTypes() {
  // [R404]
  InitBuiltinType(IntegerTy,         BuiltinType::Integer);
  InitBuiltinType(RealTy,            BuiltinType::Real);
  DoublePrecisionTy = getExtQualType(RealTy.getTypePtr(), Qualifiers(),
                                     BuiltinType::Real8, true, false, 0);
  InitBuiltinType(ComplexTy,         BuiltinType::Complex);
  DoubleComplexTy = getExtQualType(ComplexTy.getTypePtr(), Qualifiers(),
                                   BuiltinType::Real8, true, false, 0);
  InitBuiltinType(CharacterTy,       BuiltinType::Character);
  InitBuiltinType(LogicalTy,         BuiltinType::Logical);
}

QualType ASTContext::getBuiltinQualType(BuiltinType::TypeSpec TS) const {
  switch (TS) {
  case BuiltinType::Invalid: assert(false && "Invalid type spec!"); break;
  case BuiltinType::Integer:         return IntegerTy;
  case BuiltinType::Real:            return RealTy;
  case BuiltinType::Character:       return CharacterTy;
  case BuiltinType::Logical:         return LogicalTy;
  case BuiltinType::Complex:         return ComplexTy;
  }
  return QualType();
}

const llvm::fltSemantics&  ASTContext::getFPTypeSemantics(QualType Type) {
  switch(getRealOrComplexTypeKind(Type.getExtQualsPtrOrNull(), Type)) {
  case BuiltinType::Real4:  return llvm::APFloat::IEEEsingle;
  case BuiltinType::Real8:  return llvm::APFloat::IEEEdouble;
  case BuiltinType::Real16: return llvm::APFloat::IEEEquad;
  }
  llvm_unreachable("invalid real type");
}

unsigned ASTContext::getTypeKindBitWidth(BuiltinType::TypeKind Kind) const {
  switch(Kind) {
  case BuiltinType::Int1: return 8;
  case BuiltinType::Int2: return 16;
  case BuiltinType::Int4: return 32;
  case BuiltinType::Int8: return 64;
  case BuiltinType::Real4: return 32;
  case BuiltinType::Real8: return 64;
  case BuiltinType::Real16: return 128;
  }
  llvm_unreachable("invalid built in type kind");
  return 0;
}

//===----------------------------------------------------------------------===//
//                   Type creation/memoization methods
//===----------------------------------------------------------------------===//

QualType ASTContext::getExtQualType(const Type *BaseType, Qualifiers Quals,
                                    unsigned KindSel, bool IsDoublePrecisionKind,
                                    bool IsStarLength,
                                    unsigned LenSel) const {
  // Check if we've already instantiated this type.
  llvm::FoldingSetNodeID ID;
  ExtQuals::Profile(ID, BaseType, Quals, KindSel, IsDoublePrecisionKind, IsStarLength, LenSel);
  void *InsertPos = 0;
  if (ExtQuals *EQ = ExtQualNodes.FindNodeOrInsertPos(ID, InsertPos)) {
    assert(EQ->getQualifiers() == Quals);
    return QualType(EQ, 0);
  }

  // If the base type is not canonical, make the appropriate canonical type.
  QualType Canon;
  if (!BaseType->isCanonicalUnqualified()) {
    SplitQualType CanonSplit = BaseType->getCanonicalTypeInternal().split();
    CanonSplit.second.addConsistentQualifiers(Quals);
    Canon = getExtQualType(CanonSplit.first, CanonSplit.second, KindSel,
                           IsDoublePrecisionKind, IsStarLength, LenSel);

    // Re-find the insert position.
    (void) ExtQualNodes.FindNodeOrInsertPos(ID, InsertPos);
  }

  ExtQuals *EQ = new (*this, TypeAlignment) ExtQuals(BaseType, Canon, Quals,
                                                     KindSel, IsDoublePrecisionKind,
                                                     IsStarLength, LenSel);
  ExtQualNodes.InsertNode(EQ, InsertPos);
  return QualType(EQ, 0);
}

QualType ASTContext::getQualTypeOtherKind(QualType Type, QualType KindType) {
  auto ExtQuals = Type.getExtQualsPtrOrNull();
  auto DesiredExtQuals = KindType.getExtQualsPtrOrNull();
  assert(DesiredExtQuals);

  return getExtQualType(Type.getTypePtr(),
                        ExtQuals? ExtQuals->getQualifiers() : Qualifiers(),
                        DesiredExtQuals->getKindSelector(),
                        DesiredExtQuals->isDoublePrecisionKind(),
                        ExtQuals? ExtQuals->isStarLengthSelector() : false,
                        ExtQuals? ExtQuals->getLengthSelector() : 0);
}

// NB: this assumes that real and complex have have the same default kind.
QualType ASTContext::getComplexTypeElementType(QualType Type) {
  assert(Type->isComplexType());
  if(Type.getExtQualsPtrOrNull())
    return getQualTypeOtherKind(RealTy, Type);
  return RealTy;
}

QualType ASTContext::getComplexType(QualType ElementType) {
  assert(ElementType->isRealType());
  if(ElementType.getExtQualsPtrOrNull())
    return getQualTypeOtherKind(ComplexTy, ElementType);
  return ComplexTy;
}

/// getPointerType - Return the uniqued reference to the type for a pointer to
/// the specified type.
PointerType *ASTContext::getPointerType(const Type *Ty, unsigned NumDims) {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  PointerType::Profile(ID, Ty, NumDims);

  void *InsertPos = 0;
  if (PointerType *PT = PointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return PT;

  PointerType *New = new (*this) PointerType(Ty, NumDims);
  Types.push_back(New);
  PointerTypes.InsertNode(New, InsertPos);
  return New;
}

/// getArrayType - Return the unique reference to the type for an array of the
/// specified element type.
QualType ASTContext::getArrayType(QualType EltTy,
                                  ArrayRef<ArraySpec*> Dims) const {
  ArrayType *New = new (*this, TypeAlignment) ArrayType(Type::Array, EltTy,
                                                        QualType(), Dims);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getTypeDeclTypeSlow - Return the unique reference to the type for the
/// specified type declaration.
QualType ASTContext::getTypeDeclTypeSlow(const TypeDecl *Decl) const {
  assert(Decl && "Passed null for Decl param");
  assert(!Decl->TypeForDecl && "TypeForDecl present in slow case");

  const RecordDecl *Record = dyn_cast<RecordDecl>(Decl);
  if (!Record) {
    llvm_unreachable("TypeDecl without a type?");
    return QualType(Decl->TypeForDecl, 0);
  }

  return getRecordType(Record);
}

QualType ASTContext::getRecordType(const RecordDecl *Decl) const {
  return QualType();
#if 0
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);

  RecordType *newType = new (*this, TypeAlignment) RecordType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
#endif
}

} //namespace flang

// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef ABSTRACTMETAFUNCTION_H
#define ABSTRACTMETAFUNCTION_H

#include "abstractmetalang_enums.h"
#include "abstractmetalang_typedefs.h"
#include "typesystem_enums.h"
#include "modifications_typedefs.h"
#include "typesystem_typedefs.h"
#include "parser/codemodel_enums.h"

#include <QtCore/QMetaObject>
#include <QtCore/QScopedPointer>

#include <optional>

QT_FORWARD_DECLARE_CLASS(QDebug)
QT_FORWARD_DECLARE_CLASS(QRegularExpression)

class AbstractMetaFunctionPrivate;
class AbstractMetaType;
class FunctionTypeEntry;
class Documentation;
class SourceLocation;

struct ArgumentOwner;
struct ReferenceCount;

class AbstractMetaFunction
{
    Q_GADGET
public:
    enum FunctionType {
        ConstructorFunction,
        CopyConstructorFunction,
        MoveConstructorFunction,
        AssignmentOperatorFunction,
        MoveAssignmentOperatorFunction,
        DestructorFunction,
        NormalFunction,
        SignalFunction,
        EmptyFunction,
        SlotFunction,
        GetAttroFunction,
        SetAttroFunction,
        CallOperator,
        FirstOperator = CallOperator,
        ConversionOperator,
        DereferenceOperator, // Iterator's operator *
        ReferenceOperator, // operator &
        ArrowOperator,
        ArithmeticOperator,
        IncrementOperator,
        DecrementOperator,
        BitwiseOperator,
        LogicalOperator,
        ShiftOperator,
        SubscriptOperator,
        ComparisonOperator,
        LastOperator = ComparisonOperator
    };
    Q_ENUM(FunctionType)

    enum ComparisonOperatorType {
        OperatorEqual, OperatorNotEqual, OperatorLess, OperatorLessEqual,
        OperatorGreater, OperatorGreaterEqual
    };
    Q_ENUM(ComparisonOperatorType)

    enum CompareResultFlag {
        EqualName                   = 0x00000001,
        EqualArguments              = 0x00000002,
        EqualAttributes             = 0x00000004,
        EqualImplementor            = 0x00000008,
        EqualReturnType             = 0x00000010,
        EqualDefaultValueOverload   = 0x00000020,
        EqualModifiedName           = 0x00000040,

        NameLessThan                = 0x00001000,

        PrettySimilar               = EqualName | EqualArguments,
        Equal                       = 0x0000001f,
        NotEqual                    = 0x00001000
    };
    Q_DECLARE_FLAGS(CompareResult, CompareResultFlag)
    Q_FLAG(CompareResultFlag)

    enum Attribute {
        None                        = 0x00000000,

        Friendly                    = 0x00000001,

        Abstract                    = 0x00000002,
        Static                      = 0x00000004,
        ClassMethod                 = 0x00000008,

        FinalInTargetLang           = 0x00000010,

        GetterFunction              = 0x00000020,
        SetterFunction              = 0x00000040,

        PropertyReader              = 0x00000100,
        PropertyWriter              = 0x00000200,
        PropertyResetter            = 0x00000400,
        PropertyNotify              = 0x00000800,

        Invokable                   = 0x00001000,

        VirtualCppMethod            = 0x00010000,
        OverriddenCppMethod         = 0x00020000,
        FinalCppMethod              = 0x00040000,
        // Add by meta builder (implicit constructors, inherited methods, etc)
        AddedMethod                 = 0x001000000,
        Deprecated                  = 0x002000000 // Code annotation
    };
    Q_DECLARE_FLAGS(Attributes, Attribute)
    Q_FLAG(Attribute)

    Attributes attributes() const;
    void setAttributes(Attributes attributes);

    void operator+=(Attribute attribute);
    void operator-=(Attribute attribute);

    enum class Flag { // Internal flags not relevant for comparing functions
        // Binary operator whose leading/trailing argument was removed by metabuilder
        OperatorLeadingClassArgumentRemoved = 0x1,
        OperatorTrailingClassArgumentRemoved = 0x2,
        OperatorClassArgumentByValue = 0x4, // The removed class argument was passed by value
        InheritedFromTemplate = 0x8, // Inherited from a template in metabuilder
        HiddenFriend = 0x10,
        PrivateSignal = 0x20 // Private Qt signal (cannot emit from client code)
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    Flags flags() const;
    void setFlags(Flags f);

    bool isFinalInTargetLang() const;
    bool isAbstract() const;
    bool isClassMethod() const;
    bool isStatic() const;
    bool isInvokable() const;
    bool isPropertyReader() const;
    bool isPropertyWriter() const;
    bool isPropertyResetter() const;

    AbstractMetaFunction();
    explicit AbstractMetaFunction(const QString &name);
    explicit AbstractMetaFunction(const AddedFunctionPtr &addedFunc);
    ~AbstractMetaFunction();

    QString name() const;
    void setName(const QString &name);

    // Names under which the function will be registered to Python.
    QStringList definitionNames() const;

    QString originalName() const;

    void setOriginalName(const QString &name);

    Access access() const;
    void setAccess(Access a);
    void modifyAccess(Access a);

    bool isPrivate() const { return access() == Access::Private; }
    bool isProtected() const { return access() == Access::Protected; }
    bool isPublic() const { return access() == Access::Public; }
    bool wasPrivate() const;
    bool wasProtected() const;
    bool wasPublic() const;

    const Documentation &documentation() const;
    void setDocumentation(const Documentation& doc);

    bool isReverseOperator() const;
    void setReverseOperator(bool reverse);

    /// Returns true if this is a binary operator and the "self" operand is a
    /// pointer, e.g. class Foo {}; operator+(SomeEnum, Foo*);
    /// (not to be mixed up with DereferenceOperator).
    bool isPointerOperator() const;
    void setPointerOperator(bool value);


    /**
    *   Says if the function (a constructor) was declared as explicit in C++.
    *   \return true if the function was declared as explicit in C++
    */
    bool isExplicit() const;
    void setExplicit(bool isExplicit);

    bool returnsBool() const;
    bool isOperatorBool() const;
    static bool isConversionOperator(const QString& funcName);

    ExceptionSpecification exceptionSpecification() const;
    void setExceptionSpecification(ExceptionSpecification e);

    bool generateExceptionHandling() const;

    bool isConversionOperator() const;

    static bool isOperatorOverload(const QString& funcName);
    bool isOperatorOverload() const;

    bool isArithmeticOperator() const;
    bool isBitwiseOperator() const; // Includes shift operator
    bool isComparisonOperator() const;
    /// Returns whether this is a comparison accepting owner class
    /// (bool operator==(QByteArray,QByteArray) but not bool operator==(QByteArray,const char *)
    bool isSymmetricalComparisonOperator() const;
    bool isIncDecrementOperator() const;
    bool isLogicalOperator() const;
    bool isAssignmentOperator() const; // Assignment or move assignment
    bool isGetter() const;
    /// Returns whether it is a Qt-style isNull() method suitable for nb_bool
    bool isQtIsNullMethod() const;

    /**
     * Informs the arity of the operator or -1 if the function is not
     * an operator overload.
     * /return the arity of the operator or -1
     */
    int arityOfOperator() const;
    bool isUnaryOperator() const { return arityOfOperator() == 1; }
    bool isBinaryOperator() const { return arityOfOperator() == 2; }
    bool isInplaceOperator() const;

    bool isVirtual() const;
    bool allowThread() const;
    QString modifiedName() const;

    QString minimalSignature() const;
    /// List of signatures matched for modifications
    QStringList modificationSignatures() const;
    // Signature with replaced argument types and return type for overload
    // decisor comment.
    QString signatureComment() const;
    QString debugSignature() const; // including virtual/override/final, etc., for debugging only.

    bool isModifiedRemoved(AbstractMetaClassCPtr cls = {}) const;
    bool isModifiedFinal(AbstractMetaClassCPtr cls = {}) const;

    bool isVoid() const;

    const AbstractMetaType &type() const;
    void setType(const AbstractMetaType &type);

    // The class that has this function as a member.
    AbstractMetaClassCPtr ownerClass() const;
    void setOwnerClass(const AbstractMetaClassCPtr &cls);

    // Owner excluding invisible namespaces
    AbstractMetaClassCPtr targetLangOwner() const;

    // The first class in a hierarchy that declares the function
    AbstractMetaClassCPtr declaringClass() const;
    void setDeclaringClass(const AbstractMetaClassCPtr &cls);

    // The class that actually implements this function
    AbstractMetaClassCPtr implementingClass() const;
    void setImplementingClass(const AbstractMetaClassCPtr &cls);

    const AbstractMetaArgumentList &arguments() const;
    AbstractMetaArgumentList &arguments();
    void setArguments(const AbstractMetaArgumentList &arguments);
    void addArgument(const AbstractMetaArgument &argument);
    int actualMinimumArgumentCount() const;
    // Return the argument index accounting for the isModifiedRemoved arguments [0..n-1]
    int actualArgumentIndex(int index) const;

    bool isDeprecated() const;
    bool isDestructor() const { return functionType() == DestructorFunction; }
    bool isConstructor() const;
    bool isCopyConstructor() const { return functionType() == CopyConstructorFunction; }
    bool isDefaultConstructor() const;
    bool needsReturnType() const;
    bool isInGlobalScope() const;
    bool isSignal() const { return functionType() == SignalFunction; }
    bool isSlot() const { return functionType() == SlotFunction; }
    bool isEmptyFunction() const { return functionType() == EmptyFunction; }
    bool maybeAccessor() const;
    FunctionType functionType() const;
    void setFunctionType(FunctionType type);

    std::optional<ComparisonOperatorType> comparisonOperatorType() const;

    bool usesRValueReferences() const;
    bool generateBinding() const;
    // Returns whether the function is contained in the positive list of the
    // type entry if one is specified.
    bool isWhiteListed() const;

    QString signature() const;
    /// Return a signature qualified by class name, for error reporting.
    QString classQualifiedSignature() const;

    /// Signature with unresolved typedefs as seen by the code parser
    QString unresolvedSignature() const;
    void setUnresolvedSignature(const QString &);

    bool isConstant() const;
    void setConstant(bool constant);

    /// Returns true if the AbstractMetaFunction was added by the user via the type system description.
    bool isUserAdded() const;
    /// Returns true if the AbstractMetaFunction was declared by the user via
    /// the type system description.
    bool isUserDeclared() const;

    CompareResult compareTo(const AbstractMetaFunction *other) const;
    bool isConstOverloadOf(const AbstractMetaFunction *other) const;

    bool operator <(const AbstractMetaFunction &a) const;

    AbstractMetaFunction *copy() const;

    QString conversionRule(TypeSystem::Language language, int idx) const;
    bool hasConversionRule(TypeSystem::Language language, int idx) const;
    QList<ReferenceCount>
        referenceCounts(const AbstractMetaClassCPtr &cls, int idx = -2) const;
    ArgumentOwner argumentOwner(const AbstractMetaClassCPtr &cls, int idx) const;

    // Returns the ownership rules for the given argument (target lang).
    TypeSystem::Ownership
        argumentTargetOwnership(const AbstractMetaClassCPtr &cls, int idx) const;

    const QString &modifiedTypeName() const;
    bool isTypeModified() const { return !modifiedTypeName().isEmpty(); }
    bool generateOpaqueContainerReturn() const;

    bool isModifiedToArray(int argumentIndex) const;

    void applyTypeModifications();

    /// Return the (modified) type for the signature; modified-pyi-type, modified-type
    QString pyiTypeReplaced(int argumentIndex) const;

    bool argumentRemoved(int) const;
    /**
    *   Verifies if any modification to the function is an inject code.
    *   \return true if there is inject code modifications to the function.
    */
    bool hasInjectedCode() const;
    /**
    *   Returns a list of code snips for this function.
    *   The code snips can be filtered by position and language.
    *   \return list of code snips
    */
    CodeSnipList injectedCodeSnips(TypeSystem::CodeSnipPosition position = TypeSystem::CodeSnipPositionAny,
                                   TypeSystem::Language language = TypeSystem::All) const;
    bool injectedCodeContains(const QRegularExpression &pattern,
                              TypeSystem::CodeSnipPosition position = TypeSystem::CodeSnipPositionAny,
                              TypeSystem::Language language = TypeSystem::All) const;
    bool injectedCodeContains(QStringView pattern,
                              TypeSystem::CodeSnipPosition position = TypeSystem::CodeSnipPositionAny,
                              TypeSystem::Language language = TypeSystem::All) const;

    /**
    *   Verifies if any modification to the function alters/removes its
    *   arguments types or default values.
    *   \return true if there is some modification to function signature
    */
    bool hasSignatureModifications() const;

    const FunctionModificationList &modifications(AbstractMetaClassCPtr implementor = {}) const;
    void clearModificationsCache();

    const DocModificationList addedFunctionDocModifications() const;

    static FunctionModificationList findClassModifications(const AbstractMetaFunction *f,
                                                           AbstractMetaClassCPtr implementor);
    static FunctionModificationList findGlobalModifications(const AbstractMetaFunction *f);

    /**
     * Return the argument name if there is a modification the renamed value will be returned
     */
    QString argumentName(int index, bool create = true, AbstractMetaClassCPtr cl = {}) const;

    int propertySpecIndex() const;
    void setPropertySpecIndex(int i);

    FunctionTypeEntryPtr typeEntry() const;

    void setTypeEntry(const FunctionTypeEntryPtr &typeEntry);

    bool isCallOperator() const;

    static AbstractMetaFunctionCPtr
        find(const AbstractMetaFunctionCList &haystack, QStringView needle);

    bool matches(OperatorQueryOptions) const;

    // for the meta builder only
    void setAllowThreadModification(TypeSystem::AllowThread am);
    void setExceptionHandlingModification(TypeSystem::ExceptionHandling em);

    int overloadNumber() const;

    TypeSystem::SnakeCase snakeCase() const;

    // Query functions for generators
    /// Verifies if any of the function's code injections of the "native"
    /// type needs the type system variable "%PYSELF".
    /// \return true if the function's native code snippets use "%PYSELF"
    bool injectedCodeUsesPySelf() const;

    /// Verifies if any of the function's code injections of the "native" class makes a
    /// call to the C++ method. This is used by the generator to avoid writing calls to
    /// Python overrides of C++ virtual methods when the user custom code already does this.
    /// \param func the function to check
    /// \return true if the function's code snippets call the Python override for a C++ virtual method
    bool injectedCodeCallsPythonOverride() const;

    /// Verifies if any of the function's code injections attributes values to
    /// the return variable (%0 or %PYARG_0).
    /// \param language    the kind of code snip
    /// \return true if the function's code attributes values to "%0" or "%PYARG_0"
    bool injectedCodeHasReturnValueAttribution(TypeSystem::Language language =
                                                   TypeSystem::TargetLangCode) const;

    /// Verifies if any of the function's code injections uses the type system variable
    /// for function arguments of a given index.
    bool injectedCodeUsesArgument(int argumentIndex) const;

    bool isVisibilityModifiedToPrivate() const;

#ifndef QT_NO_DEBUG_STREAM
    void formatDebugBrief(QDebug &debug) const;
    void formatDebugVerbose(QDebug &debug) const;
#endif

    SourceLocation sourceLocation() const;
    void setSourceLocation(const SourceLocation &sourceLocation);

    static const char *pythonRichCompareOpCode(ComparisonOperatorType ct);
    static const char *cppComparisonOperator(ComparisonOperatorType ct);

private:
    template <class Predicate>
    bool traverseCodeSnips(Predicate predicate,
                           TypeSystem::CodeSnipPosition position = TypeSystem::CodeSnipPositionAny,
                           TypeSystem::Language language = TypeSystem::All) const;
    bool autoDetectAllowThread() const;

    QScopedPointer<AbstractMetaFunctionPrivate> d;
};

inline bool AbstractMetaFunction::isFinalInTargetLang() const
{
    return attributes().testFlag(FinalInTargetLang);
}

inline bool AbstractMetaFunction::isAbstract() const
{
    return attributes().testFlag(Abstract);
}

inline bool AbstractMetaFunction::isStatic() const
{
    return attributes().testFlag(Static);
}

inline bool AbstractMetaFunction::isClassMethod() const
{
    return attributes().testFlag(ClassMethod);
}

inline bool AbstractMetaFunction::isInvokable() const
{
    return attributes().testFlag(Invokable);
}

inline bool AbstractMetaFunction::isPropertyReader() const
{
    return attributes().testFlag(PropertyReader);
}

inline bool AbstractMetaFunction::isPropertyWriter() const
{
    return attributes().testFlag(PropertyWriter);
}

inline bool AbstractMetaFunction::isPropertyResetter() const
{
    return attributes().testFlag(PropertyResetter);
}

Q_DECLARE_OPERATORS_FOR_FLAGS(AbstractMetaFunction::CompareResult)

Q_DECLARE_OPERATORS_FOR_FLAGS(AbstractMetaFunction::Attributes);

Q_DECLARE_OPERATORS_FOR_FLAGS(AbstractMetaFunction::Flags);

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug debug, const AbstractMetaFunction *af);
#endif

#endif // ABSTRACTMETAFUNCTION_H

// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef ABSTRACTMETATYPE_H
#define ABSTRACTMETATYPE_H

#include "abstractmetalang_enums.h"
#include "abstractmetalang_typedefs.h"
#include "parser/codemodel_enums.h"
#include "typedatabase_typedefs.h"

#include <QtCore/qobjectdefs.h>
#include <QtCore/QSharedDataPointer>
#include <QtCore/QList>
#include <QtCore/QSet>

#include <optional>

QT_FORWARD_DECLARE_CLASS(QDebug)

class AbstractMetaTypeData;
class TypeEntry;

class AbstractMetaType
{
    Q_GADGET
public:
    using Indirections = QList<Indirection>;

    enum TypeUsagePattern {
        PrimitivePattern,
        FlagsPattern,
        EnumPattern,
        ValuePattern,
        ObjectPattern,
        ValuePointerPattern,
        NativePointerPattern,
        NativePointerAsArrayPattern, // "int*" as "int[]"
        ContainerPattern,
        SmartPointerPattern,
        VarargsPattern,
        ArrayPattern,
        VoidPattern,            // Plain "void", no "void *" or similar.
        TemplateArgument,       // 'T' in std::array<T,2>
        NonTypeTemplateArgument // '2' in in std::array<T,2>
    };
    Q_ENUM(TypeUsagePattern)

    AbstractMetaType();
    explicit AbstractMetaType(const TypeEntryCPtr &t);
    AbstractMetaType(const AbstractMetaType &);
    AbstractMetaType &operator=(const AbstractMetaType &);
    AbstractMetaType(AbstractMetaType &&);
    AbstractMetaType &operator=(AbstractMetaType &&);
    ~AbstractMetaType();

    QString package() const;
    QString name() const;
    QString fullName() const;

    void setTypeUsagePattern(TypeUsagePattern pattern);
    TypeUsagePattern typeUsagePattern() const;

    // true when use pattern is container
    bool hasInstantiations() const;

    const AbstractMetaTypeList &instantiations() const;
    void addInstantiation(const AbstractMetaType &inst);
    void setInstantiations(const AbstractMetaTypeList  &insts);
    QStringList instantiationCppSignatures() const;

    QString minimalSignature() const { return formatSignature(true); }

    // returns true if the typs is used as a non complex primitive, no & or *'s
    bool isPrimitive() const { return typeUsagePattern() == PrimitivePattern; }

    bool isCppPrimitive() const;

    // returns true if the type is used as an enum
    bool isEnum() const { return typeUsagePattern() == EnumPattern; }

    // returns true if the type is used as an object, e.g. Xxx *
    bool isObject() const { return typeUsagePattern() == ObjectPattern; }

    // returns true if the type is indicated an object by the TypeEntry
    bool isObjectType() const;

    // returns true if the type is used as an array, e.g. Xxx[42]
    bool isArray() const { return typeUsagePattern() == ArrayPattern; }

    // returns true if the type is used as a value type (X or const X &)
    bool isValue() const { return typeUsagePattern() == ValuePattern; }

    bool isValuePointer() const { return typeUsagePattern() == ValuePointerPattern; }

    // returns true for more complex types...
    bool isNativePointer() const { return typeUsagePattern() == NativePointerPattern; }

    // return true if the type was originally a varargs
    bool isVarargs() const { return typeUsagePattern() == VarargsPattern; }

    // returns true if the type was used as a container
    bool isContainer() const { return typeUsagePattern() == ContainerPattern; }

    // returns true if the type was used as a smart pointer
    bool isSmartPointer() const { return typeUsagePattern() == SmartPointerPattern; }
    bool isUniquePointer() const;

    // returns true if the type was used as a flag
    bool isFlags() const { return typeUsagePattern() == FlagsPattern; }

    bool isVoid() const { return typeUsagePattern() == VoidPattern; }

    bool isConstant() const;
    void setConstant(bool constant);

    bool isVolatile() const;
    void setVolatile(bool v);

    bool passByConstRef() const;
    bool passByValue() const;

    ReferenceType referenceType() const;
    void setReferenceType(ReferenceType ref);

    int actualIndirections() const;

    Indirections indirectionsV() const;
    void setIndirectionsV(const Indirections &i);
    void clearIndirections();

    // "Legacy"?
    int indirections() const;
    void setIndirections(int indirections);
    void addIndirection(Indirection i = Indirection::Pointer);

    void setArrayElementCount(int n);
    int arrayElementCount() const;

    const AbstractMetaType *arrayElementType() const;
    void setArrayElementType(const AbstractMetaType &t);

    AbstractMetaTypeList nestedArrayTypes() const;

    /// Strip const/indirections/reference from the type
    AbstractMetaType plainType() const;

    QString cppSignature() const;

    QString pythonSignature() const;

    bool applyArrayModification(QString *errorMessage);

    TypeEntryCPtr typeEntry() const;
    void setTypeEntry(const TypeEntryCPtr &type);

    void setOriginalTypeDescription(const QString &otd);
    QString originalTypeDescription() const;

    void setOriginalTemplateType(const AbstractMetaType &type);
    const AbstractMetaType *originalTemplateType() const;

    AbstractMetaType getSmartPointerInnerType() const;

    QString getSmartPointerInnerTypeName() const;

    /// Decides and sets the proper usage patter for the current meta type.
    void decideUsagePattern();

    bool hasTemplateChildren() const;

    bool equals(const AbstractMetaType &rhs) const;
    /// Is equivalent from the POV of argument passing (differ by const ref)
    bool isEquivalent(const AbstractMetaType &rhs) const;

    // View on: Type to use for function argument conversion, fex
    // std::string_view -> std::string for foo(std::string_view);
    // cf TypeEntry::viewOn()
    const AbstractMetaType *viewOn() const;
    void setViewOn(const AbstractMetaType &v);

    static AbstractMetaType createVoid();

    /// Builds an AbstractMetaType object from a QString.
    /// Returns nullopt if no type could be built from the string.
    /// \param typeSignature The string describing the type to be built.
    /// \return A new AbstractMetaType object or nullopt in case of failure.
    static std::optional<AbstractMetaType>
        fromString(QString typeSignature, QString *errorMessage = nullptr);
    /// Creates an AbstractMetaType object from a TypeEntry.
    static AbstractMetaType fromTypeEntry(const TypeEntryCPtr &typeEntry);
    /// Creates an AbstractMetaType object from an AbstractMetaClass.
    static AbstractMetaType fromAbstractMetaClass(const AbstractMetaClassCPtr &metaClass);

    static void dereference(QString *type); // "foo" -> "(*foo)"
    /// Apply the result of shouldDereferenceArgument()
    static QString dereferencePrefix(qsizetype n); // Return the prefix **/& as as required
    static void applyDereference(QString *type, qsizetype n);
    static bool stripDereference(QString *type); // "(*foo)" -> "foo"

    // Query functions for generators
    /// Check if type is a pointer.
    bool isPointer() const;
    /// Helper for field setters: Check for "const QWidget *" (settable field),
    /// but not "int *const" (read-only field).
    bool isPointerToConst() const;
    /// Returns true if the type is a C string (const char *).
    bool isCString() const;
    /// Returns true if the type is a void pointer.
    bool isVoidPointer() const;
    /// Returns true if the type is a primitive but not a C++ primitive.
    bool isUserPrimitive() const;
    /// Returns true if it is an Object Type used as a value.
    bool isObjectTypeUsedAsValueType() const;
    /// Returns true if the type passed has a Python wrapper for it.
    /// Although namespace has a Python wrapper, it's not considered a type.
    bool isWrapperType() const;
    /// Checks if the type is an Object/QObject or pointer to Value Type.
    /// In other words, tells if the type is "T*" and T has a Python wrapper.
    bool isPointerToWrapperType() const;
    /// Wrapper type passed by reference
    bool isWrapperPassedByReference() const;
    /// Returns true if the type is a C++ integral primitive,
    /// i.e. bool, char, int, long, and their unsigned counterparts.
    bool isCppIntegralPrimitive() const;
    /// Returns true if the type is an extended C++ primitive, a void*,
    /// a const char*, or a std::string (cf isCppPrimitive()).
    bool isExtendedCppPrimitive() const;
    /// Returns whether the underlying type is a value type with copy constructor only
    bool isValueTypeWithCopyConstructorOnly() const;
    /// Returns whether the type (function argument) is a value type with
    /// copy constructor only is passed as value or const-ref and thus
    /// no default value can be constructed.
    bool valueTypeWithCopyConstructorOnlyPassed() const;
    /// Returns whether to generate an opaque container for the type
    bool generateOpaqueContainer() const;
    /// Returns whether to generate an opaque container for a getter
    bool generateOpaqueContainerForGetter(const QString &modifiedType) const;

    /// Types for which libshiboken has built-in primitive converters
    static const QSet<QString> &cppFloatTypes();
    static const QSet<QString> &cppSignedCharTypes();
    static const QSet<QString> &cppUnsignedCharTypes();
    static const QSet<QString> &cppCharTypes();
    static const QSet<QString> &cppSignedIntTypes();
    static const QSet<QString> &cppUnsignedIntTypes();
    static const QSet<QString> &cppIntegralTypes();
    static const QSet<QString> &cppPrimitiveTypes();

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &debug) const;
#endif

private:
    friend class AbstractMetaTypeData;
    QSharedDataPointer<AbstractMetaTypeData> d;

    TypeUsagePattern determineUsagePattern() const;
    QString formatSignature(bool minimal) const;
    QString formatPythonSignature() const;
};

inline bool operator==(const AbstractMetaType &t1, const AbstractMetaType &t2)
{ return t1.equals(t2); }
inline bool operator!=(const AbstractMetaType &t1, const AbstractMetaType &t2)
{ return !t1.equals(t2); }

inline size_t qHash(const AbstractMetaType &t, size_t seed)
{ return qHash(t.typeEntry().get(), seed); }

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const AbstractMetaType &at);
QDebug operator<<(QDebug d, const AbstractMetaType *at);
#endif

#endif // ABSTRACTMETALANG_H

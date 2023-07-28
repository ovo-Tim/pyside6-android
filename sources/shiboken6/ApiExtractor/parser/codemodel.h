// Copyright (C) 2016 The Qt Company Ltd.
// Copyright (C) 2002-2005 Roberto Raggi <roberto@kdevelop.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


#ifndef CODEMODEL_H
#define CODEMODEL_H

#include "codemodel_fwd.h"
#include "codemodel_enums.h"
#include "enumvalue.h"
#include "typeinfo.h"

#include <QtCore/QHash>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>
#include <QtCore/QWeakPointer>

#include <optional>

QT_FORWARD_DECLARE_CLASS(QDebug)

#define DECLARE_MODEL_NODE(k) \
    enum { __node_kind = Kind_##k };

class SourceLocation;

class CodeModel
{
    Q_GADGET
public:
    Q_DISABLE_COPY(CodeModel)

    enum FunctionType {
        Normal,
        Constructor,
        CopyConstructor,
        MoveConstructor,
        Destructor,
        Signal,
        Slot,
        AssignmentOperator,
        CallOperator,
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
        ComparisonOperator
    };
    Q_ENUM(FunctionType)

    enum ClassType {
        Class,
        Struct,
        Union
    };
    Q_ENUM(ClassType)

public:
    CodeModel();
    virtual ~CodeModel();

    FileList files() const { return m_files; }
    NamespaceModelItem globalNamespace() const;

    void addFile(const FileModelItem &item);
    FileModelItem findFile(const QString &name) const;

    static CodeModelItem findItem(const QStringList &qualifiedName,
                                  const ScopeModelItem &scope);

private:
    FileList m_files;
    NamespaceModelItem m_globalNamespace;
};

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, Access a);
QDebug operator<<(QDebug d, const CodeModel *m);
#endif

class _CodeModelItem
{
    Q_DISABLE_COPY(_CodeModelItem)
public:
    enum Kind {
        /* These are bit-flags resembling inheritance */
        Kind_Scope = 0x1,
        Kind_Namespace = 0x2 | Kind_Scope,
        Kind_Member = 0x4,
        Kind_Function = 0x8 | Kind_Member,
        KindMask = 0xf,

        /* These are for classes that are not inherited from */
        FirstKind = 0x8,
        Kind_Argument = 1 << FirstKind,
        Kind_Class = 2 << FirstKind | Kind_Scope,
        Kind_Enum = 3 << FirstKind,
        Kind_Enumerator = 4 << FirstKind,
        Kind_File = 5 << FirstKind | Kind_Namespace,
        Kind_TemplateParameter = 7 << FirstKind,
        Kind_TypeDef = 8 << FirstKind,
        Kind_TemplateTypeAlias = 9 << FirstKind,
        Kind_Variable = 10 << FirstKind | Kind_Member
    };

public:
    virtual ~_CodeModelItem();

    int kind() const;

    QStringList qualifiedName() const;

    QString name() const;
    void setName(const QString &name);

    QStringList scope() const;
    void setScope(const QStringList &scope);

    QString fileName() const;
    void setFileName(const QString &fileName);

    FileModelItem file() const;

    void getStartPosition(int *line, int *column);
    int startLine() const { return m_startLine; }
    void setStartPosition(int line, int column);

    void getEndPosition(int *line, int *column);
    void setEndPosition(int line, int column);

    SourceLocation sourceLocation() const;

    inline CodeModel *model() const { return m_model; }

    const _ScopeModelItem *enclosingScope() const;
    void setEnclosingScope(const _ScopeModelItem *s);

#ifndef QT_NO_DEBUG_STREAM
    static void formatKind(QDebug &d, int k);
    virtual void formatDebug(QDebug &d) const;
#endif

protected:
    explicit _CodeModelItem(CodeModel *model, int kind);
    explicit _CodeModelItem(CodeModel *model, const QString &name, int kind);

private:
    CodeModel *m_model;
    const _ScopeModelItem *m_enclosingScope = nullptr;
    int m_kind;
    int m_startLine;
    int m_startColumn;
    int m_endLine;
    int m_endColumn;
    QString m_name;
    QString m_fileName;
    QStringList m_scope;
};

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const _CodeModelItem *t);
#endif

class _ScopeModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(Scope)

    ~_ScopeModelItem();

    ClassList classes() const { return m_classes; }
    const EnumList &enums() const { return m_enums; }
    inline const FunctionList &functions() const { return m_functions; }
    TypeDefList typeDefs() const { return m_typeDefs; }
    TemplateTypeAliasList templateTypeAliases() const { return m_templateTypeAliases; }
    VariableList variables() const { return m_variables; }

    void addClass(const ClassModelItem &item);
    void addEnum(const EnumModelItem &item);
    void addFunction(const FunctionModelItem &item);
    void addTypeDef(const TypeDefModelItem &item);
    void addTemplateTypeAlias(const TemplateTypeAliasModelItem &item);
    void addVariable(const VariableModelItem &item);

    ClassModelItem findClass(const QString &name) const;
    EnumModelItem findEnum(const QString &name) const;

    struct FindEnumByValueReturn
    {
        operator bool() const { return bool(item); }

        EnumModelItem item;
        QString qualifiedName;
    };
    FindEnumByValueReturn findEnumByValue(QStringView value) const;

    FunctionList findFunctions(const QString &name) const;
    TypeDefModelItem findTypeDef(const QString &name) const;
    TemplateTypeAliasModelItem findTemplateTypeAlias(const QString &name) const;
    VariableModelItem findVariable(const QString &name) const;

    void addEnumsDeclaration(const QString &enumsDeclaration);
    QStringList enumsDeclarations() const { return m_enumsDeclarations; }

    FunctionModelItem declaredFunction(const FunctionModelItem &item);

    bool isEmpty() const;
    void purgeClassDeclarations();

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

protected:
    explicit _ScopeModelItem(CodeModel *model, int kind = __node_kind);
    explicit _ScopeModelItem(CodeModel *model, const QString &name,
                             int kind = __node_kind);

    void appendScope(const _ScopeModelItem &other);

#ifndef QT_NO_DEBUG_STREAM
    void formatScopeItemsDebug(QDebug &d) const;
#endif

private:
    qsizetype indexOfEnum(const QString &name) const;

    FindEnumByValueReturn findEnumByValueHelper(QStringView fullValue,
                                                QStringView value) const;
    static FindEnumByValueReturn
        findEnumByValueRecursion(const _ScopeModelItem *scope,
                                 QStringView fullValue, QStringView value,
                                 bool searchSiblingNamespaces = true);

    ClassList m_classes;
    EnumList m_enums;
    TypeDefList m_typeDefs;
    TemplateTypeAliasList m_templateTypeAliases;
    VariableList m_variables;
    FunctionList m_functions;

private:
    QStringList m_enumsDeclarations;
};

class _ClassModelItem: public _ScopeModelItem
{
public:
    DECLARE_MODEL_NODE(Class)

    struct BaseClass
    {
        QString name;
        ClassModelItem klass; // Might be null in case of templates
        Access accessPolicy = Access::Public;
    };

    struct UsingMember // Introducing a base class member via 'using' directive
    {
        QString className;
        QString memberName;
        Access access = Access::Public;
    };

    explicit _ClassModelItem(CodeModel *model, int kind = __node_kind);
    explicit _ClassModelItem(CodeModel *model, const QString &name,
                             int kind = __node_kind);
    ~_ClassModelItem();

    const QList<BaseClass> &baseClasses() const { return m_baseClasses; }

    const QList<UsingMember> &usingMembers() const;
    void addUsingMember(const QString &className, const QString &memberName,
                        Access accessPolicy);

    void addBaseClass(const BaseClass &b) { m_baseClasses.append(b); }

    TemplateParameterList templateParameters() const;
    void setTemplateParameters(const TemplateParameterList &templateParameters);

    bool extendsClass(const QString &name) const;

    void setClassType(CodeModel::ClassType type);
    CodeModel::ClassType classType() const;

    void addPropertyDeclaration(const QString &propertyDeclaration);
    QStringList propertyDeclarations() const { return m_propertyDeclarations; }

    bool isFinal() const { return m_final; }
    void setFinal(bool f) { m_final = f; }

    bool isEmpty() const;
    bool isTemplate() const;

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    QList<BaseClass> m_baseClasses;
    QList<UsingMember> m_usingMembers;
    TemplateParameterList m_templateParameters;
    CodeModel::ClassType m_classType = CodeModel::Class;

    QStringList m_propertyDeclarations;
    bool m_final = false;
};

class _NamespaceModelItem: public _ScopeModelItem
{
public:
    DECLARE_MODEL_NODE(Namespace)

    explicit _NamespaceModelItem(CodeModel *model, int kind = __node_kind);
    explicit _NamespaceModelItem(CodeModel *model, const QString &name,
                                 int kind = __node_kind);
    ~_NamespaceModelItem();

    const NamespaceList &namespaces() const { return m_namespaces; }

     NamespaceType type() const { return m_type; }
     void setType(NamespaceType t) { m_type = t; }

    void addNamespace(NamespaceModelItem item);

    NamespaceModelItem findNamespace(const QString &name) const;

    void appendNamespace(const _NamespaceModelItem &other);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    NamespaceList m_namespaces;
    NamespaceType m_type = NamespaceType::Default;
};

class _FileModelItem: public _NamespaceModelItem
{
public:
    DECLARE_MODEL_NODE(File)

    using _NamespaceModelItem::_NamespaceModelItem;

    ~_FileModelItem();
};

class _ArgumentModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(Argument)

    explicit _ArgumentModelItem(CodeModel *model, int kind = __node_kind);
    explicit _ArgumentModelItem(CodeModel *model, const QString &name,
                                int kind = __node_kind);
    ~_ArgumentModelItem();

    TypeInfo type() const;
    void setType(const TypeInfo &type);

    bool defaultValue() const;
    void setDefaultValue(bool defaultValue);

    QString defaultValueExpression() const { return m_defaultValueExpression; }
    void setDefaultValueExpression(const QString &expr) { m_defaultValueExpression = expr; }

    // Argument type has scope resolution "::ArgumentType"
    bool scopeResolution() const;
    void setScopeResolution(bool v);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    TypeInfo m_type;
    QString m_defaultValueExpression;
    bool m_defaultValue = false;
    bool m_scopeResolution = false;
};

class _MemberModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(Member)

    explicit _MemberModelItem(CodeModel *model, int kind = __node_kind);
    explicit _MemberModelItem(CodeModel *model, const QString &name,
                              int kind = __node_kind);
    ~_MemberModelItem();

    bool isConstant() const;
    void setConstant(bool isConstant);

    bool isVolatile() const;
    void setVolatile(bool isVolatile);

    bool isStatic() const;
    void setStatic(bool isStatic);

    bool isAuto() const;
    void setAuto(bool isAuto);

    bool isFriend() const;
    void setFriend(bool isFriend);

    bool isRegister() const;
    void setRegister(bool isRegister);

    bool isExtern() const;
    void setExtern(bool isExtern);

    bool isMutable() const;
    void setMutable(bool isMutable);

    Access accessPolicy() const;
    void setAccessPolicy(Access accessPolicy);

    TemplateParameterList templateParameters() const { return m_templateParameters; }
    void setTemplateParameters(const TemplateParameterList &templateParameters) { m_templateParameters = templateParameters; }

    TypeInfo type() const;
    void setType(const TypeInfo &type);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    TemplateParameterList m_templateParameters;
    TypeInfo m_type;
    Access m_accessPolicy = Access::Public;
    union {
        struct {
            uint m_isConstant: 1;
            uint m_isVolatile: 1;
            uint m_isStatic: 1;
            uint m_isAuto: 1;
            uint m_isFriend: 1;
            uint m_isRegister: 1;
            uint m_isExtern: 1;
            uint m_isMutable: 1;
        };
        uint m_flags;
    };

};

class _FunctionModelItem: public _MemberModelItem
{
public:
    DECLARE_MODEL_NODE(Function)

    explicit _FunctionModelItem(CodeModel *model, int kind = __node_kind);
    explicit _FunctionModelItem(CodeModel *model, const QString &name,
                                int kind = __node_kind);
    ~_FunctionModelItem();

    ArgumentList arguments() const;

    void addArgument(const ArgumentModelItem& item);

    CodeModel::FunctionType functionType() const;
    void setFunctionType(CodeModel::FunctionType functionType);

    static std::optional<CodeModel::FunctionType> functionTypeFromName(QStringView name);

    bool isDeleted() const;
    void setDeleted(bool d);

    bool isDeprecated() const;
    void setDeprecated(bool d);

    bool isVirtual() const;
    void setVirtual(bool isVirtual);

    bool isOverride() const;
    void setOverride(bool o);

    bool isFinal() const;
    void setFinal(bool f);

    bool isInline() const;
    void setInline(bool isInline);

    bool isExplicit() const;
    void setExplicit(bool isExplicit);

    bool isHiddenFriend() const;
    void setHiddenFriend(bool f);

    bool isInvokable() const; // Qt
    void setInvokable(bool isInvokable); // Qt

    bool isAbstract() const;
    void setAbstract(bool isAbstract);

    bool isVariadics() const;
    void setVariadics(bool isVariadics);

    bool scopeResolution() const; // Return type has scope resolution "::ReturnType"
    void setScopeResolution(bool v);

    bool isDefaultConstructor() const;
    bool isSpaceshipOperator() const;

    bool isSimilar(const FunctionModelItem &other) const;

    bool isNoExcept() const;

    bool isOperator() const;

    ExceptionSpecification exceptionSpecification() const;
    void setExceptionSpecification(ExceptionSpecification e);

    QString typeSystemSignature() const; // For dumping out type system files

    // Private, for usage by the clang builder.
    void _determineType();

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    CodeModel::FunctionType _determineTypeHelper() const;

    ArgumentList m_arguments;
    CodeModel::FunctionType m_functionType = CodeModel::Normal;
    union {
        struct {
            uint m_isDeleted: 1;
            uint m_isVirtual: 1;
            uint m_isOverride: 1;
            uint m_isFinal: 1;
            uint m_isDeprecated: 1;
            uint m_isInline: 1;
            uint m_isAbstract: 1;
            uint m_isExplicit: 1;
            uint m_isVariadics: 1;
            uint m_isHiddenFriend: 1;
            uint m_isInvokable : 1; // Qt
            uint m_scopeResolution: 1;
        };
        uint m_flags;
    };
    ExceptionSpecification m_exceptionSpecification = ExceptionSpecification::Unknown;
};

class _VariableModelItem: public _MemberModelItem
{
public:
    DECLARE_MODEL_NODE(Variable)

    using _MemberModelItem::_MemberModelItem;
};

class _TypeDefModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(TypeDef)

    explicit _TypeDefModelItem(CodeModel *model, int kind = __node_kind);
    explicit _TypeDefModelItem(CodeModel *model, const QString &name,
                               int kind = __node_kind);

    TypeInfo type() const;
    void setType(const TypeInfo &type);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    TypeInfo m_type;
};

class _TemplateTypeAliasModelItem : public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(TemplateTypeAlias)

    explicit _TemplateTypeAliasModelItem(CodeModel *model, int kind = __node_kind);
    explicit _TemplateTypeAliasModelItem(CodeModel *model, const QString &name,
                                         int kind = __node_kind);

    TemplateParameterList templateParameters() const;
    void addTemplateParameter(const TemplateParameterModelItem &templateParameter);

    TypeInfo type() const;
    void setType(const TypeInfo &type);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    TemplateParameterList m_templateParameters;
    TypeInfo m_type;
};

class _EnumModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(Enum)

    explicit _EnumModelItem(CodeModel *model, const QString &name, int kind = __node_kind);
    explicit _EnumModelItem(CodeModel *model, int kind = __node_kind);
    ~_EnumModelItem();

    Access accessPolicy() const;
    void setAccessPolicy(Access accessPolicy);

    bool hasValues() const { return !m_enumerators.isEmpty(); }
    EnumeratorList enumerators() const;
    void addEnumerator(const EnumeratorModelItem &item);

    EnumKind enumKind() const { return m_enumKind; }
    void setEnumKind(EnumKind kind) { m_enumKind = kind; }

    qsizetype indexOfValue(QStringView value) const;

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

    bool isDeprecated() const;
    void setDeprecated(bool d);

    bool isSigned() const;
    void setSigned(bool s);

private:
    Access m_accessPolicy = Access::Public;
    EnumeratorList m_enumerators;
    EnumKind m_enumKind = CEnum;
    bool m_deprecated = false;
    bool m_signed = true;
};

class _EnumeratorModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(Enumerator)

    explicit _EnumeratorModelItem(CodeModel *model, int kind = __node_kind);
    explicit _EnumeratorModelItem(CodeModel *model, const QString &name,
                                  int kind = __node_kind);
    ~_EnumeratorModelItem();

    QString stringValue() const;
    void setStringValue(const QString &stringValue);

    EnumValue value() const { return m_value; }
    void setValue(EnumValue v) { m_value = v; }

    bool isDeprecated() const;
    void setDeprecated(bool d);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    QString m_stringValue;
    EnumValue m_value;
    bool m_deprecated = false;
};

class _TemplateParameterModelItem: public _CodeModelItem
{
public:
    DECLARE_MODEL_NODE(TemplateParameter)

    explicit _TemplateParameterModelItem(CodeModel *model, int kind = __node_kind);
    explicit _TemplateParameterModelItem(CodeModel *model, const QString &name,
                                         int kind = __node_kind);
    ~_TemplateParameterModelItem();

    TypeInfo type() const;
    void setType(const TypeInfo &type);

    bool defaultValue() const;
    void setDefaultValue(bool defaultValue);

#ifndef QT_NO_DEBUG_STREAM
    void formatDebug(QDebug &d) const override;
#endif

private:
    TypeInfo m_type;
    bool m_defaultValue = false;
};

#endif // CODEMODEL_H

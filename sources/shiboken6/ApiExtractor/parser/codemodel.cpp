// Copyright (C) 2016 The Qt Company Ltd.
// Copyright (C) 2002-2005 Roberto Raggi <roberto@kdevelop.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


#include "codemodel.h"

#include <sourcelocation.h>
#include <debughelpers_p.h>

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>

#include <algorithm>
#include <functional>
#include <iostream>

using namespace Qt::StringLiterals;

// Predicate to find an item by name in a list of std::shared_ptr<Item>
template <class T> class ModelItemNamePredicate
{
public:
    explicit ModelItemNamePredicate(const QString &name) : m_name(name) {}
    bool operator()(const std::shared_ptr<T> &item) const { return item->name() == m_name; }

private:
    const QString m_name;
};

template <class T>
static std::shared_ptr<T> findModelItem(const QList<std::shared_ptr<T> > &list, const QString &name)
{
    const auto it = std::find_if(list.cbegin(), list.cend(), ModelItemNamePredicate<T>(name));
    return it != list.cend() ? *it : std::shared_ptr<T>();
}

// ---------------------------------------------------------------------------

CodeModel::CodeModel() : m_globalNamespace(new _NamespaceModelItem(this))
{
}

CodeModel::~CodeModel() = default;

NamespaceModelItem CodeModel::globalNamespace() const
{
    return m_globalNamespace;
}

void CodeModel::addFile(const FileModelItem &item)
{
    m_files.append(item);
}

FileModelItem CodeModel::findFile(const QString &name) const
{
    return findModelItem(m_files, name);
}

static CodeModelItem findRecursion(const ScopeModelItem &scope,
                                   const QStringList &qualifiedName, int segment = 0)
{
    const QString &nameSegment = qualifiedName.at(segment);
    if (segment == qualifiedName.size() - 1) { // Leaf item
        if (ClassModelItem cs = scope->findClass(nameSegment))
            return cs;
        if (EnumModelItem es = scope->findEnum(nameSegment))
            return es;
        if (TypeDefModelItem tp = scope->findTypeDef(nameSegment))
            return tp;
        if (TemplateTypeAliasModelItem tta = scope->findTemplateTypeAlias(nameSegment))
            return tta;
        return CodeModelItem();
    }
    if (auto nestedClass = scope->findClass(nameSegment))
        return findRecursion(nestedClass, qualifiedName, segment + 1);
    if (auto namespaceItem = std::dynamic_pointer_cast<_NamespaceModelItem>(scope)) {
        for (const auto &nestedNamespace : namespaceItem->namespaces()) {
            if (nestedNamespace->name() == nameSegment) {
                if (auto item = findRecursion(nestedNamespace, qualifiedName, segment + 1))
                    return item;
            }
        }
    }
    return CodeModelItem();
}

CodeModelItem CodeModel::findItem(const QStringList &qualifiedName, const ScopeModelItem &scope)
{
    return findRecursion(scope, qualifiedName);
}

#ifndef QT_NO_DEBUG_STREAM

QDebug operator<<(QDebug d, Access a)
{
    QDebugStateSaver s(d);
    d.noquote();
    d.nospace();
    switch (a) {
    case Access::Public:
        d << "public";
        break;
    case Access::Protected:
        d << "protected";
        break;
    case Access::Private:
        d << "private";
        break;
    }
    return d;
}

QDebug operator<<(QDebug d, const CodeModel *m)
{
    QDebugStateSaver s(d);
    d.noquote();
    d.nospace();
    d << "CodeModel(";
    if (m) {
        const NamespaceModelItem globalNamespaceP = m->globalNamespace();
        if (globalNamespaceP)
            globalNamespaceP->formatDebug(d);
    } else {
        d << '0';
    }
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_CodeModelItem::_CodeModelItem(CodeModel *model, int kind)
        : m_model(model),
        m_kind(kind),
        m_startLine(0),
        m_startColumn(0),
        m_endLine(0),
        m_endColumn(0)
{
}

_CodeModelItem::_CodeModelItem(CodeModel *model, const QString &name, int kind)
    : m_model(model),
    m_kind(kind),
    m_startLine(0),
    m_startColumn(0),
    m_endLine(0),
    m_endColumn(0),
    m_name(name)
{
}

_CodeModelItem::~_CodeModelItem() = default;

int _CodeModelItem::kind() const
{
    return m_kind;
}

QStringList _CodeModelItem::qualifiedName() const
{
    QStringList q = scope();

    if (!name().isEmpty())
        q += name();

    return q;
}

QString _CodeModelItem::name() const
{
    return m_name;
}

void _CodeModelItem::setName(const QString &name)
{
    m_name = name;
}

QStringList _CodeModelItem::scope() const
{
    return m_scope;
}

void _CodeModelItem::setScope(const QStringList &scope)
{
    m_scope = scope;
}

QString _CodeModelItem::fileName() const
{
    return m_fileName;
}

void _CodeModelItem::setFileName(const QString &fileName)
{
    m_fileName = fileName;
}

FileModelItem _CodeModelItem::file() const
{
    return model()->findFile(fileName());
}

void _CodeModelItem::getStartPosition(int *line, int *column)
{
    *line = m_startLine;
    *column = m_startColumn;
}

void _CodeModelItem::setStartPosition(int line, int column)
{
    m_startLine = line;
    m_startColumn = column;
}

void _CodeModelItem::getEndPosition(int *line, int *column)
{
    *line = m_endLine;
    *column = m_endColumn;
}

void _CodeModelItem::setEndPosition(int line, int column)
{
    m_endLine = line;
    m_endColumn = column;
}

SourceLocation _CodeModelItem::sourceLocation() const
{
    return SourceLocation(m_fileName, m_startLine);
}

const _ScopeModelItem *_CodeModelItem::enclosingScope() const
{
    return m_enclosingScope;
}

void _CodeModelItem::setEnclosingScope(const _ScopeModelItem *s)
{
    m_enclosingScope = s;
}

_ScopeModelItem::_ScopeModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

_ScopeModelItem::_ScopeModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

#ifndef QT_NO_DEBUG_STREAM
void _CodeModelItem::formatKind(QDebug &d, int k)
{
    switch (k) {
    case Kind_Argument:
        d << "ArgumentModelItem";
        break;
    case Kind_Class:
        d << "ClassModelItem";
        break;
    case Kind_Enum:
        d << "EnumModelItem";
        break;
    case Kind_Enumerator:
        d << "EnumeratorModelItem";
        break;
    case Kind_File:
        d << "FileModelItem";
        break;
    case Kind_Function:
        d << "FunctionModelItem";
        break;
    case Kind_Member:
        d << "MemberModelItem";
        break;
    case Kind_Namespace:
        d << "NamespaceModelItem";
        break;
    case Kind_Variable:
        d << "VariableModelItem";
        break;
    case Kind_Scope:
        d << "ScopeModelItem";
        break;
    case Kind_TemplateParameter:
        d << "TemplateParameter";
        break;
    case Kind_TypeDef:
        d << "TypeDefModelItem";
        break;
    case Kind_TemplateTypeAlias:
        d << "TemplateTypeAliasModelItem";
        break;
    default:
        d << "CodeModelItem";
        break;
    }
}

void _CodeModelItem::formatDebug(QDebug &d) const
{
     d << "(\"" << name() << '"';
     if (!m_scope.isEmpty()) {
         d << ", scope=";
         formatSequence(d, m_scope.cbegin(), m_scope.cend(), "::");
     }
     if (!m_fileName.isEmpty()) {
         d << ", file=\"" << QDir::toNativeSeparators(m_fileName);
         if (m_startLine > 0)
              d << ':' << m_startLine;
         d << '"';
     }
}

QDebug operator<<(QDebug d, const _CodeModelItem *t)
{
    QDebugStateSaver s(d);
    d.noquote();
    d.nospace();
    if (!t) {
        d << "CodeModelItem(0)";
        return d;
    }
    _CodeModelItem::formatKind(d, t->kind());
    t->formatDebug(d);
    switch (t->kind()) {
    case  _CodeModelItem::Kind_Class:
        d << " /* class " << t->name() << " */";
        break;
    case  _CodeModelItem::Kind_Namespace:
        d << " /* namespace " << t->name() << " */";
        break;
    }
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_ClassModelItem::~_ClassModelItem() = default;

TemplateParameterList _ClassModelItem::templateParameters() const
{
    return m_templateParameters;
}

void _ClassModelItem::setTemplateParameters(const TemplateParameterList &templateParameters)
{
    m_templateParameters = templateParameters;
}

bool _ClassModelItem::extendsClass(const QString &name) const
{
    for (const BaseClass &bc : m_baseClasses) {
        if (bc.name == name)
            return true;
    }
    return false;
}

_ClassModelItem::_ClassModelItem(CodeModel *model, int kind)
    : _ScopeModelItem(model, kind)
{
}

_ClassModelItem::_ClassModelItem(CodeModel *model, const QString &name, int kind)
    : _ScopeModelItem(model, name, kind)
{
}

const QList<_ClassModelItem::UsingMember> &_ClassModelItem::usingMembers() const
{
    return m_usingMembers;
}

void _ClassModelItem::addUsingMember(const QString &className,
                                     const QString &memberName,
                                     Access accessPolicy)
{
    m_usingMembers.append({className, memberName, accessPolicy});
}

void _ClassModelItem::setClassType(CodeModel::ClassType type)
{
    m_classType = type;
}

CodeModel::ClassType _ClassModelItem::classType() const
{
    return m_classType;
}

void _ClassModelItem::addPropertyDeclaration(const QString &propertyDeclaration)
{
    m_propertyDeclarations << propertyDeclaration;
}

bool _ClassModelItem::isEmpty() const
{
    return _ScopeModelItem::isEmpty() && m_propertyDeclarations.isEmpty();
}

bool _ClassModelItem::isTemplate() const
{
    return !m_templateParameters.isEmpty();
}

#ifndef QT_NO_DEBUG_STREAM
template <class List>
static void formatModelItemList(QDebug &d, const char *prefix, const List &l,
                                const char *separator = ", ")
{
    if (const auto size = l.size()) {
        d << prefix << '[' << size << "](";
        for (qsizetype i = 0; i < size; ++i) {
            if (i)
                d << separator;
            l.at(i)->formatDebug(d);
        }
        d << ')';
    }
}

void _ClassModelItem::formatDebug(QDebug &d) const
{
    _ScopeModelItem::formatDebug(d);
    if (!m_baseClasses.isEmpty()) {
        if (m_final)
            d << " [final]";
        d << ", inherits=";
        d << ", inherits=";
        for (qsizetype i = 0, size = m_baseClasses.size(); i < size; ++i) {
            if (i)
                d << ", ";
            d << m_baseClasses.at(i).name << " (" << m_baseClasses.at(i).accessPolicy << ')';
        }
    }
    for (const auto &im : m_usingMembers)
        d << ", using " << im.className << "::" << im.memberName
            << " (" << im.access << ')';
    formatModelItemList(d, ", templateParameters=", m_templateParameters);
    formatScopeItemsDebug(d);
    if (!m_propertyDeclarations.isEmpty())
        d << ", Properties=" << m_propertyDeclarations;
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
FunctionModelItem _ScopeModelItem::declaredFunction(const FunctionModelItem &item)
{
    for (const FunctionModelItem &fun : std::as_const(m_functions)) {
        if (fun->name() == item->name() && fun->isSimilar(item))
            return fun;

    }
    return FunctionModelItem();
}

_ScopeModelItem::~_ScopeModelItem() = default;

void _ScopeModelItem::addEnumsDeclaration(const QString &enumsDeclaration)
{
    m_enumsDeclarations << enumsDeclaration;
}

void _ScopeModelItem::addClass(const ClassModelItem &item)
{
    m_classes.append(item);
    item->setEnclosingScope(this);
}

void _ScopeModelItem::addFunction(const FunctionModelItem &item)
{
    m_functions.append(item);
    item->setEnclosingScope(this);
}

void _ScopeModelItem::addVariable(const VariableModelItem &item)
{
    m_variables.append(item);
    item->setEnclosingScope(this);
}

void _ScopeModelItem::addTypeDef(const TypeDefModelItem &item)
{
    m_typeDefs.append(item);
    item->setEnclosingScope(this);
}

void _ScopeModelItem::addTemplateTypeAlias(const TemplateTypeAliasModelItem &item)
{
    m_templateTypeAliases.append(item);
    item->setEnclosingScope(this);
}

qsizetype _ScopeModelItem::indexOfEnum(const QString &name) const
{
    for (qsizetype i = 0, size = m_enums.size(); i < size; ++i) {
        if (m_enums.at(i)->name() == name)
            return i;
    }
    return -1;
}

void _ScopeModelItem::addEnum(const EnumModelItem &item)
{
    item->setEnclosingScope(this);
    // A forward declaration of an enum ("enum class Foo;") is undistinguishable
    // from an enum without values ("enum class QCborTag {}"), so, add all
    // enums and replace existing ones without values by ones with values.
    const int index = indexOfEnum(item->name());
    if (index >= 0) {
        if (item->hasValues() && !m_enums.at(index)->hasValues())
            m_enums[index] = item;
        return;
    }
    m_enums.append(item);
}

void _ScopeModelItem::appendScope(const _ScopeModelItem &other)
{
    m_classes += other.m_classes;
    m_enums += other.m_enums;
    m_typeDefs += other.m_typeDefs;
    m_templateTypeAliases += other.m_templateTypeAliases;
    m_variables += other.m_variables;
    m_functions += other.m_functions;
    m_enumsDeclarations += other.m_enumsDeclarations;
}

bool _ScopeModelItem::isEmpty() const
{
    return m_classes.isEmpty() && m_enums.isEmpty()
        && m_typeDefs.isEmpty() && m_templateTypeAliases.isEmpty()
        && m_variables.isEmpty() && m_functions.isEmpty()
        && m_enumsDeclarations.isEmpty();
}

/* This function removes MSVC export declarations of non-type template
 * specializations (see below code from photon.h) for which
 * clang_isCursorDefinition() returns true, causing them to be added as
 * definitions of empty classes shadowing the template definition depending
 *  on QHash seed values.

template <int N> class Tpl
{
public:
...
};

#ifdef WIN32
template class LIBSAMPLE_EXPORT Tpl<54>;
*/
void _ScopeModelItem::purgeClassDeclarations()
{
    for (auto i = m_classes.size() - 1; i >= 0; --i) {
        auto klass = m_classes.at(i);
        // For an empty class, check if there is a matching template
        // definition, and remove it if this is the case.
        if (!klass->isTemplate() && klass->isEmpty()) {
            const QString definitionPrefix = klass->name() + u'<';
            const bool definitionFound =
                std::any_of(m_classes.cbegin(), m_classes.cend(),
                            [definitionPrefix] (const ClassModelItem &c) {
                    return c->isTemplate() && !c->isEmpty()
                        && c->name().startsWith(definitionPrefix);
            });
            if (definitionFound)
                m_classes.removeAt(i);
        }
    }
}

#ifndef QT_NO_DEBUG_STREAM
template <class Hash>
static void formatScopeHash(QDebug &d, const char *prefix, const Hash &h,
                            const char *separator = ", ",
                            bool trailingNewLine = false)
{
    if (!h.isEmpty()) {
        d << prefix << '[' << h.size() << "](";
        const auto begin = h.cbegin();
        for (auto it = begin, end = h.cend(); it != end; ++it) { // Omit the names as they are repeated
            if (it != begin)
                d << separator;
            d << it.value().data();
        }
        d << ')';
        if (trailingNewLine)
            d << '\n';
    }
}

template <class List>
static void formatScopeList(QDebug &d, const char *prefix, const List &l,
                            const char *separator = ", ",
                            bool trailingNewLine = false)
{
    if (!l.isEmpty()) {
        d << prefix << '[' << l.size() << "](";
        formatPtrSequence(d, l.begin(), l.end(), separator);
        d << ')';
        if (trailingNewLine)
            d << '\n';
    }
}

void _ScopeModelItem::formatScopeItemsDebug(QDebug &d) const
{
    formatScopeList(d, ", classes=", m_classes, "\n", true);
    formatScopeList(d, ", enums=", m_enums, "\n", true);
    formatScopeList(d, ", aliases=", m_typeDefs, "\n", true);
    formatScopeList(d, ", template type aliases=", m_templateTypeAliases, "\n", true);
    formatScopeList(d, ", functions=", m_functions, "\n", true);
    formatScopeList(d, ", variables=", m_variables);
}

void  _ScopeModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    formatScopeItemsDebug(d);
}
#endif // !QT_NO_DEBUG_STREAM

namespace {
// Predicate to match a non-template class name against the class list.
// "Vector" should match "Vector" as well as "Vector<T>" (as seen for methods
// from within the class "Vector").
class ClassNamePredicate
{
public:
    explicit ClassNamePredicate(const QString &name) : m_name(name) {}
    bool operator()(const ClassModelItem &item) const
    {
        const QString &itemName = item->name();
        if (!itemName.startsWith(m_name))
            return false;
        return itemName.size() == m_name.size() || itemName.at(m_name.size()) == u'<';
    }

private:
    const QString m_name;
};
} // namespace

ClassModelItem _ScopeModelItem::findClass(const QString &name) const
{
    // A fully qualified template is matched by name only
    const ClassList::const_iterator it = name.contains(u'<')
        ? std::find_if(m_classes.begin(), m_classes.end(), ModelItemNamePredicate<_ClassModelItem>(name))
        : std::find_if(m_classes.begin(), m_classes.end(), ClassNamePredicate(name));
    return it != m_classes.end() ? *it : ClassModelItem();
}

VariableModelItem _ScopeModelItem::findVariable(const QString &name) const
{
    return findModelItem(m_variables, name);
}

TypeDefModelItem _ScopeModelItem::findTypeDef(const QString &name) const
{
    return findModelItem(m_typeDefs, name);
}

TemplateTypeAliasModelItem _ScopeModelItem::findTemplateTypeAlias(const QString &name) const
{
    return findModelItem(m_templateTypeAliases, name);
}

EnumModelItem _ScopeModelItem::findEnum(const QString &name) const
{
    return findModelItem(m_enums, name);
}

_ScopeModelItem::FindEnumByValueReturn
    _ScopeModelItem::findEnumByValueHelper(QStringView fullValue,
                                       QStringView enumValue) const
{
    const bool unqualified = fullValue.size() == enumValue.size();
    QString scopePrefix = scope().join(u"::");
    if (!scopePrefix.isEmpty())
        scopePrefix += u"::"_s;
    scopePrefix += name() + u"::"_s;

    for (const auto &e : m_enums) {
        const auto index = e->indexOfValue(enumValue);
        if (index != -1) {
            QString fullyQualifiedName = scopePrefix;
            if (e->enumKind() != AnonymousEnum)
                fullyQualifiedName += e->name() + u"::"_s;
            fullyQualifiedName += e->enumerators().at(index)->name();
            if (unqualified || fullyQualifiedName.endsWith(fullValue))
                return {e, fullyQualifiedName};
            // For standard enums, check the name without enum name
            if (e->enumKind() == CEnum) {
                const QString qualifiedName =
                    scopePrefix + e->enumerators().at(index)->name();
                if (qualifiedName.endsWith(fullValue))
                    return {e, fullyQualifiedName};
            }
        }
    }

    return {};
}

// Helper to recursively find the scope of an enum value
_ScopeModelItem::FindEnumByValueReturn
    _ScopeModelItem::findEnumByValueRecursion(const _ScopeModelItem *scope,
                                              QStringView fullValue,
                                              QStringView enumValue,
                                              bool searchSiblingNamespaces)
{
    if (const auto e = scope->findEnumByValueHelper(fullValue, enumValue))
        return e;

    if (auto *enclosingScope = scope->enclosingScope()) {
        // The enclosing scope may have several sibling namespaces of that name.
        if (searchSiblingNamespaces && scope->kind() == Kind_Namespace) {
            if (auto *enclosingNamespace = dynamic_cast<const _NamespaceModelItem *>(enclosingScope)) {
                for (const auto &sibling : enclosingNamespace->namespaces()) {
                    if (sibling.get() != scope && sibling->name() == scope->name()) {
                        if (const auto e = findEnumByValueRecursion(sibling.get(),
                                                                    fullValue, enumValue, false)) {
                            return e;
                        }
                    }
                }
            }
        }

        if (const auto e = findEnumByValueRecursion(enclosingScope, fullValue, enumValue))
            return e;
    }

    // PYSIDE-331: We need to also search the base classes.
    if (auto *classItem = dynamic_cast<const _ClassModelItem *>(scope)) {
        for (const auto &base : classItem->baseClasses()) {
            if (base.klass) {
                auto *c = base.klass.get();
                if (const auto e = findEnumByValueRecursion(c, fullValue, enumValue))
                    return e;
            }
        }
    }

    return {};
}

_ScopeModelItem::FindEnumByValueReturn
   _ScopeModelItem::findEnumByValue(QStringView value) const
{
    const auto lastQualifier = value.lastIndexOf(u"::");
    const auto enumValue = lastQualifier == -1
                           ? value : value.mid(lastQualifier + 2);
    return findEnumByValueRecursion(this, value, enumValue);
}

FunctionList _ScopeModelItem::findFunctions(const QString &name) const
{
    FunctionList result;
    for (const FunctionModelItem &func : m_functions) {
        if (func->name() == name)
            result.append(func);
    }
    return result;
}

// ---------------------------------------------------------------------------
_NamespaceModelItem::_NamespaceModelItem(CodeModel *model, int kind)
    : _ScopeModelItem(model, kind)
{
}

_NamespaceModelItem::_NamespaceModelItem(CodeModel *model, const QString &name, int kind)
    : _ScopeModelItem(model, name, kind)
{
}

_NamespaceModelItem::~_NamespaceModelItem() = default;

void _NamespaceModelItem::addNamespace(NamespaceModelItem item)
{
    item->setEnclosingScope(this);
    m_namespaces.append(item);
}

NamespaceModelItem _NamespaceModelItem::findNamespace(const QString &name) const
{
    return findModelItem(m_namespaces, name);
}

_FileModelItem::~_FileModelItem() = default;

void  _NamespaceModelItem::appendNamespace(const _NamespaceModelItem &other)
{
    appendScope(other);
    m_namespaces += other.m_namespaces;
}

#ifndef QT_NO_DEBUG_STREAM
void _NamespaceModelItem::formatDebug(QDebug &d) const
{
    _ScopeModelItem::formatDebug(d);
    switch (m_type) {
    case NamespaceType::Default:
        break;
    case NamespaceType::Anonymous:
        d << ", anonymous";
        break;
    case NamespaceType::Inline:
        d << ", inline";
        break;
    }
    formatScopeList(d, ", namespaces=", m_namespaces);
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_ArgumentModelItem::_ArgumentModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

_ArgumentModelItem::_ArgumentModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

_ArgumentModelItem::~_ArgumentModelItem()
{
}

TypeInfo _ArgumentModelItem::type() const
{
    return m_type;
}

void _ArgumentModelItem::setType(const TypeInfo &type)
{
    m_type = type;
}

bool _ArgumentModelItem::defaultValue() const
{
    return m_defaultValue;
}

void _ArgumentModelItem::setDefaultValue(bool defaultValue)
{
    m_defaultValue = defaultValue;
}

bool _ArgumentModelItem::scopeResolution() const
{
    return m_scopeResolution;
}

void _ArgumentModelItem::setScopeResolution(bool v)
{
    m_scopeResolution = v;
}

#ifndef QT_NO_DEBUG_STREAM
void _ArgumentModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", type=" << m_type;
    if (m_scopeResolution)
        d << ", [m_scope resolution]";
    if (m_defaultValue)
        d << ", defaultValue=\"" << m_defaultValueExpression << '"';
}
#endif // !QT_NO_DEBUG_STREAM
// ---------------------------------------------------------------------------
_FunctionModelItem::~_FunctionModelItem() = default;

bool _FunctionModelItem::isSimilar(const FunctionModelItem &other) const
{
    if (name() != other->name())
        return false;

    if (isConstant() != other->isConstant())
        return false;

    if (isVariadics() != other->isVariadics())
        return false;

    if (arguments().size() != other->arguments().size())
        return false;

    // ### check the template parameters

    for (qsizetype i = 0; i < arguments().size(); ++i) {
        ArgumentModelItem arg1 = arguments().at(i);
        ArgumentModelItem arg2 = other->arguments().at(i);

        if (arg1->type() != arg2->type())
            return false;
    }

    return true;
}

_FunctionModelItem::_FunctionModelItem(CodeModel *model, int kind)
    : _MemberModelItem(model, kind), m_flags(0)
{
}

_FunctionModelItem::_FunctionModelItem(CodeModel *model, const QString &name, int kind)
    : _MemberModelItem(model, name, kind), m_flags(0)
{
}

ArgumentList _FunctionModelItem::arguments() const
{
    return m_arguments;
}

void _FunctionModelItem::addArgument(const ArgumentModelItem& item)
{
    m_arguments.append(item);
}

CodeModel::FunctionType _FunctionModelItem::functionType() const
{
    return m_functionType;
}

void _FunctionModelItem::setFunctionType(CodeModel::FunctionType functionType)
{
    m_functionType = functionType;
}

bool _FunctionModelItem::isVariadics() const
{
    return m_isVariadics;
}

void _FunctionModelItem::setVariadics(bool isVariadics)
{
    m_isVariadics = isVariadics;
}

bool _FunctionModelItem::scopeResolution() const
{
    return m_scopeResolution;
}

void _FunctionModelItem::setScopeResolution(bool v)
{
    m_scopeResolution = v;
}

bool _FunctionModelItem::isDefaultConstructor() const
{
    return m_functionType == CodeModel::Constructor
           && (m_arguments.isEmpty() || m_arguments.constFirst()->defaultValue());
}

bool _FunctionModelItem::isSpaceshipOperator() const
{
    return m_functionType == CodeModel::ComparisonOperator
        && name() == u"operator<=>";
}

bool _FunctionModelItem::isNoExcept() const
{
    return m_exceptionSpecification == ExceptionSpecification::NoExcept;
}

bool _FunctionModelItem::isOperator() const
{
    bool result = false;
    switch (m_functionType) {
    case CodeModel::CallOperator:
    case CodeModel::ConversionOperator:
    case CodeModel::DereferenceOperator:
    case CodeModel::ReferenceOperator:
    case CodeModel::ArrowOperator:
    case CodeModel::ArithmeticOperator:
    case CodeModel::IncrementOperator:
    case CodeModel::DecrementOperator:
    case CodeModel::BitwiseOperator:
    case CodeModel::LogicalOperator:
    case CodeModel::ShiftOperator:
    case CodeModel::SubscriptOperator:
    case CodeModel::ComparisonOperator:
        result = true;
        break;
    default:
        break;
    }
    return result;
}

ExceptionSpecification _FunctionModelItem::exceptionSpecification() const
{
    return m_exceptionSpecification;
}

void _FunctionModelItem::setExceptionSpecification(ExceptionSpecification e)
{
    m_exceptionSpecification = e;
}

bool _FunctionModelItem::isDeleted() const
{
    return m_isDeleted;
}

void _FunctionModelItem::setDeleted(bool d)
{
    m_isDeleted = d;
}

bool _FunctionModelItem::isDeprecated() const
{
    return m_isDeprecated;
}

void _FunctionModelItem::setDeprecated(bool d)
{
    m_isDeprecated = d;
}

bool _FunctionModelItem::isVirtual() const
{
    return m_isVirtual;
}

void _FunctionModelItem::setVirtual(bool isVirtual)
{
    m_isVirtual = isVirtual;
}

bool _FunctionModelItem::isInline() const
{
    return m_isInline;
}

bool _FunctionModelItem::isOverride() const
{
    return m_isOverride;
}

void _FunctionModelItem::setOverride(bool o)
{
    m_isOverride = o;
}

bool _FunctionModelItem::isFinal() const
{
    return m_isFinal;
}

void _FunctionModelItem::setFinal(bool f)
{
    m_isFinal = f;
}

void _FunctionModelItem::setInline(bool isInline)
{
    m_isInline = isInline;
}

bool _FunctionModelItem::isExplicit() const
{
    return m_isExplicit;
}

void _FunctionModelItem::setExplicit(bool isExplicit)
{
    m_isExplicit = isExplicit;
}

bool _FunctionModelItem::isHiddenFriend() const
{
    return m_isHiddenFriend;
}

void _FunctionModelItem::setHiddenFriend(bool f)
{
    m_isHiddenFriend = f;
}

bool _FunctionModelItem::isAbstract() const
{
    return m_isAbstract;
}

void _FunctionModelItem::setAbstract(bool isAbstract)
{
    m_isAbstract = isAbstract;
}

// Qt
bool _FunctionModelItem::isInvokable() const
{
    return m_isInvokable;
}

void _FunctionModelItem::setInvokable(bool isInvokable)
{
    m_isInvokable = isInvokable;
}

QString _FunctionModelItem::typeSystemSignature() const  // For dumping out type system files
{
    QString result;
    QTextStream str(&result);
    str << name() << '(';
    for (qsizetype a = 0, size = m_arguments.size(); a < size; ++a) {
        if (a)
            str << ',';
        m_arguments.at(a)->type().formatTypeSystemSignature(str);
    }
    str << ')';
    return result;
}

using NameFunctionTypeHash = QHash<QStringView, CodeModel::FunctionType>;

static const NameFunctionTypeHash &nameToOperatorFunction()
{
    static const NameFunctionTypeHash result = {
        {u"operator=",   CodeModel::AssignmentOperator},
        {u"operator+",   CodeModel::ArithmeticOperator},
        {u"operator+=",  CodeModel::ArithmeticOperator},
        {u"operator-",   CodeModel::ArithmeticOperator},
        {u"operator-=",  CodeModel::ArithmeticOperator},
        {u"operator*",   CodeModel::ArithmeticOperator},
        {u"operator*=",  CodeModel::ArithmeticOperator},
        {u"operator/",   CodeModel::ArithmeticOperator},
        {u"operator/=",  CodeModel::ArithmeticOperator},
        {u"operator%",   CodeModel::ArithmeticOperator},
        {u"operator%=",  CodeModel::ArithmeticOperator},
        {u"operator++",  CodeModel::IncrementOperator},
        {u"operator--",  CodeModel::DecrementOperator},
        {u"operator&",   CodeModel::BitwiseOperator},
        {u"operator&=",  CodeModel::BitwiseOperator},
        {u"operator|",   CodeModel::BitwiseOperator},
        {u"operator|=",  CodeModel::BitwiseOperator},
        {u"operator^",   CodeModel::BitwiseOperator},
        {u"operator^=",  CodeModel::BitwiseOperator},
        {u"operator~",   CodeModel::BitwiseOperator},
        {u"operator<<",  CodeModel::ShiftOperator},
        {u"operator<<=", CodeModel::ShiftOperator},
        {u"operator>>",  CodeModel::ShiftOperator},
        {u"operator>>=", CodeModel::ShiftOperator},
        {u"operator<",   CodeModel::ComparisonOperator},
        {u"operator<=",  CodeModel::ComparisonOperator},
        {u"operator>",   CodeModel::ComparisonOperator},
        {u"operator>=",  CodeModel::ComparisonOperator},
        {u"operator==",  CodeModel::ComparisonOperator},
        {u"operator!=",  CodeModel::ComparisonOperator},
        {u"operator<=>", CodeModel::ComparisonOperator},
        {u"operator!",   CodeModel::LogicalOperator},
        {u"operator&&",  CodeModel::LogicalOperator},
        {u"operator||",  CodeModel::LogicalOperator},
        {u"operator[]",  CodeModel::SubscriptOperator},
        {u"operator()",  CodeModel::CallOperator}, // Can be void
        {u"operator->",  CodeModel::ArrowOperator}
    };
    return result;
}

std::optional<CodeModel::FunctionType> _FunctionModelItem::functionTypeFromName(QStringView name)
{
    const auto it = nameToOperatorFunction().constFind(name);
    if (it != nameToOperatorFunction().constEnd())
        return it.value();
    // This check is only for added functions. Clang detects this
    // by cursor type  CXCursor_ConversionFunction.
    if (name.startsWith(u"operator "))
        return CodeModel::ConversionOperator;
    return {};
}

// Check for operators, etc. unless it is a specific type like a constructor
CodeModel::FunctionType _FunctionModelItem::_determineTypeHelper() const
{
    switch (m_functionType) {
    case CodeModel::Constructor:
    case CodeModel::CopyConstructor:
    case CodeModel::MoveConstructor:
    case CodeModel::Destructor:
    case CodeModel::Signal:
    case CodeModel::Slot:
        return m_functionType; // nothing to do here
    default:
        break;
    }
    const QString &functionName = name();
    const auto newTypeOpt = _FunctionModelItem::functionTypeFromName(functionName);
    if (!newTypeOpt.has_value())
        return m_functionType;

    auto newType = newTypeOpt.value();
    // It's some sort of dereference operator?!
    if (m_arguments.isEmpty()) {
        switch (newType) {
        case CodeModel::ArithmeticOperator:
            if (functionName == u"operator*")
                return CodeModel::DereferenceOperator;
            break;
        case CodeModel::BitwiseOperator:
            if (functionName == u"operator&")
                return CodeModel::ReferenceOperator;
            break;
        default:
            break;
        }
    }
    return newType;
}

void _FunctionModelItem::_determineType()
{
    m_functionType = _determineTypeHelper();
}

#ifndef QT_NO_DEBUG_STREAM
void _FunctionModelItem::formatDebug(QDebug &d) const
{
    _MemberModelItem::formatDebug(d);
    d << ", type=" << m_functionType << ", exspec=" << int(m_exceptionSpecification);
    if (m_isDeleted)
        d << " [deleted!]";
    if (m_isInline)
        d << " [inline]";
    if (m_isVirtual)
        d << " [virtual]";
    if (m_isOverride)
        d << " [override]";
    if (m_isDeprecated)
        d << " [deprecated]";
    if (m_isFinal)
        d << " [final]";
    if (m_isAbstract)
        d << " [abstract]";
    if (m_isExplicit)
        d << " [explicit]";
    if (m_isInvokable)
        d << " [invokable]";
    if (m_scopeResolution)
        d << " [scope resolution]";
    formatModelItemList(d, ", arguments=", m_arguments);
    if (m_isVariadics)
        d << ",...";
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_TypeDefModelItem::_TypeDefModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

_TypeDefModelItem::_TypeDefModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

TypeInfo _TypeDefModelItem::type() const
{
    return m_type;
}

void _TypeDefModelItem::setType(const TypeInfo &type)
{
    m_type = type;
}

#ifndef QT_NO_DEBUG_STREAM
void _TypeDefModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", type=" << m_type;
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------

_TemplateTypeAliasModelItem::_TemplateTypeAliasModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind) {}

_TemplateTypeAliasModelItem::_TemplateTypeAliasModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind) {}

TemplateParameterList _TemplateTypeAliasModelItem::templateParameters() const
{
    return m_templateParameters;
}

void _TemplateTypeAliasModelItem::addTemplateParameter(const TemplateParameterModelItem &templateParameter)
{
    m_templateParameters.append(templateParameter);
}

TypeInfo _TemplateTypeAliasModelItem::type() const
{
    return m_type;
}

void _TemplateTypeAliasModelItem::setType(const TypeInfo &type)
{
    m_type = type;
}

#ifndef QT_NO_DEBUG_STREAM
void _TemplateTypeAliasModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", <";
    for (qsizetype i = 0, count = m_templateParameters.size(); i < count; ++i) {
        if (i)
            d << ", ";
        d << m_templateParameters.at(i)->name();
    }
    d << ">, type=" << m_type;
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_EnumModelItem::_EnumModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

_EnumModelItem::_EnumModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

Access _EnumModelItem::accessPolicy() const
{
    return m_accessPolicy;
}

_EnumModelItem::~_EnumModelItem() = default;

void _EnumModelItem::setAccessPolicy(Access accessPolicy)
{
    m_accessPolicy = accessPolicy;
}

EnumeratorList _EnumModelItem::enumerators() const
{
    return m_enumerators;
}

void _EnumModelItem::addEnumerator(const EnumeratorModelItem &item)
{
    m_enumerators.append(item);
}

qsizetype _EnumModelItem::indexOfValue(QStringView value) const
{
    for (qsizetype i = 0, size = m_enumerators.size(); i < size; ++i) {
        if (m_enumerators.at(i)->name() == value)
            return i;
    }
    return -1;
}

bool _EnumModelItem::isSigned() const
{
    return m_signed;
}

void _EnumModelItem::setSigned(bool s)
{
    m_signed = s;
}

bool _EnumModelItem::isDeprecated() const
{
    return m_deprecated;
}

void _EnumModelItem::setDeprecated(bool d)
{
    m_deprecated = d;
}

#ifndef QT_NO_DEBUG_STREAM
void _EnumModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    switch (m_enumKind) {
    case CEnum:
        break;
    case AnonymousEnum:
        d << " (anonymous)";
        break;
    case EnumClass:
        d << " (class)";
        break;
    }
    if (m_deprecated)
        d << " (deprecated)";
    if (!m_signed)
         d << " (unsigned)";
    formatModelItemList(d, ", enumerators=", m_enumerators);
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_EnumeratorModelItem::~_EnumeratorModelItem() = default;

_EnumeratorModelItem::_EnumeratorModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

_EnumeratorModelItem::_EnumeratorModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

QString _EnumeratorModelItem::stringValue() const
{
    return m_stringValue;
}

void _EnumeratorModelItem::setStringValue(const QString &value)
{
    m_stringValue = value;
}

bool _EnumeratorModelItem::isDeprecated() const
{
    return m_deprecated;
}

void _EnumeratorModelItem::setDeprecated(bool d)
{
    m_deprecated = d;
}

#ifndef QT_NO_DEBUG_STREAM
void _EnumeratorModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", value=" << m_value << ", stringValue=\"" << m_stringValue << '"';
    if (m_deprecated)
        d << " (deprecated)";
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
_TemplateParameterModelItem::~_TemplateParameterModelItem() = default;

_TemplateParameterModelItem::_TemplateParameterModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind)
{
}

_TemplateParameterModelItem::_TemplateParameterModelItem(CodeModel *model,
                                                         const QString &name, int kind)
    : _CodeModelItem(model, name, kind)
{
}

TypeInfo _TemplateParameterModelItem::type() const
{
    return m_type;
}

void _TemplateParameterModelItem::setType(const TypeInfo &type)
{
    m_type = type;
}

bool _TemplateParameterModelItem::defaultValue() const
{
    return m_defaultValue;
}

void _TemplateParameterModelItem::setDefaultValue(bool defaultValue)
{
    m_defaultValue = defaultValue;
}

#ifndef QT_NO_DEBUG_STREAM
void _TemplateParameterModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", type=" << m_type;
    if (m_defaultValue)
        d << " [defaultValue]";
}
#endif // !QT_NO_DEBUG_STREAM

// ---------------------------------------------------------------------------
TypeInfo _MemberModelItem::type() const
{
    return m_type;
}

void _MemberModelItem::setType(const TypeInfo &type)
{
    m_type = type;
}

Access _MemberModelItem::accessPolicy() const
{
    return m_accessPolicy;
}

_MemberModelItem::~_MemberModelItem() = default;

void _MemberModelItem::setAccessPolicy(Access accessPolicy)
{
    m_accessPolicy = accessPolicy;
}

bool _MemberModelItem::isStatic() const
{
    return m_isStatic;
}

void _MemberModelItem::setStatic(bool isStatic)
{
    m_isStatic = isStatic;
}

_MemberModelItem::_MemberModelItem(CodeModel *model, int kind)
    : _CodeModelItem(model, kind), m_flags(0)
{
}

_MemberModelItem::_MemberModelItem(CodeModel *model, const QString &name, int kind)
    : _CodeModelItem(model, name, kind), m_flags(0)
{
}

bool _MemberModelItem::isConstant() const
{
    return m_isConstant;
}

void _MemberModelItem::setConstant(bool isConstant)
{
    m_isConstant = isConstant;
}

bool _MemberModelItem::isVolatile() const
{
    return m_isVolatile;
}

void _MemberModelItem::setVolatile(bool isVolatile)
{
    m_isVolatile = isVolatile;
}

bool _MemberModelItem::isAuto() const
{
    return m_isAuto;
}

void _MemberModelItem::setAuto(bool isAuto)
{
    m_isAuto = isAuto;
}

bool _MemberModelItem::isFriend() const
{
    return m_isFriend;
}

void _MemberModelItem::setFriend(bool isFriend)
{
    m_isFriend = isFriend;
}

bool _MemberModelItem::isRegister() const
{
    return m_isRegister;
}

void _MemberModelItem::setRegister(bool isRegister)
{
    m_isRegister = isRegister;
}

bool _MemberModelItem::isExtern() const
{
    return m_isExtern;
}

void _MemberModelItem::setExtern(bool isExtern)
{
    m_isExtern = isExtern;
}

bool _MemberModelItem::isMutable() const
{
    return m_isMutable;
}

void _MemberModelItem::setMutable(bool isMutable)
{
    m_isMutable = isMutable;
}

#ifndef QT_NO_DEBUG_STREAM
void _MemberModelItem::formatDebug(QDebug &d) const
{
    _CodeModelItem::formatDebug(d);
    d << ", " << m_accessPolicy << ", type=";
    if (m_isConstant)
        d << "const ";
    if (m_isVolatile)
        d << "volatile ";
    if (m_isStatic)
        d << "static ";
    if (m_isAuto)
        d << "auto ";
    if (m_isFriend)
        d << "friend ";
    if (m_isRegister)
        d << "register ";
    if (m_isExtern)
        d << "extern ";
    if (m_isMutable)
        d << "mutable ";
    d << m_type;
    formatScopeList(d, ", templateParameters", m_templateParameters);
}
#endif // !QT_NO_DEBUG_STREAM

// kate: space-indent on; indent-width 2; replace-tabs on;

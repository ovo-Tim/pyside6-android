// Copyright (C) 2020 The Qt Company Ltd.
// Copyright (C) 2002-2005 Roberto Raggi <roberto@kdevelop.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


#include "typeinfo.h"
#include "codemodel.h"

#include <clangparser/clangutils.h>
#include <debughelpers_p.h>

#include "qtcompat.h"

#include <QtCore/QDebug>
#include <QtCore/QStack>
#include <QtCore/QTextStream>

#include <iostream>

using namespace Qt::StringLiterals;

class TypeInfoData : public QSharedData
{

public:
    TypeInfoData();

    bool isVoid() const;
    bool equals(const TypeInfoData &other) const;
    bool isStdType() const;
    void simplifyStdType();

    QStringList m_qualifiedName;
    QStringList m_arrayElements;
    TypeInfo::TypeInfoList m_arguments;
    TypeInfo::TypeInfoList m_instantiations;
    TypeInfo::Indirections m_indirections;

    union {
        uint flags;

        struct {
            uint m_constant: 1;
            uint m_volatile: 1;
            uint m_functionPointer: 1;
            uint m_padding: 29;
        };
    };

    ReferenceType m_referenceType;
};

TypeInfoData::TypeInfoData() : flags(0), m_referenceType(NoReference)
{
}

TypeInfo::TypeInfo() : d(new TypeInfoData)
{
}

TypeInfo::~TypeInfo() = default;
TypeInfo::TypeInfo(const TypeInfo &) = default;
TypeInfo& TypeInfo::operator=(const TypeInfo &) = default;
TypeInfo::TypeInfo(TypeInfo &&) = default;
TypeInfo& TypeInfo::operator=(TypeInfo &&) = default;


static inline TypeInfo createType(const QString &name)
{
    TypeInfo result;
    result.addName(name);
    return result;
}

TypeInfo TypeInfo::voidType()
{
    static const TypeInfo result = createType(u"void"_s);
    return result;
}

TypeInfo TypeInfo::varArgsType()
{
    static const TypeInfo result = createType(u"..."_s);
    return result;
}

TypeInfo TypeInfo::combine(const TypeInfo &__lhs, const TypeInfo &__rhs)
{
    TypeInfo __result = __lhs;

    __result.setConstant(__result.isConstant() || __rhs.isConstant());
    __result.setVolatile(__result.isVolatile() || __rhs.isVolatile());
    if (__rhs.referenceType() > __result.referenceType())
        __result.setReferenceType(__rhs.referenceType());

    const auto indirections = __rhs.indirectionsV();
    for (auto i : indirections)
        __result.addIndirection(i);

    __result.setArrayElements(__result.arrayElements() + __rhs.arrayElements());

    const auto instantiations = __rhs.instantiations();
    for (const auto &i : instantiations)
        __result.addInstantiation(i);

    return __result;
}

QStringList TypeInfo::qualifiedName() const
{
    return d->m_qualifiedName;
}

void TypeInfo::setQualifiedName(const QStringList &qualified_name)
{
    if (d->m_qualifiedName != qualified_name)
        d->m_qualifiedName = qualified_name;
}

void  TypeInfo::addName(const QString &n)
{
    d->m_qualifiedName.append(n);
}

bool TypeInfoData::isVoid() const
{
    return m_indirections.isEmpty() && m_referenceType == NoReference
        && m_arguments.isEmpty() && m_arrayElements.isEmpty()
        && m_instantiations.isEmpty()
        && m_qualifiedName.size() == 1
        && m_qualifiedName.constFirst() == u"void";
}

bool TypeInfo::isVoid() const
{
    return d->isVoid();
}

bool TypeInfo::isConstant() const
{
    return d->m_constant;
}

void TypeInfo::setConstant(bool is)
{
    if (d->m_constant != is)
        d->m_constant = is;
}

bool TypeInfo::isVolatile() const
{
    return d->m_volatile;
}

void TypeInfo::setVolatile(bool is)
{
    if (d->m_volatile != is)
        d->m_volatile = is;
}

ReferenceType TypeInfo::referenceType() const
{
    return d->m_referenceType;
}

void TypeInfo::setReferenceType(ReferenceType r)
{
    if (d->m_referenceType != r)
        d->m_referenceType = r;
}

const TypeInfo::Indirections &TypeInfo::indirectionsV() const
{
    return d->m_indirections;
}

void TypeInfo::setIndirectionsV(const TypeInfo::Indirections &i)
{
    if (d->m_indirections != i)
        d->m_indirections = i;
}

int TypeInfo::indirections() const
{
    return d->m_indirections.size();
}

void TypeInfo::setIndirections(int indirections)
{
    const Indirections newValue(indirections, Indirection::Pointer);
    if (d->m_indirections != newValue)
        d->m_indirections = newValue;
}

void TypeInfo::addIndirection(Indirection i)
{
    d->m_indirections.append(i);
}

bool TypeInfo::isFunctionPointer() const
{
    return d->m_functionPointer;
}

void TypeInfo::setFunctionPointer(bool is)
{
    if (d->m_functionPointer != is)
        d->m_functionPointer = is;
}

const QStringList &TypeInfo::arrayElements() const
{
    return d->m_arrayElements;
}

void TypeInfo::setArrayElements(const QStringList &arrayElements)
{
    if (d->m_arrayElements != arrayElements)
        d->m_arrayElements = arrayElements;
}

void TypeInfo::addArrayElement(const QString &a)
{
    d->m_arrayElements.append(a);
}

const QList<TypeInfo> &TypeInfo::arguments() const
{
    return d->m_arguments;
}

void TypeInfo::setArguments(const QList<TypeInfo> &arguments)
{
    if (d->m_arguments != arguments)
        d->m_arguments = arguments;
}

void TypeInfo::addArgument(const TypeInfo &arg)
{
    d->m_arguments.append(arg);
}

const TypeInfo::TypeInfoList &TypeInfo::instantiations() const
{
    return d->m_instantiations;
}

TypeInfo::TypeInfoList &TypeInfo::instantiations()
{
    return d->m_instantiations;
}

void TypeInfo::setInstantiations(const TypeInfoList &i)
{
    if (d->m_instantiations != i)
        d->m_instantiations = i;
}

void TypeInfo::addInstantiation(const TypeInfo &i)
{
    d->m_instantiations.append(i);
}

void TypeInfo::clearInstantiations()
{
    if (!d->m_instantiations.isEmpty())
        d->m_instantiations.clear();
}

TypeInfo TypeInfo::resolveType(TypeInfo const &__type, const ScopeModelItem &__scope)
{
    CodeModel *__model = __scope->model();
    Q_ASSERT(__model != nullptr);

    return TypeInfo::resolveType(__model->findItem(__type.qualifiedName(), __scope),  __type, __scope);
}

TypeInfo TypeInfo::resolveType(CodeModelItem __item, TypeInfo const &__type, const ScopeModelItem &__scope)
{
    // Copy the type and replace with the proper qualified name. This
    // only makes sence to do if we're actually getting a resolved
    // type with a namespace. We only get this if the returned type
    // has more than 2 entries in the qualified name... This test
    // could be improved by returning if the type was found or not.
    TypeInfo otherType(__type);
    if (__item && __item->qualifiedName().size() > 1) {
        otherType.setQualifiedName(__item->qualifiedName());
    }

    if (TypeDefModelItem __typedef = std::dynamic_pointer_cast<_TypeDefModelItem>(__item)) {
        const TypeInfo combined = TypeInfo::combine(__typedef->type(), otherType);
        const CodeModelItem nextItem = __scope->model()->findItem(combined.qualifiedName(), __scope);
        if (!nextItem)
            return combined;
        // PYSIDE-362, prevent recursion on opaque structs like
        // typedef struct xcb_connection_t xcb_connection_t;
        if (nextItem.get() ==__item.get()) {
            std::cerr << "** WARNING Bailing out recursion of " << __FUNCTION__
                << "() on " << qPrintable(__type.qualifiedName().join(u"::"_s))
                << std::endl;
            return otherType;
        }
        return resolveType(nextItem, combined, __scope);
    }

    if (TemplateTypeAliasModelItem templateTypeAlias = std::dynamic_pointer_cast<_TemplateTypeAliasModelItem>(__item)) {

        TypeInfo combined = TypeInfo::combine(templateTypeAlias->type(), otherType);
        // For the alias "template<typename T> using QList = QVector<T>" with
        // other="QList<int>", replace the instantiations to obtain "QVector<int>".
        auto aliasInstantiations = templateTypeAlias->type().instantiations();
        const auto &concreteInstantiations = otherType.instantiations();
        const auto count = qMin(aliasInstantiations.size(), concreteInstantiations.size());
        for (qsizetype i = 0; i < count; ++i)
            aliasInstantiations[i] = concreteInstantiations.at(i);
        combined.setInstantiations(aliasInstantiations);
        const CodeModelItem nextItem = __scope->model()->findItem(combined.qualifiedName(), __scope);
        if (!nextItem)
            return combined;
        return resolveType(nextItem, combined, __scope);
    }

    return otherType;
}

// Handler for clang::parseTemplateArgumentList() that populates
// TypeInfo::m_instantiations
class TypeInfoTemplateArgumentHandler
{
public:
    explicit TypeInfoTemplateArgumentHandler(TypeInfo *t)
    {
        m_parseStack.append(t);
    }

    void operator()(int level, QStringView name)
    {
        if (level > m_parseStack.size()) {
            Q_ASSERT(!top()->instantiations().isEmpty());
            m_parseStack.push(&top()->instantiations().back());
        }
        while (level < m_parseStack.size())
            m_parseStack.pop();
        TypeInfo instantiation;
        if (name.startsWith(u"const ")) {
            instantiation.setConstant(true);
            name = name.mid(6);
        }
        instantiation.setQualifiedName(qualifiedName(name));
        top()->addInstantiation(instantiation);
   }

private:
    TypeInfo *top() const { return m_parseStack.back(); }

    static QStringList qualifiedName(QStringView name)
    {
        QStringList result;
        const auto nameParts = name.split(u"::");
        result.reserve(nameParts.size());
        for (const auto &p : nameParts)
            result.append(p.toString());
        return result;
    }

    QStack<TypeInfo *> m_parseStack;
};

QPair<qsizetype, qsizetype> TypeInfo::parseTemplateArgumentList(const QString &l, qsizetype from)
{
    return clang::parseTemplateArgumentList(l, clang::TemplateArgumentHandler(TypeInfoTemplateArgumentHandler(this)), from);
}

QString TypeInfo::toString() const
{
    QString tmp;
    if (isConstant())
        tmp += u"const "_s;

    if (isVolatile())
        tmp += u"volatile "_s;

    tmp += d->m_qualifiedName.join(u"::"_s);

    if (const auto instantiationCount = d->m_instantiations.size()) {
        tmp += u'<';
        for (qsizetype i = 0; i < instantiationCount; ++i) {
            if (i)
                tmp += u", "_s;
            tmp += d->m_instantiations.at(i).toString();
        }
        if (tmp.endsWith(u'>'))
            tmp += u' ';
        tmp += u'>';
    }

    for (Indirection i : d->m_indirections)
        tmp.append(indirectionKeyword(i));

    switch (referenceType()) {
    case NoReference:
        break;
    case LValueReference:
        tmp += u'&';
        break;
    case RValueReference:
        tmp += u"&&"_s;
        break;
    }

    if (isFunctionPointer()) {
        tmp += u" (*)("_s;
        for (qsizetype i = 0; i < d->m_arguments.size(); ++i) {
            if (i != 0)
                tmp += u", "_s;

            tmp += d->m_arguments.at(i).toString();
        }
        tmp += u')';
    }

    for (const QString &elt : d->m_arrayElements)
        tmp += u'[' + elt + u']';

    return tmp;
}

bool TypeInfoData::equals(const TypeInfoData &other) const
{
    if (m_arrayElements.size() != other.m_arrayElements.size())
        return false;

#if defined (RXX_CHECK_ARRAY_ELEMENTS) // ### it'll break
    for (qsizetype i = 0; i < arrayElements().size(); ++i) {
        QString elt1 = arrayElements().at(i).trimmed();
        QString elt2 = other.arrayElements().at(i).trimmed();

        if (elt1 != elt2)
            return false;
    }
#endif

    return flags == other.flags
           && m_qualifiedName == other.m_qualifiedName
           && (!m_functionPointer || m_arguments == other.m_arguments)
           && m_instantiations == other.m_instantiations;
}

bool TypeInfo::equals(const TypeInfo &other) const
{
    return d.data() == other.d.data() || d->equals(*other.d);
}

QString TypeInfo::indirectionKeyword(Indirection i)
{
    return i == Indirection::Pointer
        ? QStringLiteral("*") : QStringLiteral("*const");
}

static inline QString constQualifier() { return QStringLiteral("const"); }
static inline QString volatileQualifier() { return QStringLiteral("volatile"); }

bool TypeInfo::stripLeadingConst(QString *s)
{
    return stripLeadingQualifier(constQualifier(), s);
}

bool TypeInfo::stripLeadingVolatile(QString *s)
{
    return stripLeadingQualifier(volatileQualifier(), s);
}

bool TypeInfo::stripLeadingQualifier(const QString &qualifier, QString *s)
{
    // "const int x"
    const auto qualifierSize = qualifier.size();
    if (s->size() < qualifierSize + 1 || !s->startsWith(qualifier)
        || !s->at(qualifierSize).isSpace()) {
        return false;
    }
    s->remove(0, qualifierSize + 1);
    while (!s->isEmpty() && s->at(0).isSpace())
        s->remove(0, 1);
    return true;
}

// Strip all const/volatile/*/&
void TypeInfo::stripQualifiers(QString *s)
{
    stripLeadingConst(s);
    stripLeadingVolatile(s);
    while (s->endsWith(u'&') || s->endsWith(u'*') || s->endsWith(u' '))
        s->chop(1);
}

// Helper functionality to simplify a raw standard type as returned by
// clang_getCanonicalType() for g++ standard containers from
// "std::__cxx11::list<int, std::allocator<int> >" or
// "std::__1::list<int, std::allocator<int> >" -> "std::list<int>".

bool TypeInfoData::isStdType() const
{
    return m_qualifiedName.size() > 1
        && m_qualifiedName.constFirst() == u"std";
}

bool TypeInfo::isStdType() const
{
    return d->isStdType();
}

static inline bool discardStdType(const QString &name)
{
    return name == u"allocator" || name == u"less";
}

void TypeInfoData::simplifyStdType()
{
    Q_ASSERT(isStdType());
    if (m_qualifiedName.at(1).startsWith(u"__"))
        m_qualifiedName.removeAt(1);
    for (auto t = m_instantiations.size() - 1; t >= 0; --t) {
        if (m_instantiations.at(t).isStdType()) {
            if (discardStdType(m_instantiations.at(t).qualifiedName().constLast()))
                m_instantiations.removeAt(t);
            else
                m_instantiations[t].simplifyStdType();
        }
    }
}

void TypeInfo::simplifyStdType()
{
    if (isStdType())
        d->simplifyStdType();
}

void TypeInfo::formatTypeSystemSignature(QTextStream &str) const
{
    if (d->m_constant)
        str << "const ";
    str << d->m_qualifiedName.join(u"::"_s);
    switch (d->m_referenceType) {
    case NoReference:
        break;
    case LValueReference:
        str << '&';
        break;
    case RValueReference:
        str << "&&";
        break;
    }
    for (auto i : d->m_indirections) {
        switch (i) {
        case Indirection::Pointer:
            str << '*';
            break;
        case Indirection::ConstPointer:
            str << "* const";
            break;
        }
    }
}

#ifndef QT_NO_DEBUG_STREAM
void TypeInfo::formatDebug(QDebug &debug) const
{
    debug << '"';
    formatSequence(debug, d->m_qualifiedName.begin(), d->m_qualifiedName.end(), "\", \"");
    debug << '"';
    if (d->m_constant)
        debug << ", [const]";
    if (d->m_volatile)
        debug << ", [volatile]";
    if (!d->m_indirections.isEmpty()) {
        debug << ", indirections=";
        for (auto i : d->m_indirections)
            debug << ' ' << TypeInfo::indirectionKeyword(i);
    }
    switch (d->m_referenceType) {
    case NoReference:
        break;
    case LValueReference:
        debug << ", [ref]";
        break;
    case RValueReference:
        debug << ", [rvalref]";
        break;
    }
    if (!d->m_instantiations.isEmpty()) {
        debug << ", template<";
        formatSequence(debug, d->m_instantiations.begin(), d->m_instantiations.end());
        debug << '>';
    }
    if (d->m_functionPointer) {
        debug << ", function ptr(";
        formatSequence(debug, d->m_arguments.begin(), d->m_arguments.end());
        debug << ')';
    }
    if (!d->m_arrayElements.isEmpty()) {
        debug << ", array[" << d->m_arrayElements.size() << "][";
        formatSequence(debug, d->m_arrayElements.begin(), d->m_arrayElements.end());
        debug << ']';
    }
}

QDebug operator<<(QDebug d, const TypeInfo &t)
{
    QDebugStateSaver s(d);
    const int verbosity = d.verbosity();
    d.noquote();
    d.nospace();
    d << "TypeInfo(";
    if (verbosity > 2)
        t.formatDebug(d);
    else
        d << t.toString();
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

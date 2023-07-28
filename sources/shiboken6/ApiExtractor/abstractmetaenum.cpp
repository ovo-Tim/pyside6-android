// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abstractmetaenum.h"
#include "abstractmetalang.h"
#include "documentation.h"
#include "enumtypeentry.h"
#include "parser/enumvalue.h"

#include "qtcompat.h"

#include <QtCore/QDebug>

#include <algorithm>

using namespace Qt::StringLiterals;

class AbstractMetaEnumValueData : public QSharedData
{
public:
    QString m_name;
    QString m_stringValue;
    EnumValue m_value;
    Documentation m_doc;
    bool m_deprecated = false;
};

AbstractMetaEnumValue::AbstractMetaEnumValue() :
    d(new AbstractMetaEnumValueData)
{
}

AbstractMetaEnumValue::AbstractMetaEnumValue(const AbstractMetaEnumValue &) = default;
AbstractMetaEnumValue &AbstractMetaEnumValue::operator=(const AbstractMetaEnumValue &) = default;
AbstractMetaEnumValue::AbstractMetaEnumValue(AbstractMetaEnumValue &&) = default;
AbstractMetaEnumValue &AbstractMetaEnumValue::operator=(AbstractMetaEnumValue &&) = default;
AbstractMetaEnumValue::~AbstractMetaEnumValue() = default;

EnumValue AbstractMetaEnumValue::value() const
{
    return d->m_value;
}

void AbstractMetaEnumValue::setValue(EnumValue value)
{
    if (d->m_value != value)
        d->m_value = value;
}

QString AbstractMetaEnumValue::stringValue() const
{
    return d->m_stringValue;
}

void AbstractMetaEnumValue::setStringValue(const QString &v)
{
    if (d->m_stringValue != v)
        d->m_stringValue = v;
}

QString AbstractMetaEnumValue::name() const
{
    return d->m_name;
}

void AbstractMetaEnumValue::setName(const QString &name)
{
    if (d->m_name != name)
        d->m_name = name;
}

bool AbstractMetaEnumValue::isDeprecated() const
{
    return d->m_deprecated;
}

void AbstractMetaEnumValue::setDeprecated(bool deprecated)
{
    if (d->m_deprecated != deprecated)
        d->m_deprecated = deprecated;
}

Documentation AbstractMetaEnumValue::documentation() const
{
    return d->m_doc;
}

void AbstractMetaEnumValue::setDocumentation(const Documentation &doc)
{
    if (d->m_doc != doc)
        d->m_doc = doc;
}

// --------------------- AbstractMetaEnum

class AbstractMetaEnumData : public QSharedData
{
public:
    AbstractMetaEnumData()  : m_deprecated(false),
        m_hasQenumsDeclaration(false), m_signed(true)
    {
    }

    AbstractMetaEnumValueList m_enumValues;

    EnumTypeEntryCPtr m_typeEntry;
    Documentation m_doc;

    EnumKind m_enumKind = CEnum;
    Access m_access = Access::Public;
    uint m_deprecated : 1;
    uint m_hasQenumsDeclaration : 1;
    uint m_signed : 1;
};

AbstractMetaEnum::AbstractMetaEnum() : d(new AbstractMetaEnumData)
{
}

AbstractMetaEnum::AbstractMetaEnum(const AbstractMetaEnum &) = default;
AbstractMetaEnum &AbstractMetaEnum::operator=(const AbstractMetaEnum&) = default;
AbstractMetaEnum::AbstractMetaEnum(AbstractMetaEnum &&) = default;
AbstractMetaEnum &AbstractMetaEnum::operator=(AbstractMetaEnum &&) = default;
AbstractMetaEnum::~AbstractMetaEnum() = default;

const AbstractMetaEnumValueList &AbstractMetaEnum::values() const
{
    return d->m_enumValues;
}

void AbstractMetaEnum::addEnumValue(const AbstractMetaEnumValue &enumValue)
{
    d->m_enumValues << enumValue;
}

std::optional<AbstractMetaEnumValue>
    findMatchingEnumValue(const AbstractMetaEnumValueList &list, QStringView value)
{
    for (const AbstractMetaEnumValue &enumValue : list) {
        if (enumValue.name() == value)
            return enumValue;
    }
    return {};
}

// Find enum values for "enum Enum { e1 }" either for "e1" or "Enum::e1"
std::optional<AbstractMetaEnumValue>
    AbstractMetaEnum::findEnumValue(QStringView value) const
{
    if (isAnonymous())
        return findMatchingEnumValue(d->m_enumValues, value);
    const int sepPos = value.indexOf(u"::");
    if (sepPos == -1)
        return findMatchingEnumValue(d->m_enumValues, value);
    if (name() == value.left(sepPos))
        return findMatchingEnumValue(d->m_enumValues, value.right(value.size() - sepPos - 2));
    return {};
}

QString AbstractMetaEnum::name() const
{
    return d->m_typeEntry->targetLangEntryName();
}

QString AbstractMetaEnum::qualifiedCppName() const
{
    return enclosingClass()
        ? enclosingClass()->qualifiedCppName() + u"::"_s + name()
        : name();
}

Access AbstractMetaEnum::access() const
{
     return d->m_access;
}

void AbstractMetaEnum::setAccess(Access a)
{
    if (a != d->m_access)
        d->m_access = a;
}

bool AbstractMetaEnum::isDeprecated() const
{
    return d->m_deprecated;
}

void AbstractMetaEnum::setDeprecated(bool deprecated)
{
    if (d->m_deprecated != deprecated)
        d->m_deprecated = deprecated;
}

static bool isDeprecatedValue(const AbstractMetaEnumValue &v)
{
    return v.isDeprecated();
};

bool AbstractMetaEnum::hasDeprecatedValues() const
{
    return std::any_of(d->m_enumValues.cbegin(), d->m_enumValues.cend(),
                       isDeprecatedValue);
}

AbstractMetaEnumValueList AbstractMetaEnum::deprecatedValues() const
{
    AbstractMetaEnumValueList result;
    std::copy_if(d->m_enumValues.cbegin(), d->m_enumValues.cend(),
                 std::back_inserter(result), isDeprecatedValue);
    return result;
}

const Documentation &AbstractMetaEnum::documentation() const
{
    return d->m_doc;
}

void AbstractMetaEnum::setDocumentation(const Documentation &doc)
{
    if (d->m_doc != doc)
        d->m_doc = doc;
}

QString AbstractMetaEnum::qualifier() const
{
    return d->m_typeEntry->targetLangQualifier();
}

QString AbstractMetaEnum::package() const
{
    return d->m_typeEntry->targetLangPackage();
}

QString AbstractMetaEnum::fullName() const
{
    return package() + u'.' + qualifier()  + u'.' + name();
}

EnumKind AbstractMetaEnum::enumKind() const
{
    return d->m_enumKind;
}

void AbstractMetaEnum::setEnumKind(EnumKind kind)
{
    if (d->m_enumKind != kind)
        d->m_enumKind = kind;
}

bool AbstractMetaEnum::isAnonymous() const
{
    return d->m_enumKind == AnonymousEnum;
}

bool AbstractMetaEnum::hasQEnumsDeclaration() const
{
    return d->m_hasQenumsDeclaration;
}

void AbstractMetaEnum::setHasQEnumsDeclaration(bool on)
{
    if (d->m_hasQenumsDeclaration != on)
        d->m_hasQenumsDeclaration = on;
}

EnumTypeEntryCPtr AbstractMetaEnum::typeEntry() const
{
    return d->m_typeEntry;
}

void AbstractMetaEnum::setTypeEntry(const EnumTypeEntryCPtr &entry)
{
    if (d->m_typeEntry != entry)
        d->m_typeEntry = entry;
}

bool AbstractMetaEnum::isSigned() const
{
    return d->m_signed;
}

void AbstractMetaEnum::setSigned(bool s)
{
    if (d->m_signed != s)
        d->m_signed = s;
}

#ifndef QT_NO_DEBUG_STREAM

static void formatMetaEnumValue(QDebug &d, const AbstractMetaEnumValue &v)
{
    d << v.name() << '=';
    v.value().formatDebug(d);
    if (v.isDeprecated())
        d << " (deprecated)";
}

QDebug operator<<(QDebug d, const AbstractMetaEnumValue &v)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "AbstractMetaEnumValue(";
    formatMetaEnumValue(d, v);
    d << ')';
    return d;
}

static void formatMetaEnum(QDebug &d, const AbstractMetaEnum &e)
{
    d << e.fullName();
    if (e.isDeprecated())
        d << " (deprecated) ";
    if (!e.isSigned())
        d << " (unsigned) ";
    d << '[';
    const AbstractMetaEnumValueList &values = e.values();
    for (qsizetype i = 0, count = values.size(); i < count; ++i) {
        if (i)
            d << ", ";
        formatMetaEnumValue(d, values.at(i));
    }
    d << ']';
}

QDebug operator<<(QDebug d, const AbstractMetaEnum *ae)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "AbstractMetaEnum(";
    if (ae)
         formatMetaEnum(d, *ae);
    else
        d << '0';
    d << ')';
    return d;
}

QDebug operator<<(QDebug d, const AbstractMetaEnum &ae)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "AbstractMetaEnum(";
    formatMetaEnum(d, ae);
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

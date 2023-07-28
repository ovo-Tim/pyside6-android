// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abstractmetalang.h"
#include "abstractmetalang_helpers.h"
#include "abstractmetaargument.h"
#include "abstractmetaenum.h"
#include "abstractmetafunction.h"
#include "abstractmetatype.h"
#include "abstractmetafield.h"
#include "parser/codemodel.h"
#include "documentation.h"
#include "messages.h"
#include "modifications.h"
#include "propertyspec.h"
#include "reporthandler.h"
#include "sourcelocation.h"
#include "typedatabase.h"
#include "enumtypeentry.h"
#include "namespacetypeentry.h"
#include "usingmember.h"

#include "qtcompat.h"

#include <QtCore/QDebug>

#include <algorithm>

using namespace Qt::StringLiterals;

bool function_sorter(const AbstractMetaFunctionCPtr &a, const AbstractMetaFunctionCPtr &b)
{
    return a->signature() < b->signature();
}

class AbstractMetaClassPrivate
{
public:
    AbstractMetaClassPrivate()
        : m_hasVirtuals(false),
          m_isPolymorphic(false),
          m_hasNonpublic(false),
          m_hasNonPrivateConstructor(false),
          m_hasPrivateConstructor(false),
          m_hasDeletedDefaultConstructor(false),
          m_hasDeletedCopyConstructor(false),
          m_functionsFixed(false),
          m_inheritanceDone(false),
          m_hasPrivateDestructor(false),
          m_hasProtectedDestructor(false),
          m_hasVirtualDestructor(false),
          m_isTypeDef(false),
          m_hasToStringCapability(false),
          m_valueTypeWithCopyConstructorOnly(false),
          m_hasCachedWrapper(false)
    {
    }

    void addFunction(const AbstractMetaFunctionCPtr &function);
    static AbstractMetaFunction *
        createFunction(const QString &name, AbstractMetaFunction::FunctionType t,
                       Access access, const AbstractMetaArgumentList &arguments,
                       const AbstractMetaType &returnType, const AbstractMetaClassPtr &q);
    void addConstructor(AbstractMetaFunction::FunctionType t,
                        Access access,
                        const AbstractMetaArgumentList &arguments,
                        const AbstractMetaClassPtr &q);
    void addUsingConstructors(const AbstractMetaClassPtr &q);
    void sortFunctions();
    void setFunctions(const AbstractMetaFunctionCList &functions,
                      const AbstractMetaClassCPtr &q);
    bool isUsingMember(const AbstractMetaClassCPtr &c, const QString &memberName,
                       Access minimumAccess) const;
    bool hasConstructors() const;
    qsizetype indexOfProperty(const QString &name) const;

    uint m_hasVirtuals : 1;
    uint m_isPolymorphic : 1;
    uint m_hasNonpublic : 1;
    uint m_hasNonPrivateConstructor : 1;
    uint m_hasPrivateConstructor : 1;
    uint m_hasDeletedDefaultConstructor : 1;
    uint m_hasDeletedCopyConstructor : 1;
    uint m_functionsFixed : 1;
    uint m_inheritanceDone : 1; // m_baseClasses has been populated from m_baseClassNames
    uint m_hasPrivateDestructor : 1;
    uint m_hasProtectedDestructor : 1;
    uint m_hasVirtualDestructor : 1;
    uint m_isTypeDef : 1;
    uint m_hasToStringCapability : 1;
    uint m_valueTypeWithCopyConstructorOnly : 1;
    mutable uint m_hasCachedWrapper : 1;

    Documentation m_doc;

    AbstractMetaClassCPtr m_enclosingClass;
    AbstractMetaClassCPtr m_defaultSuperclass;
    AbstractMetaClassCList m_baseClasses; // Real base classes after setting up inheritance
    AbstractMetaTypeList m_baseTemplateInstantiations;
    AbstractMetaClassCPtr m_extendedNamespace;

    AbstractMetaClassCPtr m_templateBaseClass;
    AbstractMetaFunctionCList m_functions;
    AbstractMetaFieldList m_fields;
    AbstractMetaEnumList m_enums;
    QList<QPropertySpec> m_propertySpecs;
    AbstractMetaClassCList m_innerClasses;
    QString m_hashFunction;

    AbstractMetaFunctionCList m_externalConversionOperators;

    QStringList m_baseClassNames;  // Base class names from C++, including rejected
    TypeEntryCList m_templateArgs;
    ComplexTypeEntryPtr m_typeEntry;
    SourceLocation m_sourceLocation;
    UsingMembers m_usingMembers;

    mutable AbstractMetaClass::CppWrapper m_cachedWrapper;
    AbstractMetaClass::Attributes m_attributes;

    bool m_stream = false;
    uint m_toStringCapabilityIndirections = 0;
};

AbstractMetaClass::AbstractMetaClass() : d(new AbstractMetaClassPrivate)
{
}

AbstractMetaClass::~AbstractMetaClass() = default;

AbstractMetaClass::Attributes AbstractMetaClass::attributes() const
{
    return d->m_attributes;
}

void AbstractMetaClass::setAttributes(Attributes attributes)
{
    d->m_attributes = attributes;
}

void AbstractMetaClass::operator+=(AbstractMetaClass::Attribute attribute)
{
    d->m_attributes.setFlag(attribute);
}

void AbstractMetaClass::operator-=(AbstractMetaClass::Attribute attribute)
{
     d->m_attributes.setFlag(attribute, false);
}

bool AbstractMetaClass::isPolymorphic() const
{
    return d->m_isPolymorphic;
}

/*******************************************************************************
 * Returns a list of all the functions with a given name
 */
AbstractMetaFunctionCList AbstractMetaClass::queryFunctionsByName(const QString &name) const
{
    AbstractMetaFunctionCList returned;
    for (const auto &function : d->m_functions) {
        if (function->name() == name)
            returned.append(function);
    }

    return returned;
}

/*******************************************************************************
 * Returns a list of all the functions retrieved during parsing which should
 * be added to the API.
 */
AbstractMetaFunctionCList AbstractMetaClass::functionsInTargetLang() const
{
    FunctionQueryOptions default_flags = FunctionQueryOption::NormalFunctions
        | FunctionQueryOption::Visible | FunctionQueryOption::NotRemoved;

    // Only public functions in final classes
    // default_flags |= isFinal() ? WasPublic : 0;
    FunctionQueryOptions public_flags;
    if (isFinalInTargetLang())
        public_flags |= FunctionQueryOption::WasPublic;

    // Constructors
    AbstractMetaFunctionCList returned = queryFunctions(FunctionQueryOption::AnyConstructor
                                                        | default_flags | public_flags);

    // Final functions
    returned += queryFunctions(FunctionQueryOption::FinalInTargetLangFunctions
                               | FunctionQueryOption::NonStaticFunctions
                               | default_flags | public_flags);

    // Virtual functions
    returned += queryFunctions(FunctionQueryOption::VirtualInTargetLangFunctions
                               | FunctionQueryOption::NonStaticFunctions
                               | default_flags | public_flags);

    // Static functions
    returned += queryFunctions(FunctionQueryOption::StaticFunctions
                               | default_flags | public_flags);

    // Empty, private functions, since they aren't caught by the other ones
    returned += queryFunctions(FunctionQueryOption::Empty | FunctionQueryOption::Invisible);

    return returned;
}

AbstractMetaFunctionCList AbstractMetaClass::implicitConversions() const
{
    if (!isCopyConstructible() && !hasExternalConversionOperators())
        return {};

    AbstractMetaFunctionCList returned;
    const auto list = queryFunctions(FunctionQueryOption::Constructors) + externalConversionOperators();

    // Exclude anything that uses rvalue references, be it a move
    // constructor "QPolygon(QPolygon &&)" or something else like
    // "QPolygon(QVector<QPoint> &&)".
    for (const auto &f : list) {
        if ((f->actualMinimumArgumentCount() == 1 || f->arguments().size() == 1 || f->isConversionOperator())
            && !f->isExplicit()
            && !f->usesRValueReferences()
            && !f->isModifiedRemoved()
            && f->wasPublic()) {
            returned += f;
        }
    }
    return returned;
}

AbstractMetaFunctionCList AbstractMetaClass::operatorOverloads(OperatorQueryOptions query) const
{
    const auto &list = queryFunctions(FunctionQueryOption::OperatorOverloads
                                      | FunctionQueryOption::Visible);
    AbstractMetaFunctionCList returned;
    for (const auto &f : list) {
        if (f->matches(query))
            returned += f;
    }

    return returned;
}

bool AbstractMetaClass::hasArithmeticOperatorOverload() const
{
    for (const auto & f: d->m_functions) {
        if (f->ownerClass() == f->implementingClass() && f->isArithmeticOperator() && !f->isPrivate())
            return true;
    }
    return false;
}

bool AbstractMetaClass::hasIncDecrementOperatorOverload() const
{
    for (const auto & f: d->m_functions) {
        if (f->ownerClass() == f->implementingClass()
            && f->isIncDecrementOperator() && !f->isPrivate()) {
            return true;
        }
    }
    return false;
}

bool AbstractMetaClass::hasBitwiseOperatorOverload() const
{
    for (const auto & f: d->m_functions) {
        if (f->ownerClass() == f->implementingClass() && f->isBitwiseOperator() && !f->isPrivate())
            return true;
    }
    return false;
}

bool AbstractMetaClass::hasComparisonOperatorOverload() const
{
    for (const auto &f : d->m_functions) {
        if (f->ownerClass() == f->implementingClass() && f->isComparisonOperator() && !f->isPrivate())
            return true;
    }
    return false;
}

bool AbstractMetaClass::hasLogicalOperatorOverload() const
{
    for (const auto &f : d->m_functions) {
        if (f->ownerClass() == f->implementingClass() && f->isLogicalOperator() && !f->isPrivate())
            return true;
    }
    return false;
}

const AbstractMetaFieldList &AbstractMetaClass::fields() const
{
    return d->m_fields;
}

AbstractMetaFieldList &AbstractMetaClass::fields()
{
    return d->m_fields;
}

void AbstractMetaClass::setFields(const AbstractMetaFieldList &fields)
{
    d->m_fields = fields;
}

void AbstractMetaClass::addField(const AbstractMetaField &field)
{
    d->m_fields << field;
}

bool AbstractMetaClass::hasStaticFields() const
{
   return std::any_of(d->m_fields.cbegin(), d->m_fields.cend(),
                      [](const AbstractMetaField &f) { return f.isStatic(); });
}

void AbstractMetaClass::sortFunctions()
{
   d->sortFunctions();
}

AbstractMetaClassCPtr AbstractMetaClass::templateBaseClass() const
{
    return d->m_templateBaseClass;
}

void AbstractMetaClass::setTemplateBaseClass(const AbstractMetaClassCPtr &cls)
{
    d->m_templateBaseClass = cls;
}

const AbstractMetaFunctionCList &AbstractMetaClass::functions() const
{
    return d->m_functions;
}

void AbstractMetaClassPrivate::sortFunctions()
{
    std::sort(m_functions.begin(), m_functions.end(), function_sorter);
}

void AbstractMetaClassPrivate::setFunctions(const AbstractMetaFunctionCList &functions,
                                            const AbstractMetaClassCPtr &q)
{
    m_functions = functions;

    // Functions must be sorted by name before next loop
    sortFunctions();

    for (const auto &f : std::as_const(m_functions)) {
        std::const_pointer_cast<AbstractMetaFunction>(f)->setOwnerClass(q);
        if (!f->isPublic())
            m_hasNonpublic = true;
    }
}

const QList<QPropertySpec> &AbstractMetaClass::propertySpecs() const
{
    return d->m_propertySpecs;
}

void AbstractMetaClass::addPropertySpec(const QPropertySpec &spec)
{
    d->m_propertySpecs << spec;
}

void AbstractMetaClass::setPropertyDocumentation(const QString &name, const Documentation &doc)
{
    const auto index = d->indexOfProperty(name);
    if (index >= 0)
        d->m_propertySpecs[index].setDocumentation(doc);
}

void AbstractMetaClassPrivate::addFunction(const AbstractMetaFunctionCPtr &function)
{
    Q_ASSERT(!function->signature().startsWith(u'('));

    if (!function->isDestructor())
        m_functions << function;
    else
        Q_ASSERT(false); //memory leak

    m_hasVirtuals |= function->isVirtual();
    m_isPolymorphic |= m_hasVirtuals;
    m_hasNonpublic |= !function->isPublic();
    m_hasNonPrivateConstructor |= !function->isPrivate()
        && function->functionType() == AbstractMetaFunction::ConstructorFunction;
}

void AbstractMetaClass::addFunction(const AbstractMetaClassPtr &klass,
                                    const AbstractMetaFunctionCPtr &function)
{
    auto nonConstF = std::const_pointer_cast<AbstractMetaFunction>(function);
    nonConstF->setOwnerClass(klass);

    // Set the default value of the declaring class. This may be changed
    // in fixFunctions later on
    nonConstF->setDeclaringClass(klass);

    // Some of the queries below depend on the implementing class being set
    // to function properly. Such as function modifications
    nonConstF->setImplementingClass(klass);

    klass->d->addFunction(function);
}

bool AbstractMetaClass::hasSignal(const AbstractMetaFunction *other) const
{
    if (!other->isSignal())
        return false;

    for (const auto &f : d->m_functions) {
        if (f->isSignal() && f->compareTo(other) & AbstractMetaFunction::EqualName)
            return other->modifiedName() == f->modifiedName();
    }

    return false;
}


QString AbstractMetaClass::name() const
{
    return d->m_typeEntry->targetLangEntryName();
}

const Documentation &AbstractMetaClass::documentation() const
{
    return d->m_doc;
}

void AbstractMetaClass::setDocumentation(const Documentation &doc)
{
    d->m_doc = doc;
}

QString AbstractMetaClass::baseClassName() const
{
    return d->m_baseClasses.isEmpty() ? QString() : d->m_baseClasses.constFirst()->name();
}

// Attribute "default-superclass"
AbstractMetaClassCPtr AbstractMetaClass::defaultSuperclass() const
{
    return d->m_defaultSuperclass;
}

void AbstractMetaClass::setDefaultSuperclass(const AbstractMetaClassPtr &s)
{
    d->m_defaultSuperclass = s;
}

AbstractMetaClassCPtr AbstractMetaClass::baseClass() const
{
    return d->m_baseClasses.value(0, nullptr);
}

const AbstractMetaClassCList &AbstractMetaClass::baseClasses() const
{
    Q_ASSERT(inheritanceDone() || !needsInheritanceSetup());
    return d->m_baseClasses;
}

// base classes including "defaultSuperclass".
AbstractMetaClassCList AbstractMetaClass::typeSystemBaseClasses() const
{
    AbstractMetaClassCList result = d->m_baseClasses;
    if (d->m_defaultSuperclass) {
        result.removeAll(d->m_defaultSuperclass);
        result.prepend(d->m_defaultSuperclass);
    }
    return result;
}

// Recursive list of all base classes including defaultSuperclass
AbstractMetaClassCList AbstractMetaClass::allTypeSystemAncestors() const
{
    AbstractMetaClassCList result;
    const auto baseClasses = typeSystemBaseClasses();
    for (const auto &base : baseClasses) {
        result.append(base);
        result.append(base->allTypeSystemAncestors());
    }
    return result;
}

void AbstractMetaClass::addBaseClass(const AbstractMetaClassCPtr &baseClass)
{
    Q_ASSERT(baseClass);
    d->m_baseClasses.append(baseClass);
    d->m_isPolymorphic |= baseClass->isPolymorphic();
}

void AbstractMetaClass::setBaseClass(const AbstractMetaClassCPtr &baseClass)
{
    if (baseClass) {
        d->m_baseClasses.prepend(baseClass);
        d->m_isPolymorphic |= baseClass->isPolymorphic();
    }
}

AbstractMetaClassCPtr AbstractMetaClass::extendedNamespace() const
{
    return d->m_extendedNamespace;
}

void AbstractMetaClass::setExtendedNamespace(const AbstractMetaClassCPtr &e)
{
    d->m_extendedNamespace = e;
}

const AbstractMetaClassCList &AbstractMetaClass::innerClasses() const
{
    return d->m_innerClasses;
}

void AbstractMetaClass::addInnerClass(const AbstractMetaClassPtr &cl)
{
    d->m_innerClasses << cl;
}

void AbstractMetaClass::setInnerClasses(const AbstractMetaClassCList &innerClasses)
{
    d->m_innerClasses = innerClasses;
}

QString AbstractMetaClass::package() const
{
    return d->m_typeEntry->targetLangPackage();
}

bool AbstractMetaClass::isNamespace() const
{
    return d->m_typeEntry->isNamespace();
}

// Is an invisible namespaces whose functions/enums
// should be mapped to the global space.
bool AbstractMetaClass::isInvisibleNamespace() const
{
    return d->m_typeEntry->isNamespace() && d->m_typeEntry->generateCode()
        && !NamespaceTypeEntry::isVisibleScope(d->m_typeEntry);
}

bool AbstractMetaClass::isInlineNamespace() const
{
    bool result = false;
    if (d->m_typeEntry->isNamespace()) {
        const auto nte = std::static_pointer_cast<const NamespaceTypeEntry>(d->m_typeEntry);
        result = nte->isInlineNamespace();
    }
    return result;
}

bool AbstractMetaClass::isQtNamespace() const
{
    return isNamespace() && name() == u"Qt";
}

QString AbstractMetaClass::qualifiedCppName() const
{
    return d->m_typeEntry->qualifiedCppName();
}

bool AbstractMetaClass::hasFunction(const QString &str) const
{
    return bool(findFunction(str));
}

AbstractMetaFunctionCPtr AbstractMetaClass::findFunction(QStringView functionName) const
{
    return AbstractMetaFunction::find(d->m_functions, functionName);
}

AbstractMetaFunctionCList AbstractMetaClass::findFunctions(QStringView functionName) const
{
    AbstractMetaFunctionCList result;
    std::copy_if(d->m_functions.cbegin(), d->m_functions.cend(),
                 std::back_inserter(result),
                 [&functionName](const AbstractMetaFunctionCPtr &f) {
                     return f->name() == functionName;
                 });
    return result;
}

AbstractMetaFunctionCPtr AbstractMetaClass::findOperatorBool() const
{
    auto it = std::find_if(d->m_functions.cbegin(), d->m_functions.cend(),
                           [](const AbstractMetaFunctionCPtr &f) {
                               return f->isOperatorBool();
                           });
    if (it == d->m_functions.cend())
        return {};
    return *it;
}

AbstractMetaFunctionCPtr AbstractMetaClass::findQtIsNullMethod() const
{
    auto it = std::find_if(d->m_functions.cbegin(), d->m_functions.cend(),
                           [](const AbstractMetaFunctionCPtr &f) {
                               return f->isQtIsNullMethod();
                           });
    if (it == d->m_functions.cend())
        return {};
    return *it;
}

bool AbstractMetaClass::hasProtectedFields() const
{
    for (const AbstractMetaField &field : d->m_fields) {
        if (field.isProtected())
            return true;
    }
    return false;
}

const TypeEntryCList &AbstractMetaClass::templateArguments() const
{
    return d->m_templateArgs;
}

void AbstractMetaClass::setTemplateArguments(const TypeEntryCList &args)
{
    d->m_templateArgs = args;
}

const QStringList &AbstractMetaClass::baseClassNames() const
{
    return d->m_baseClassNames;
}

void AbstractMetaClass::setBaseClassNames(const QStringList &names)
{
    d->m_baseClassNames = names;
}

ComplexTypeEntryCPtr AbstractMetaClass::typeEntry() const
{
    return d->m_typeEntry;
}

ComplexTypeEntryPtr AbstractMetaClass::typeEntry()
{
    return d->m_typeEntry;
}

void AbstractMetaClass::setTypeEntry(const ComplexTypeEntryPtr &type)
{
    d->m_typeEntry = type;
}

QString AbstractMetaClass::hashFunction() const
{
    return d->m_hashFunction;
}

void AbstractMetaClass::setHashFunction(const QString &f)
{
    d->m_hashFunction = f;
}

bool AbstractMetaClass::hasHashFunction() const
{
    return !d->m_hashFunction.isEmpty();
}

// Search whether a functions is a property setter/getter/reset
AbstractMetaClass::PropertyFunctionSearchResult
    AbstractMetaClass::searchPropertyFunction(const QString &name) const
{
    for (qsizetype i = 0, size = d->m_propertySpecs.size(); i < size; ++i) {
        const auto &propertySpec = d->m_propertySpecs.at(i);
        if (name == propertySpec.read())
            return PropertyFunctionSearchResult{i, PropertyFunction::Read};
        if (name == propertySpec.write())
            return PropertyFunctionSearchResult{i, PropertyFunction::Write};
        if (name == propertySpec.reset())
            return PropertyFunctionSearchResult{i, PropertyFunction::Reset};
        if (name == propertySpec.notify())
            return PropertyFunctionSearchResult{i, PropertyFunction::Notify};
    }
    return PropertyFunctionSearchResult{-1, PropertyFunction::Read};
}

std::optional<QPropertySpec>
    AbstractMetaClass::propertySpecByName(const QString &name) const
{
    const auto index = d->indexOfProperty(name);
    if (index >= 0)
        return d->m_propertySpecs.at(index);
    return {};
}

const AbstractMetaFunctionCList &AbstractMetaClass::externalConversionOperators() const
{
    return d->m_externalConversionOperators;
}

void AbstractMetaClass::addExternalConversionOperator(const AbstractMetaFunctionCPtr &conversionOp)
{
    if (!d->m_externalConversionOperators.contains(conversionOp))
        d->m_externalConversionOperators.append(conversionOp);
}

bool AbstractMetaClass::hasExternalConversionOperators() const
{
    return !d->m_externalConversionOperators.isEmpty();
}

bool AbstractMetaClass::hasTemplateBaseClassInstantiations() const
{
    return d->m_templateBaseClass != nullptr && !d->m_baseTemplateInstantiations.isEmpty();
}

const AbstractMetaTypeList &AbstractMetaClass::templateBaseClassInstantiations() const
{
    return d->m_baseTemplateInstantiations;
}

void AbstractMetaClass::setTemplateBaseClassInstantiations(const AbstractMetaTypeList &instantiations)
{
    Q_ASSERT(d->m_templateBaseClass != nullptr);
    d->m_baseTemplateInstantiations = instantiations;
}

void AbstractMetaClass::setTypeDef(bool typeDef)
{
    d->m_isTypeDef = typeDef;
}

bool AbstractMetaClass::isTypeDef() const
{
    return d->m_isTypeDef;
}

bool AbstractMetaClass::isStream() const
{
    return d->m_stream;
}

void AbstractMetaClass::setStream(bool stream)
{
    d->m_stream = stream;
}

bool AbstractMetaClass::hasToStringCapability() const
{
    return d->m_hasToStringCapability;
}

void AbstractMetaClass::setToStringCapability(bool value, uint indirections)
{
    d->m_hasToStringCapability = value;
    d->m_toStringCapabilityIndirections = indirections;
}

uint AbstractMetaClass::toStringCapabilityIndirections() const
{
    return d->m_toStringCapabilityIndirections;
}

// Does any of the base classes require deletion in the main thread?
bool AbstractMetaClass::deleteInMainThread() const
{
    return typeEntry()->deleteInMainThread()
            || (!d->m_baseClasses.isEmpty() && d->m_baseClasses.constFirst()->deleteInMainThread());
}

bool AbstractMetaClassPrivate::hasConstructors() const
{
    return AbstractMetaClass::queryFirstFunction(m_functions,
                                                 FunctionQueryOption::AnyConstructor) != nullptr;
}

qsizetype AbstractMetaClassPrivate::indexOfProperty(const QString &name) const
{
    for (qsizetype i = 0; i < m_propertySpecs.size(); ++i) {
        if (m_propertySpecs.at(i).name() == name)
            return i;
    }
    return -1;
}

bool AbstractMetaClass::hasConstructors() const
{
    return d->hasConstructors();
}

AbstractMetaFunctionCPtr AbstractMetaClass::copyConstructor() const
{
    for (const auto &f : d->m_functions) {
        if (f->functionType() == AbstractMetaFunction::CopyConstructorFunction)
            return f;
    }
    return {};
}

bool AbstractMetaClass::hasCopyConstructor() const
{
    return copyConstructor() != nullptr;
}

bool AbstractMetaClass::hasPrivateCopyConstructor() const
{
    const auto copyCt = copyConstructor();
    return copyCt && copyCt->isPrivate();
}

void AbstractMetaClassPrivate::addConstructor(AbstractMetaFunction::FunctionType t,
                                              Access access,
                                              const AbstractMetaArgumentList &arguments,
                                              const AbstractMetaClassPtr &q)
{
    auto *f = createFunction(q->name(), t, access, arguments, AbstractMetaType::createVoid(), q);
    if (access != Access::Private)
         m_hasNonPrivateConstructor = true;
    f->setAttributes(AbstractMetaFunction::FinalInTargetLang
                     | AbstractMetaFunction::AddedMethod);
    addFunction(AbstractMetaFunctionCPtr(f));
}

void AbstractMetaClass::addDefaultConstructor(const AbstractMetaClassPtr &klass)
{
    klass->d->addConstructor(AbstractMetaFunction::ConstructorFunction,
                             Access::Public, {}, klass);
}

void AbstractMetaClass::addDefaultCopyConstructor(const AbstractMetaClassPtr &klass)
{
    AbstractMetaType argType(klass->typeEntry());
    argType.setReferenceType(LValueReference);
    argType.setConstant(true);
    argType.setTypeUsagePattern(AbstractMetaType::ValuePattern);

    AbstractMetaArgument arg;
    arg.setType(argType);
    arg.setName(klass->name());

    klass->d->addConstructor(AbstractMetaFunction::CopyConstructorFunction,
                             Access::Public, {arg}, klass);
}

AbstractMetaFunction *
    AbstractMetaClassPrivate::createFunction(const QString &name,
                                            AbstractMetaFunction::FunctionType t,
                                            Access access,
                                            const AbstractMetaArgumentList &arguments,
                                            const AbstractMetaType &returnType,
                                            const AbstractMetaClassPtr &q)
{
    auto *f = new AbstractMetaFunction(name);
    f->setType(returnType);
    f->setOwnerClass(q);
    f->setFunctionType(t);
    f->setArguments(arguments);
    f->setDeclaringClass(q);
    f->setAccess(access);
    f->setImplementingClass(q);
    return f;
}

static AbstractMetaType boolType()
{
    auto boolType = TypeDatabase::instance()->findType(u"bool"_s);
    Q_ASSERT(boolType);
    AbstractMetaType result(boolType);
    result.decideUsagePattern();
    return result;
}

// Helper to synthesize comparison operators from a spaceship operator. Since
// shiboken also generates code for comparing to different types, this fits
// better than of handling it in the generator code.
void AbstractMetaClass::addSynthesizedComparisonOperators(const AbstractMetaClassPtr &c)
{
    static const auto returnType = boolType();

    AbstractMetaType selfType(c->typeEntry());
    selfType.setConstant(true);
    selfType.setReferenceType(LValueReference);
    selfType.decideUsagePattern();
    AbstractMetaArgument selfArgument;
    selfArgument.setType(selfType);
    selfArgument.setName(u"rhs"_s);
    AbstractMetaArgumentList arguments(1, selfArgument);

    static const char *operators[]
        = {"operator==", "operator!=", "operator<", "operator<=", "operator>", "operator>="};
    for (auto *op : operators) {
        auto *f = AbstractMetaClassPrivate::createFunction(QLatin1StringView(op),
                                                           AbstractMetaFunction::ComparisonOperator,
                                                           Access::Public, arguments,
                                                           returnType, c);
         c->d->addFunction(AbstractMetaFunctionCPtr(f));
    }
}

bool AbstractMetaClass::hasNonPrivateConstructor() const
{
    return d->m_hasNonPrivateConstructor;
}

void AbstractMetaClass::setHasNonPrivateConstructor(bool value)
{
    d->m_hasNonPrivateConstructor = value;
}

bool AbstractMetaClass::hasPrivateConstructor() const
{
    return d->m_hasPrivateConstructor;
}

void AbstractMetaClass::setHasPrivateConstructor(bool value)
{
    d->m_hasPrivateConstructor = value;
}

bool AbstractMetaClass::hasDeletedDefaultConstructor() const
{
    return d->m_hasDeletedDefaultConstructor;
}

void AbstractMetaClass::setHasDeletedDefaultConstructor(bool value)
{
    d->m_hasDeletedDefaultConstructor = value;
}

bool AbstractMetaClass::hasDeletedCopyConstructor() const
{
    return d->m_hasDeletedCopyConstructor;
}

void AbstractMetaClass::setHasDeletedCopyConstructor(bool value)
{
    d->m_hasDeletedCopyConstructor = value;
}

bool AbstractMetaClass::hasPrivateDestructor() const
{
    return d->m_hasPrivateDestructor;
}

void AbstractMetaClass::setHasPrivateDestructor(bool value)
{
    d->m_hasPrivateDestructor = value;
}

bool AbstractMetaClass::hasProtectedDestructor() const
{
    return d->m_hasProtectedDestructor;
}

void AbstractMetaClass::setHasProtectedDestructor(bool value)
{
    d->m_hasProtectedDestructor = value;
}

bool AbstractMetaClass::hasVirtualDestructor() const
{
    return d->m_hasVirtualDestructor;
}

void AbstractMetaClass::setHasVirtualDestructor(bool value)
{
    d->m_hasVirtualDestructor = value;
    if (value)
        d->m_hasVirtuals = d->m_isPolymorphic = 1;
}

bool AbstractMetaClass::isDefaultConstructible() const
{
    // Private constructors are skipped by the builder.
    if (hasDeletedDefaultConstructor() || hasPrivateConstructor())
        return false;
    const AbstractMetaFunctionCList ctors =
        queryFunctions(FunctionQueryOption::Constructors);
    for (const auto &ct : ctors) {
        if (ct->isDefaultConstructor())
            return ct->isPublic();
    }
    return ctors.isEmpty() && isImplicitlyDefaultConstructible();
}

// Non-comprehensive check for default constructible field
// (non-ref or not const value).
static bool defaultConstructibleField(const AbstractMetaField &f)
{
    if (f.isStatic())
        return true;
    const auto &type = f.type();
    return type.referenceType() == NoReference
        && !(type.indirections() == 0 && type.isConstant()); // no const values
}

bool AbstractMetaClass::isImplicitlyDefaultConstructible() const
{
    return std::all_of(d->m_fields.cbegin(), d->m_fields.cend(),
                        defaultConstructibleField)
        && std::all_of(d->m_baseClasses.cbegin(), d->m_baseClasses.cend(),
                       [] (const AbstractMetaClassCPtr &c) {
                           return c->isDefaultConstructible();
                       });
}

static bool canAddDefaultConstructorHelper(const AbstractMetaClass *cls)
{
    return !cls->isNamespace()
        && !cls->hasDeletedDefaultConstructor()
        && !cls->attributes().testFlag(AbstractMetaClass::HasRejectedConstructor)
        && !cls->hasPrivateDestructor();
}

bool AbstractMetaClass::canAddDefaultConstructor() const
{
    return canAddDefaultConstructorHelper(this) && !hasConstructors()
        && !hasPrivateConstructor() && isImplicitlyDefaultConstructible();
}

bool AbstractMetaClass::isCopyConstructible() const
{
    // Private constructors are skipped by the builder.
    if (hasDeletedCopyConstructor() || hasPrivateCopyConstructor())
        return false;
    const AbstractMetaFunctionCList copyCtors =
        queryFunctions(FunctionQueryOption::CopyConstructor);
    return copyCtors.isEmpty()
        ? isImplicitlyCopyConstructible()
        : copyCtors.constFirst()->isPublic();
}

bool AbstractMetaClass::isImplicitlyCopyConstructible() const
{
    // Fields are currently not considered
    return std::all_of(d->m_baseClasses.cbegin(), d->m_baseClasses.cend(),
                       [] (const AbstractMetaClassCPtr &c) {
                           return c->isCopyConstructible();
                       });
}

bool AbstractMetaClass::canAddDefaultCopyConstructor() const
{
    if (!canAddDefaultConstructorHelper(this)
        || !d->m_typeEntry->isValue() || isAbstract()
        || hasPrivateCopyConstructor() || hasCopyConstructor()) {
        return false;
    }
    return isImplicitlyCopyConstructible();
}

static bool classHasParentManagement(const AbstractMetaClassCPtr &c)
{
    const auto flags = c->typeEntry()->typeFlags();
    return flags.testFlag(ComplexTypeEntry::ParentManagement);
}

TypeEntryCPtr parentManagementEntry(const AbstractMetaClassCPtr &klass)
{
    if (klass->typeEntry()->isObject()) {
        if (auto c = recurseClassHierarchy(klass, classHasParentManagement))
            return c->typeEntry();
    }
    return nullptr;
}

bool AbstractMetaClass::generateExceptionHandling() const
{
    return queryFirstFunction(d->m_functions, FunctionQueryOption::Visible
                              | FunctionQueryOption::GenerateExceptionHandling) != nullptr;
}

static bool needsProtectedWrapper(const AbstractMetaFunctionCPtr &func)
{
    return func->isProtected()
        && !(func->isSignal() || func->isModifiedRemoved())
        && !func->isOperatorOverload();
}

static AbstractMetaClass::CppWrapper determineCppWrapper(const AbstractMetaClass *metaClass)
{

    AbstractMetaClass::CppWrapper result;

    if (metaClass->isNamespace()
        || metaClass->attributes().testFlag(AbstractMetaClass::FinalCppClass)
        || metaClass->typeEntry()->typeFlags().testFlag(ComplexTypeEntry::DisableWrapper)) {
        return result;
    }

#ifndef Q_CC_MSVC
    // PYSIDE-504: When C++ 11 is used, then the destructor must always be
    // declared. Only MSVC can handle this, the others generate a link error.
    // See also HeaderGenerator::generateClass().
    if (metaClass->hasPrivateDestructor())
        return result;
#endif

    // Need checking for Python overrides?
    if (metaClass->isPolymorphic())
        result |= AbstractMetaClass::CppVirtualMethodWrapper;

    // Is there anything protected that needs to be made accessible?
    if (metaClass->hasProtectedFields() || metaClass->hasProtectedDestructor()
        || std::any_of(metaClass->functions().cbegin(), metaClass->functions().cend(),
                       needsProtectedWrapper)) {
        result |= AbstractMetaClass::CppProtectedHackWrapper;
    }
    return result;
}

AbstractMetaClass::CppWrapper AbstractMetaClass::cppWrapper() const
{
    if (!d->m_hasCachedWrapper) {
        d->m_cachedWrapper = determineCppWrapper(this);
        d->m_hasCachedWrapper = true;
    }
    return d->m_cachedWrapper;
}

const UsingMembers &AbstractMetaClass::usingMembers() const
{
    return d->m_usingMembers;
}

void AbstractMetaClass::addUsingMember(const UsingMember &um)
{
    d->m_usingMembers.append(um);
}

bool AbstractMetaClassPrivate::isUsingMember(const AbstractMetaClassCPtr &c,
                                             const QString &memberName,
                                             Access minimumAccess) const
{
    auto it = std::find_if(m_usingMembers.cbegin(), m_usingMembers.cend(),
                           [c, &memberName](const UsingMember &um) {
                               return um.baseClass == c && um.memberName == memberName;
                           });
    return it != m_usingMembers.cend() && it->access >= minimumAccess;
}

bool AbstractMetaClass::isUsingMember(const AbstractMetaClassCPtr &c,
                                      const QString &memberName,
                                      Access minimumAccess) const
{
    return d->isUsingMember(c, memberName, minimumAccess);
}

bool AbstractMetaClass::hasUsingMemberFor(const QString &memberName) const
{
    return std::any_of(d->m_usingMembers.cbegin(), d->m_usingMembers.cend(),
                       [&memberName](const UsingMember &um) {
                          return um.memberName == memberName;
                       });
}

/* Goes through the list of functions and returns a list of all
   functions matching all of the criteria in \a query.
 */

bool AbstractMetaClass::queryFunction(const AbstractMetaFunction *f, FunctionQueryOptions query)
{
    if ((query.testFlag(FunctionQueryOption::NotRemoved))) {
        if (f->isModifiedRemoved())
            return false;
        if (f->isVirtual() && f->isModifiedRemoved(f->declaringClass()))
            return false;
    }

    if (query.testFlag(FunctionQueryOption::Visible) && f->isPrivate())
        return false;

    if (query.testFlag(FunctionQueryOption::VirtualInTargetLangFunctions) && f->isFinalInTargetLang())
        return false;

    if (query.testFlag(FunctionQueryOption::Invisible) && !f->isPrivate())
        return false;

    if (query.testFlag(FunctionQueryOption::Empty) && !f->isEmptyFunction())
        return false;

    if (query.testFlag(FunctionQueryOption::WasPublic) && !f->wasPublic())
        return false;

    if (query.testFlag(FunctionQueryOption::ClassImplements) && f->ownerClass() != f->implementingClass())
        return false;

    if (query.testFlag(FunctionQueryOption::FinalInTargetLangFunctions) && !f->isFinalInTargetLang())
        return false;

    if (query.testFlag(FunctionQueryOption::VirtualInCppFunctions) && !f->isVirtual())
        return false;

    if (query.testFlag(FunctionQueryOption::Signals) && (!f->isSignal()))
        return false;

    if (query.testFlag(FunctionQueryOption::AnyConstructor)
         && (!f->isConstructor() || f->ownerClass() != f->implementingClass())) {
        return false;
    }

    if (query.testFlag(FunctionQueryOption::Constructors)
         && (f->functionType() != AbstractMetaFunction::ConstructorFunction
             || f->ownerClass() != f->implementingClass())) {
        return false;
    }

    if (query.testFlag(FunctionQueryOption::CopyConstructor)
        && (!f->isCopyConstructor() || f->ownerClass() != f->implementingClass())) {
        return false;
    }

    // Destructors are never included in the functions of a class currently
    /*
           if ((query & Destructors) && (!f->isDestructor()
                                       || f->ownerClass() != f->implementingClass())
            || f->isDestructor() && (query & Destructors) == 0) {
            return false;
        }*/

    if (query.testFlag(FunctionQueryOption::StaticFunctions) && (!f->isStatic() || f->isSignal()))
        return false;

    if (query.testFlag(FunctionQueryOption::NonStaticFunctions) && (f->isStatic()))
        return false;

    if (query.testFlag(FunctionQueryOption::NormalFunctions) && (f->isSignal()))
        return false;

    if (query.testFlag(FunctionQueryOption::OperatorOverloads) && !f->isOperatorOverload())
        return false;

    if (query.testFlag(FunctionQueryOption::GenerateExceptionHandling) && !f->generateExceptionHandling())
        return false;

    if (query.testFlag(FunctionQueryOption::GetAttroFunction)
        && f->functionType() != AbstractMetaFunction::GetAttroFunction) {
        return false;
    }

    if (query.testFlag(FunctionQueryOption::SetAttroFunction)
        && f->functionType() != AbstractMetaFunction::SetAttroFunction) {
        return false;
    }

    return true;
}

AbstractMetaFunctionCList AbstractMetaClass::queryFunctionList(const AbstractMetaFunctionCList &list,
                                                              FunctionQueryOptions query)
{
    AbstractMetaFunctionCList result;
    for (const auto &f : list) {
        if (queryFunction(f.get(), query))
            result.append(f);
    }
    return result;
}

AbstractMetaFunctionCPtr AbstractMetaClass::queryFirstFunction(const AbstractMetaFunctionCList &list,
                                                               FunctionQueryOptions query)
{
    for (const auto &f : list) {
        if (queryFunction(f.get(), query))
            return f;
    }
    return {};
}

AbstractMetaFunctionCList AbstractMetaClass::queryFunctions(FunctionQueryOptions query) const
{
    return AbstractMetaClass::queryFunctionList(d->m_functions, query);
}

bool AbstractMetaClass::hasSignals() const
{
    return queryFirstFunction(d->m_functions,
                              FunctionQueryOption::Signals
                              | FunctionQueryOption::Visible
                              | FunctionQueryOption::NotRemoved) != nullptr;
}

AbstractMetaFunctionCList AbstractMetaClass::cppSignalFunctions() const
{
    return queryFunctions(FunctionQueryOption::Signals
                          | FunctionQueryOption::Visible
                          | FunctionQueryOption::NotRemoved);
}

std::optional<AbstractMetaField>
    AbstractMetaClass::findField(QStringView name) const
{
    return AbstractMetaField::find(d->m_fields, name);
}

const AbstractMetaEnumList &AbstractMetaClass::enums() const
{
    return d->m_enums;
}

AbstractMetaEnumList &AbstractMetaClass::enums()
{
    return d->m_enums;
}

void AbstractMetaClass::setEnums(const AbstractMetaEnumList &enums)
{
    d->m_enums = enums;
}

void AbstractMetaClass::addEnum(const AbstractMetaEnum &e)
{
    d->m_enums << e;
}

std::optional<AbstractMetaEnum>
    AbstractMetaClass::findEnum(const QString &enumName) const
{
    for (const auto &e : d->m_enums) {
        if (e.name() == enumName)
            return e;
    }
    return {};
}

/*!  Recursively searches for the enum value named \a enumValueName in
  this class and its superclasses and interfaces.
*/
std::optional<AbstractMetaEnumValue>
    AbstractMetaClass::findEnumValue(const QString &enumValueName) const
{
    for (const AbstractMetaEnum &e : std::as_const(d->m_enums)) {
        auto v = e.findEnumValue(enumValueName);
        if (v.has_value())
            return v;
    }
    if (baseClass())
        return baseClass()->findEnumValue(enumValueName);

    return {};
}

void AbstractMetaClass::getEnumsToBeGenerated(AbstractMetaEnumList *enumList) const
{
    for (const AbstractMetaEnum &metaEnum : d->m_enums) {
        if (!metaEnum.isPrivate() && metaEnum.typeEntry()->generateCode())
            enumList->append(metaEnum);
    }
}

void AbstractMetaClass::getEnumsFromInvisibleNamespacesToBeGenerated(AbstractMetaEnumList *enumList) const
{
    if (isNamespace()) {
        invisibleNamespaceRecursion([enumList](const AbstractMetaClassCPtr &c) {
            c->getEnumsToBeGenerated(enumList);
        });
    }
}

void AbstractMetaClass::getFunctionsFromInvisibleNamespacesToBeGenerated(AbstractMetaFunctionCList *funcList) const
{
    if (isNamespace()) {
        invisibleNamespaceRecursion([funcList](const AbstractMetaClassCPtr &c) {
            funcList->append(c->functions());
        });
    }
}

QString AbstractMetaClass::fullName() const
{
    return package() + u'.' + d->m_typeEntry->targetLangName();
}

static void addExtraIncludeForType(const AbstractMetaClassPtr &metaClass,
                                   const AbstractMetaType &type)
{

    Q_ASSERT(metaClass);
    const auto entry = type.typeEntry();

    if (entry && entry->include().isValid()) {
        const auto class_entry = metaClass->typeEntry();
        class_entry->addArgumentInclude(entry->include());
    }

    if (type.hasInstantiations()) {
        for (const AbstractMetaType &instantiation : type.instantiations())
            addExtraIncludeForType(metaClass, instantiation);
    }
}

static void addExtraIncludesForFunction(const AbstractMetaClassPtr &metaClass,
                                        const AbstractMetaFunctionCPtr &meta_function)
{
    Q_ASSERT(metaClass);
    Q_ASSERT(meta_function);
    addExtraIncludeForType(metaClass, meta_function->type());

    const AbstractMetaArgumentList &arguments = meta_function->arguments();
    for (const AbstractMetaArgument &argument : arguments) {
        const auto &type = argument.type();
        addExtraIncludeForType(metaClass, type);
        if (argument.modifiedType() != type)
            addExtraIncludeForType(metaClass, argument.modifiedType());
    }
}

static bool addSuperFunction(const AbstractMetaFunctionCPtr &f)
{
    switch (f->functionType()) {
    case AbstractMetaFunction::ConstructorFunction:
    case AbstractMetaFunction::CopyConstructorFunction:
    case AbstractMetaFunction::MoveConstructorFunction:
    case AbstractMetaFunction::AssignmentOperatorFunction:
    case AbstractMetaFunction::MoveAssignmentOperatorFunction:
    case AbstractMetaFunction::DestructorFunction:
        return false;
    default:
        break;
    }
    return true;
}

// Add constructors imported via "using" from the base classes. This is not
// needed for normal hidden inherited member functions since we generate a
// cast to the base class to call them into binding code.
void AbstractMetaClassPrivate::addUsingConstructors(const AbstractMetaClassPtr &q)
{
    // Restricted to the non-constructor case currently to avoid
    // having to compare the parameter lists of existing constructors.
    if (m_baseClasses.isEmpty() || m_usingMembers.isEmpty()
        || hasConstructors()) {
        return;
    }

    for (const auto &superClass : m_baseClasses) {
        // Find any "using base-constructor" directives
        if (isUsingMember(superClass, superClass->name(), Access::Protected)) {
            // Add to derived class with parameter lists.
            const auto ctors = superClass->queryFunctions(FunctionQueryOption::Constructors);
            for (const auto &ctor : ctors) {
                if (!ctor->isPrivate()) {
                    addConstructor(AbstractMetaFunction::ConstructorFunction,
                                   ctor->access(), ctor->arguments(), q);
                }
            }
        }
    }
}

void AbstractMetaClass::fixFunctions(const AbstractMetaClassPtr &klass)
{
    auto *d = klass->d.data();
    if (d->m_functionsFixed)
        return;

    d->m_functionsFixed = true;

    AbstractMetaFunctionCList funcs = klass->functions();
    AbstractMetaFunctionCList nonRemovedFuncs;
    nonRemovedFuncs.reserve(funcs.size());

    d->addUsingConstructors(klass);

    for (const auto &f : std::as_const(funcs)) {
        // Fishy: Setting up of implementing/declaring/base classes changes
        // the applicable modifications; clear cached ones.
        std::const_pointer_cast<AbstractMetaFunction>(f)->clearModificationsCache();
        if (!f->isModifiedRemoved())
            nonRemovedFuncs.append(f);
    }

    for (const auto &superClassC : d->m_baseClasses) {
        auto superClass = std::const_pointer_cast<AbstractMetaClass>(superClassC);
        AbstractMetaClass::fixFunctions(superClass);
        // Since we always traverse the complete hierarchy we are only
        // interrested in what each super class implements, not what
        // we may have propagated from their base classes again.
        AbstractMetaFunctionCList superFuncs;
        // Super classes can never be final
        if (superClass->isFinalInTargetLang()) {
            qCWarning(lcShiboken).noquote().nospace()
                << "Final class '" << superClass->name() << "' set to non-final, as it is extended by other classes";
            *superClass -= AbstractMetaClass::FinalInTargetLang;
        }
        superFuncs = superClass->queryFunctions(FunctionQueryOption::ClassImplements);
        const auto virtuals = superClass->queryFunctions(FunctionQueryOption::VirtualInCppFunctions);
        superFuncs += virtuals;

        QSet<AbstractMetaFunctionCPtr> funcsToAdd;
        for (const auto &sf : std::as_const(superFuncs)) {
            if (sf->isModifiedRemoved())
                continue;

            // skip functions added in base classes
            if (sf->isUserAdded() && sf->declaringClass() != klass)
                continue;

            // Skip base class comparison operators declared as members (free
            // operators are added later by traverseOperatorFunction().
            if (sf->isComparisonOperator())
                continue;

            // we generally don't care about private functions, but we have to get the ones that are
            // virtual in case they override abstract functions.
            bool add = addSuperFunction(sf);
            for (const auto &cf : std::as_const(nonRemovedFuncs)) {
                AbstractMetaFunctionPtr f(std::const_pointer_cast<AbstractMetaFunction>(cf));
                const AbstractMetaFunction::CompareResult cmp = cf->compareTo(sf.get());

                if (cmp & AbstractMetaFunction::EqualModifiedName) {
                    add = false;
                    if (cmp & AbstractMetaFunction::EqualArguments) {
                        // Set "override" in case it was not spelled out (since it
                        // is then not detected by clang parsing).
                        const auto attributes = cf->attributes();
                        if (cf->isVirtual()
                            && !attributes.testFlag(AbstractMetaFunction::OverriddenCppMethod)
                            && !attributes.testFlag(AbstractMetaFunction::FinalCppMethod)) {
                            *f += AbstractMetaFunction::OverriddenCppMethod;
                        }
                        // Same function, propegate virtual...
                        if (!(cmp & AbstractMetaFunction::EqualAttributes)) {
                            if (!f->isEmptyFunction()) {
                                if (!sf->isFinalInTargetLang() && f->isFinalInTargetLang()) {
                                    *f -= AbstractMetaFunction::FinalInTargetLang;
                                }
#if 0
                                if (!f->isFinalInTargetLang() && f->isPrivate()) {
                                    f->setFunctionType(AbstractMetaFunction::EmptyFunction);
                                    f->setVisibility(AbstractMetaAttributes::Protected);
                                    *f += AbstractMetaAttributes::FinalInTargetLang;
                                    qCWarning(lcShiboken).noquote().nospace()
                                        << QStringLiteral("private virtual function '%1' in '%2'")
                                                          .arg(f->signature(), f->implementingClass()->name());
                                }
#endif
                            }
                        }

                        if (f->access() != sf->access()) {
                            qCWarning(lcShiboken, "%s",
                                      qPrintable(msgFunctionVisibilityModified(klass, f.get())));
#if 0
                            // If new visibility is private, we can't
                            // do anything. If it isn't, then we
                            // prefer the parent class's visibility
                            // setting for the function.
                            if (!f->isPrivate() && !sf->isPrivate())
                                f->setVisibility(sf->visibility());
#endif
                            // Private overrides of abstract functions have to go into the class or
                            // the subclasses will not compile as non-abstract classes.
                            // But they don't need to be implemented, since they can never be called.
                            if (f->isPrivate()) {
                                f->setFunctionType(AbstractMetaFunction::EmptyFunction);
                                *f += AbstractMetaFunction::FinalInTargetLang;
                            }
                        }

                        // Set the class which first declares this function, afawk
                        f->setDeclaringClass(sf->declaringClass());

                        if (sf->isFinalInTargetLang() && !sf->isPrivate() && !f->isPrivate() && !sf->isStatic() && !f->isStatic()) {
                            // Shadowed funcion, need to make base class
                            // function non-virtual
                            if (f->implementingClass() != sf->implementingClass()
                                && inheritsFrom(f->implementingClass(), sf->implementingClass())) {

                                // Check whether the superclass method has been redefined to non-final

                                bool hasNonFinalModifier = false;
                                bool isBaseImplPrivate = false;
                                const FunctionModificationList &mods = sf->modifications(sf->implementingClass());
                                for (const FunctionModification &mod : mods) {
                                    if (mod.isNonFinal()) {
                                        hasNonFinalModifier = true;
                                        break;
                                    }
                                    if (mod.isPrivate()) {
                                        isBaseImplPrivate = true;
                                        break;
                                    }
                                }

                                if (!hasNonFinalModifier && !isBaseImplPrivate) {
                                    qCWarning(lcShiboken, "%s",
                                              qPrintable(msgShadowingFunction(sf.get(), f.get())));
                                }
                            }
                        }

                    }

                    if (cmp & AbstractMetaFunction::EqualDefaultValueOverload) {
                        AbstractMetaArgumentList arguments;
                        if (f->arguments().size() < sf->arguments().size())
                            arguments = sf->arguments();
                        else
                            arguments = f->arguments();
                        //TODO: fix this
                        //for (int i=0; i<arguments.size(); ++i)
                        //    arguments[i]->setDefaultValueExpression("<#>" + QString());
                    }


                    // Otherwise we have function shadowing and we can
                    // skip the thing...
                } else if (cmp & AbstractMetaFunction::EqualName && !sf->isSignal()) {
                    // In the case of function shadowing where the function name has been altered to
                    // avoid conflict, we don't copy in the original.
                    add = false;
                }
            }

            if (add)
                funcsToAdd << sf;
        }

        for (const auto &f : std::as_const(funcsToAdd)) {
            AbstractMetaFunction *copy = f->copy();
            (*copy) += AbstractMetaFunction::AddedMethod;
            funcs.append(AbstractMetaFunctionCPtr(copy));
        }
    }

    bool hasPrivateConstructors = false;
    bool hasPublicConstructors = false;
    // Apply modifications after the declaring class has been set
    for (const auto &func : std::as_const(funcs)) {
        auto ncFunc = std::const_pointer_cast<AbstractMetaFunction>(func);
        for (const auto &mod : func->modifications(klass)) {
            if (mod.isRenameModifier())
                ncFunc->setName(mod.renamedToName());
        }
        ncFunc->applyTypeModifications();

        // Make sure class is abstract if one of the functions is
        if (func->isAbstract()) {
            (*klass) += AbstractMetaClass::Abstract;
            (*klass) -= AbstractMetaClass::FinalInTargetLang;
        }

        if (func->isConstructor()) {
            if (func->isPrivate())
                hasPrivateConstructors = true;
            else
                hasPublicConstructors = true;
        }



        // Make sure that we include files for all classes that are in use
        addExtraIncludesForFunction(klass, func);
    }

    if (hasPrivateConstructors && !hasPublicConstructors) {
        (*klass) += AbstractMetaClass::Abstract;
        (*klass) -= AbstractMetaClass::FinalInTargetLang;
    }

    d->setFunctions(funcs, klass);
}

bool AbstractMetaClass::needsInheritanceSetup() const
{
    if (d->m_typeEntry != nullptr) {
        switch (d->m_typeEntry->type()) {
        case TypeEntry::NamespaceType:
        case TypeEntry::SmartPointerType:
            return false;
        default:
            break;
        }
    }
    return true;
}

void AbstractMetaClass::setInheritanceDone(bool b)
{
    d->m_inheritanceDone = b;
}

bool AbstractMetaClass::inheritanceDone() const
{
    return d->m_inheritanceDone;
}

/*******************************************************************************
 * Other stuff...
 */

std::optional<AbstractMetaEnumValue>
    AbstractMetaClass::findEnumValue(const AbstractMetaClassList &classes,
                                     const QString &name)
{
    const auto lst = QStringView{name}.split(u"::");

    if (lst.size() > 1) {
        const auto &prefixName = lst.at(0);
        const auto &enumName = lst.at(1);
        if (auto cl = findClass(classes, prefixName))
            return cl->findEnumValue(enumName.toString());
    }

    for (const auto &metaClass : classes) {
        auto enumValue = metaClass->findEnumValue(name);
        if (enumValue.has_value())
            return enumValue;
    }

    qCWarning(lcShiboken, "no matching enum '%s'", qPrintable(name));
    return {};
}

/// Searches the list after a class that matches \a name; either as C++,
/// Target language base name or complete Target language package.class name.

template <class It>
static It findClassHelper(It begin, It end, QStringView name)
{
    if (name.isEmpty() || begin == end)
        return end;

    if (name.contains(u'.')) { // Search target lang name
        for (auto it = begin; it != end; ++it) {
            if ((*it)->fullName() == name)
                return it;
        }
        return end;
    }

    for (auto it = begin; it != end; ++it) {
        if ((*it)->qualifiedCppName() == name)
            return it;
    }

    if (name.contains(u"::")) // Qualified, cannot possibly match name
        return end;

    for (auto it = begin; it != end; ++it) {
        if ((*it)->name() == name)
            return it;
    }

    return end;
}

AbstractMetaClassPtr AbstractMetaClass::findClass(const AbstractMetaClassList &classes,
                                                  QStringView name)
{
    auto it =findClassHelper(classes.cbegin(), classes.cend(), name);
    return it != classes.cend() ? *it : nullptr;
}

AbstractMetaClassCPtr AbstractMetaClass::findClass(const AbstractMetaClassCList &classes,
                                                   QStringView name)
{
    auto it = findClassHelper(classes.cbegin(), classes.cend(), name);
    return it != classes.cend() ? *it : nullptr;
}

AbstractMetaClassPtr AbstractMetaClass::findClass(const AbstractMetaClassList &classes,
                                                  const TypeEntryCPtr &typeEntry)
{
    for (AbstractMetaClassPtr c : classes) {
        if (c->typeEntry() == typeEntry)
            return c;
    }
    return nullptr;
}

AbstractMetaClassCPtr AbstractMetaClass::findClass(const AbstractMetaClassCList &classes,
                                                      const TypeEntryCPtr &typeEntry)
{
    for (auto c : classes) {
        if (c->typeEntry() == typeEntry)
            return c;
    }
    return nullptr;
}

/// Returns true if this class is a subclass of the given class
bool inheritsFrom(const AbstractMetaClassCPtr &c, const AbstractMetaClassCPtr &cls)
{
    Q_ASSERT(cls != nullptr);

    if (c == cls || c->templateBaseClass() == cls)
        return true;

    return bool(recurseClassHierarchy(c, [cls](const AbstractMetaClassCPtr &c) {
        return cls.get() == c.get();
    }));
}

bool inheritsFrom(const AbstractMetaClassCPtr &c, const QString &name)
{
    if (c->qualifiedCppName() == name)
        return true;

    if (c->templateBaseClass() != nullptr
        && c->templateBaseClass()->qualifiedCppName() == name) {
        return true;
    }

    return bool(recurseClassHierarchy(c, [&name](const AbstractMetaClassCPtr &c) {
        return c->qualifiedCppName() == name;
    }));
}

AbstractMetaClassCPtr findBaseClass(const AbstractMetaClassCPtr &c,
                                    const QString &qualifiedName)
{
    auto tp = c->templateBaseClass();
    if (tp && tp->qualifiedCppName() == qualifiedName)
        return tp;

    return recurseClassHierarchy(c, [&qualifiedName](const AbstractMetaClassCPtr &c) {
        return c->qualifiedCppName() == qualifiedName;
    });
}

// Query functions for generators
bool AbstractMetaClass::isObjectType() const
{
    return d->m_typeEntry->isObject();
}

bool AbstractMetaClass::isCopyable() const
{
    if (isNamespace() || d->m_typeEntry->isObject())
        return false;
    auto copyable = d->m_typeEntry->copyable();
    return copyable == ComplexTypeEntry::CopyableSet
        || (copyable == ComplexTypeEntry::Unknown && isCopyConstructible());
}

bool AbstractMetaClass::isValueTypeWithCopyConstructorOnly() const
{
    return d->m_valueTypeWithCopyConstructorOnly;
}

void AbstractMetaClass::setValueTypeWithCopyConstructorOnly(bool v)
{
    d->m_valueTypeWithCopyConstructorOnly = v;
}

bool AbstractMetaClass::determineValueTypeWithCopyConstructorOnly(const AbstractMetaClassCPtr &c,
                                                                  bool avoidProtectedHack)
{

    if (!c->typeEntry()->isValue())
        return false;
    if (c->attributes().testFlag(AbstractMetaClass::HasRejectedDefaultConstructor))
        return false;
    const auto ctors = c->queryFunctions(FunctionQueryOption::AnyConstructor);
    bool copyConstructorFound = false;
    for (const auto &ctor : ctors) {
        switch (ctor->functionType()) {
        case AbstractMetaFunction::ConstructorFunction:
            if (!ctor->isPrivate() && (ctor->isPublic() || !avoidProtectedHack))
                return false;
            break;
        case AbstractMetaFunction::CopyConstructorFunction:
            copyConstructorFound = true;
            break;
        case AbstractMetaFunction::MoveConstructorFunction:
            break;
        default:
            Q_ASSERT(false);
            break;
        }
    }
    return copyConstructorFound;
}

#ifndef QT_NO_DEBUG_STREAM

void AbstractMetaClass::format(QDebug &debug) const
{
    if (debug.verbosity() > 2)
        debug << static_cast<const void *>(this) << ", ";
    debug << '"' << qualifiedCppName();
    if (const auto count = d->m_templateArgs.size()) {
        for (qsizetype i = 0; i < count; ++i)
            debug << (i ? ',' : '<') << d->m_templateArgs.at(i)->qualifiedCppName();
        debug << '>';
    }
    debug << '"';
    if (isNamespace())
        debug << " [namespace]";
    if (attributes().testFlag(AbstractMetaClass::FinalCppClass))
        debug << " [final]";
    if (attributes().testFlag(AbstractMetaClass::Deprecated))
        debug << " [deprecated]";

    if (d->m_hasPrivateConstructor)
        debug << " [private constructor]";
    if (d->m_hasDeletedDefaultConstructor)
        debug << " [deleted default constructor]";
    if (d->m_hasDeletedCopyConstructor)
        debug << " [deleted copy constructor]";
    if (d->m_hasPrivateDestructor)
        debug << " [private destructor]";
    if (d->m_hasProtectedDestructor)
        debug << " [protected destructor]";
    if (d->m_hasVirtualDestructor)
        debug << " [virtual destructor]";
    if (d->m_valueTypeWithCopyConstructorOnly)
        debug << " [value type with copy constructor only]";

    if (!d->m_baseClasses.isEmpty()) {
        debug << ", inherits ";
        for (const auto &b : d->m_baseClasses)
            debug << " \"" << b->name() << '"';
    }

    if (const qsizetype count = d->m_usingMembers.size()) {
        for (qsizetype i = 0; i < count; ++i) {
            if (i)
                debug << ", ";
            debug << d->m_usingMembers.at(i);
        }
    }

    if (auto templateBase = templateBaseClass()) {
        const auto &instantiatedTypes = templateBaseClassInstantiations();
        debug << ", instantiates \"" << templateBase->name();
        for (qsizetype i = 0, count = instantiatedTypes.size(); i < count; ++i)
            debug << (i ? ',' : '<') << instantiatedTypes.at(i).name();
        debug << ">\"";
    }
    if (const auto count = d->m_propertySpecs.size()) {
        debug << ", properties (" << count << "): [";
        for (qsizetype i = 0; i < count; ++i) {
            if (i)
                debug << ", ";
            d->m_propertySpecs.at(i).formatDebug(debug);
        }
        debug << ']';
    }
}

void AbstractMetaClass::formatMembers(QDebug &debug) const
{
    if (!d->m_enums.isEmpty())
        debug << ", enums[" << d->m_enums.size() << "]=" << d->m_enums;
    if (!d->m_functions.isEmpty()) {
        const auto count = d->m_functions.size();
        debug << ", functions=[" << count << "](";
        for (qsizetype i = 0; i < count; ++i) {
            if (i)
                debug << ", ";
            d->m_functions.at(i)->formatDebugBrief(debug);
        }
        debug << ')';
    }
    if (const auto count = d->m_fields.size()) {
        debug << ", fields=[" << count << "](";
        for (qsizetype i = 0; i < count; ++i) {
            if (i)
                debug << ", ";
            d->m_fields.at(i).formatDebug(debug);
        }
        debug << ')';
    }
}

SourceLocation AbstractMetaClass::sourceLocation() const
{
    return d->m_sourceLocation;
}

void AbstractMetaClass::setSourceLocation(const SourceLocation &sourceLocation)
{
    d->m_sourceLocation = sourceLocation;
}

AbstractMetaClassCList allBaseClasses(const AbstractMetaClassCPtr metaClass)
{
    AbstractMetaClassCList result;
    recurseClassHierarchy(metaClass, [&result] (const AbstractMetaClassCPtr &c) {
        if (!result.contains(c))
            result.append(c);
        return false;
    });
    result.removeFirst(); // remove self
    return result;
}

QDebug operator<<(QDebug debug, const UsingMember &d)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "UsingMember(" << d.access << ' '
        << d.baseClass->qualifiedCppName() << "::" << d.memberName << ')';
    return debug;
}

QDebug operator<<(QDebug d, const AbstractMetaClassCPtr &ac)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "AbstractMetaClass(";
    if (ac) {
        ac->format(d);
        if (d.verbosity() > 2)
            ac->formatMembers(d);
    } else {
        d << '0';
    }
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

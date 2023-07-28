// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abstractmetabuilder_p.h"
#include "abstractmetaargument.h"
#include "abstractmetaenum.h"
#include "abstractmetafield.h"
#include "abstractmetafunction.h"
#include "abstractmetatype.h"
#include "addedfunction.h"
#include "graph.h"
#include "debughelpers_p.h"
#include "exception.h"
#include "messages.h"
#include "propertyspec.h"
#include "reporthandler.h"
#include "sourcelocation.h"
#include "typedatabase.h"
#include "enumtypeentry.h"
#include "enumvaluetypeentry.h"
#include "arraytypeentry.h"
#include "constantvaluetypeentry.h"
#include "containertypeentry.h"
#include "flagstypeentry.h"
#include "functiontypeentry.h"
#include "namespacetypeentry.h"
#include "primitivetypeentry.h"
#include "smartpointertypeentry.h"
#include "templateargumententry.h"
#include "typedefentry.h"
#include "typesystemtypeentry.h"
#include "usingmember.h"

#include "parser/codemodel.h"

#include <clangparser/clangbuilder.h>
#include <clangparser/clangutils.h>
#include <clangparser/compilersupport.h>

#include "qtcompat.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QQueue>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryFile>
#include <QtCore/QTextStream>

#include <cstdio>
#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

static QString stripTemplateArgs(const QString &name)
{
    int pos = name.indexOf(u'<');
    return pos < 0 ? name : name.left(pos);
}

static void fixArgumentIndexes(AbstractMetaArgumentList *list)
{
    for (qsizetype i = 0, size = list->size(); i < size; ++i)
        (*list)[i].setArgumentIndex(i);
}

bool operator<(const RejectEntry &re1, const RejectEntry &re2)
{
    return re1.reason != re2.reason
        ? (re1.reason < re2.reason) : (re1.sortkey < re2.sortkey);
}

QTextStream &operator<<(QTextStream &str, const RejectEntry &re)
{
    str << re.signature;
    if (!re.message.isEmpty())
        str << ": " << re.message;
    return str;
}

bool AbstractMetaBuilderPrivate::m_useGlobalHeader = false;
bool AbstractMetaBuilderPrivate::m_codeModelTestMode = false;

AbstractMetaBuilderPrivate::AbstractMetaBuilderPrivate() :
    m_logDirectory(u"."_s + QDir::separator())
{
}

AbstractMetaBuilder::AbstractMetaBuilder() : d(new AbstractMetaBuilderPrivate)
{
    d->q = this;
}

AbstractMetaBuilder::~AbstractMetaBuilder()
{
    delete d;
}

const AbstractMetaClassList &AbstractMetaBuilder::classes() const
{
    return d->m_metaClasses;
}

AbstractMetaClassList AbstractMetaBuilder::takeClasses()
{
    AbstractMetaClassList result;
    qSwap(result, d->m_metaClasses);
    return result;
}

const AbstractMetaClassList &AbstractMetaBuilder::templates() const
{
    return d->m_templates;
}

AbstractMetaClassList AbstractMetaBuilder::takeTemplates()
{
    AbstractMetaClassList result;
    qSwap(result, d->m_templates);
    return result;
}

const AbstractMetaClassList &AbstractMetaBuilder::smartPointers() const
{
    return d->m_smartPointers;
}

AbstractMetaClassList AbstractMetaBuilder::takeSmartPointers()
{
    AbstractMetaClassList result;
    qSwap(result, d->m_smartPointers);
    return result;
}

const AbstractMetaFunctionCList &AbstractMetaBuilder::globalFunctions() const
{
    return d->m_globalFunctions;
}

const AbstractMetaEnumList &AbstractMetaBuilder::globalEnums() const
{
    return d->m_globalEnums;
}

const QHash<TypeEntryCPtr, AbstractMetaEnum> &AbstractMetaBuilder::typeEntryToEnumsHash() const
{
    return d->m_enums;
}

void AbstractMetaBuilderPrivate::checkFunctionModifications()
{
    const auto &entries = TypeDatabase::instance()->entries();

    for (auto it = entries.cbegin(), end = entries.cend(); it != end; ++it) {
        TypeEntryCPtr entry = it.value();
        if (!entry)
            continue;
        if (!entry->isComplex() || !entry->generateCode())
            continue;

        auto centry = std::static_pointer_cast<const ComplexTypeEntry>(entry);

        if (!centry->generateCode())
            continue;

        FunctionModificationList modifications = centry->functionModifications();

        for (const FunctionModification &modification : std::as_const(modifications)) {
            QString signature = modification.signature();

            QString name = signature.trimmed();
            name.truncate(name.indexOf(u'('));

            const auto clazz = AbstractMetaClass::findClass(m_metaClasses, centry);
            if (!clazz)
                continue;

            bool found = false;
            QStringList possibleSignatures;
            for (const auto &function : clazz->functions()) {
                if (function->implementingClass() == clazz
                    && modification.matches(function->modificationSignatures())) {
                    found = true;
                    break;
                }

                if (function->originalName() == name) {
                    const QString signatures = function->modificationSignatures().join(u'/');
                    possibleSignatures.append(signatures + u" in "_s
                                              + function->implementingClass()->name());
                }
            }

            if (!found) {
                qCWarning(lcShiboken).noquote().nospace()
                    << msgNoFunctionForModification(clazz, signature,
                                                    modification.originalSignature(),
                                                    possibleSignatures, clazz->functions());
            }
        }
    }
}

AbstractMetaClassPtr AbstractMetaBuilderPrivate::argumentToClass(const ArgumentModelItem &argument,
                                                               const AbstractMetaClassCPtr &currentClass)
{
    AbstractMetaClassPtr returned;
    auto type = translateType(argument->type(), currentClass);
    if (!type.has_value())
        return returned;
    TypeEntryCPtr entry = type->typeEntry();
    if (entry && entry->isComplex())
        returned = AbstractMetaClass::findClass(m_metaClasses, entry);
    return returned;
}

/**
 * Checks the argument of a hash function and flags the type if it is a complex type
 */
void AbstractMetaBuilderPrivate::registerHashFunction(const FunctionModelItem &function_item,
                                                      const AbstractMetaClassPtr &currentClass)
{
    if (function_item->isDeleted())
        return;
    ArgumentList arguments = function_item->arguments();
    if (arguments.size() >= 1) { // (Class, Hash seed).
        if (AbstractMetaClassPtr cls = argumentToClass(arguments.at(0), currentClass))
            cls->setHashFunction(function_item->name());
    }
}

void AbstractMetaBuilderPrivate::registerToStringCapabilityIn(const NamespaceModelItem &nsItem)
{
    const FunctionList &streamOps = nsItem->findFunctions(u"operator<<"_s);
    for (const FunctionModelItem &item : streamOps)
        registerToStringCapability(item, nullptr);
    for (const NamespaceModelItem &ni : nsItem->namespaces())
        registerToStringCapabilityIn(ni);
}

/**
 * Check if a class has a debug stream operator that can be used as toString
 */

void AbstractMetaBuilderPrivate::registerToStringCapability(const FunctionModelItem &function_item,
                                                            const AbstractMetaClassPtr &currentClass)
{
    ArgumentList arguments = function_item->arguments();
    if (arguments.size() == 2) {
        if (arguments.at(0)->type().toString() == u"QDebug") {
            const ArgumentModelItem &arg = arguments.at(1);
            if (AbstractMetaClassPtr cls = argumentToClass(arg, currentClass)) {
                if (arg->type().indirections() < 2)
                    cls->setToStringCapability(true, int(arg->type().indirections()));
            }
        }
    }
}

void AbstractMetaBuilderPrivate::traverseOperatorFunction(const FunctionModelItem &item,
                                                          const AbstractMetaClassPtr &currentClass)
{
    if (item->accessPolicy() != Access::Public)
        return;

    const ArgumentList &itemArguments = item->arguments();
    bool firstArgumentIsSelf = true;
    bool unaryOperator = false;

    auto baseoperandClass = argumentToClass(itemArguments.at(0), currentClass);

    if (itemArguments.size() == 1) {
        unaryOperator = true;
    } else if (!baseoperandClass
               || !baseoperandClass->typeEntry()->generateCode()) {
        baseoperandClass = argumentToClass(itemArguments.at(1), currentClass);
        firstArgumentIsSelf = false;
    } else {
        auto type = translateType(item->type(), currentClass);
        const auto retType = type.has_value() ? type->typeEntry() : TypeEntryCPtr{};
        const auto otherArgClass = argumentToClass(itemArguments.at(1), currentClass);
        if (otherArgClass && retType
            && (retType->isValue() || retType->isObject())
            && retType != baseoperandClass->typeEntry()
            && retType == otherArgClass->typeEntry()) {
            baseoperandClass = AbstractMetaClass::findClass(m_metaClasses, retType);
            firstArgumentIsSelf = false;
        }
    }
    if (!baseoperandClass) {
        rejectFunction(item, currentClass, AbstractMetaBuilder::UnmatchedOperator,
                       u"base operand class not found."_s);
        return;
    }

    if (item->isSpaceshipOperator() && !item->isDeleted()) {
        AbstractMetaClass::addSynthesizedComparisonOperators(baseoperandClass);
        return;
    }

    AbstractMetaFunction *metaFunction = traverseFunction(item, baseoperandClass);
    if (metaFunction == nullptr)
        return;

    auto flags = metaFunction->flags();
    // Strip away first argument, since that is the containing object
    AbstractMetaArgumentList arguments = metaFunction->arguments();
    if (firstArgumentIsSelf || unaryOperator) {
        AbstractMetaArgument first = arguments.takeFirst();
        fixArgumentIndexes(&arguments);
        if (!unaryOperator && first.type().indirections())
            metaFunction->setPointerOperator(true);
        metaFunction->setArguments(arguments);
        flags.setFlag(AbstractMetaFunction::Flag::OperatorLeadingClassArgumentRemoved);
        if (first.type().passByValue())
            flags.setFlag(AbstractMetaFunction::Flag::OperatorClassArgumentByValue);
    } else {
        // If the operator method is not unary and the first operator is
        // not of the same type of its owning class we suppose that it
        // must be an reverse operator (e.g. CLASS::operator(TYPE, CLASS)).
        // All operator overloads that operate over a class are already
        // being added as member functions of that class by the API Extractor.
        AbstractMetaArgument last = arguments.takeLast();
        if (last.type().indirections())
            metaFunction->setPointerOperator(true);
        metaFunction->setArguments(arguments);
        metaFunction->setReverseOperator(true);
        flags.setFlag(AbstractMetaFunction::Flag::OperatorTrailingClassArgumentRemoved);
        if (last.type().passByValue())
            flags.setFlag(AbstractMetaFunction::Flag::OperatorClassArgumentByValue);
    }
    metaFunction->setFlags(flags);
    metaFunction->setAccess(Access::Public);
    AbstractMetaClass::addFunction(baseoperandClass, AbstractMetaFunctionCPtr(metaFunction));
    if (!metaFunction->arguments().isEmpty()) {
        const auto include = metaFunction->arguments().constFirst().type().typeEntry()->include();
        baseoperandClass->typeEntry()->addArgumentInclude(include);
    }
    Q_ASSERT(!metaFunction->wasPrivate());
}

bool AbstractMetaBuilderPrivate::traverseStreamOperator(const FunctionModelItem &item,
                                                        const AbstractMetaClassPtr &currentClass)
{
    ArgumentList itemArguments = item->arguments();
    if (itemArguments.size() != 2 || item->accessPolicy() != Access::Public)
        return false;
    auto streamClass = argumentToClass(itemArguments.at(0), currentClass);
    if (streamClass == nullptr || !streamClass->isStream())
        return false;
   auto streamedClass = argumentToClass(itemArguments.at(1), currentClass);
   if (streamedClass == nullptr)
       return false;

   AbstractMetaFunction *streamFunction = traverseFunction(item, streamedClass);
   if (!streamFunction)
       return false;

    // Strip first argument, since that is the containing object
   AbstractMetaArgumentList arguments = streamFunction->arguments();
   if (!streamClass->typeEntry()->generateCode()) {
       arguments.takeLast();
   } else {
       arguments.takeFirst();
       fixArgumentIndexes(&arguments);
   }

    streamFunction->setArguments(arguments);

    *streamFunction += AbstractMetaFunction::FinalInTargetLang;
    streamFunction->setAccess(Access::Public);

    AbstractMetaClassPtr funcClass;

    if (!streamClass->typeEntry()->generateCode()) {
        AbstractMetaArgumentList reverseArgs = streamFunction->arguments();
        std::reverse(reverseArgs.begin(), reverseArgs.end());
        fixArgumentIndexes(&reverseArgs);
        streamFunction->setArguments(reverseArgs);
        streamFunction->setReverseOperator(true);
        funcClass = streamedClass;
    } else {
        funcClass = streamClass;
    }

    AbstractMetaClass::addFunction(funcClass, AbstractMetaFunctionCPtr(streamFunction));
    auto funcTe = funcClass->typeEntry();
    if (funcClass == streamClass)
        funcTe->addArgumentInclude(streamedClass->typeEntry()->include());
    else
        funcTe->addArgumentInclude(streamClass->typeEntry()->include());
    return true;
}

static bool metaEnumLessThan(const AbstractMetaEnum &e1, const AbstractMetaEnum &e2)
{ return e1.fullName() < e2.fullName(); }

static bool metaClassLessThan(const AbstractMetaClassCPtr &c1, const AbstractMetaClassCPtr &c2)
{ return c1->fullName() < c2->fullName(); }

static bool metaFunctionLessThan(const AbstractMetaFunctionCPtr &f1, const AbstractMetaFunctionCPtr &f2)
{ return f1->name() < f2->name(); }

void AbstractMetaBuilderPrivate::sortLists()
{
    // Ensure indepedent classes are in alphabetical order,
    std::sort(m_metaClasses.begin(), m_metaClasses.end(), metaClassLessThan);
    // this is a temporary solution before new type revision implementation
    // We need move QMetaObject register before QObject.
    Dependencies additionalDependencies;
    if (auto qObjectClass = AbstractMetaClass::findClass(m_metaClasses, u"QObject")) {
        if (auto qMetaObjectClass = AbstractMetaClass::findClass(m_metaClasses, u"QMetaObject")) {
            Dependency dependency;
            dependency.parent = qMetaObjectClass;
            dependency.child = qObjectClass;
            additionalDependencies.append(dependency);
        }
    }
    m_metaClasses = classesTopologicalSorted(m_metaClasses, additionalDependencies);

    for (const auto &cls : std::as_const(m_metaClasses))
        cls->sortFunctions();

    // Ensure that indexes are in alphabetical order, roughly, except
    // for classes, which are in topological order
    std::sort(m_globalEnums.begin(), m_globalEnums.end(), metaEnumLessThan);
    std::sort(m_templates.begin(), m_templates.end(), metaClassLessThan);
    std::sort(m_smartPointers.begin(), m_smartPointers.end(), metaClassLessThan);
    std::sort(m_globalFunctions.begin(), m_globalFunctions.end(), metaFunctionLessThan);
}

FileModelItem AbstractMetaBuilderPrivate::buildDom(QByteArrayList arguments,
                                                   bool addCompilerSupportArguments,
                                                   LanguageLevel level,
                                                   unsigned clangFlags)
{
    clang::Builder builder;
    builder.setSystemIncludes(TypeDatabase::instance()->systemIncludes());
    if (addCompilerSupportArguments) {
        if (level == LanguageLevel::Default)
            level = clang::emulatedCompilerLanguageLevel();
        arguments.prepend(QByteArrayLiteral("-std=")
                          + clang::languageLevelOption(level));
    }
    FileModelItem result = clang::parse(arguments, addCompilerSupportArguments,
                                        clangFlags, builder)
        ? builder.dom() : FileModelItem();
    const clang::BaseVisitor::Diagnostics &diagnostics = builder.diagnostics();
    if (const auto diagnosticsCount = diagnostics.size()) {
        QDebug d = qWarning();
        d.nospace();
        d.noquote();
        d << "Clang: " << diagnosticsCount << " diagnostic messages:\n";
        for (qsizetype i = 0; i < diagnosticsCount; ++i)
            d << "  " << diagnostics.at(i) << '\n';
    }
    return result;
}

// List of candidates for a mismatched added global function.
static QStringList functionCandidates(const AbstractMetaFunctionCList &list,
                                      const QString &signature)
{
    QString name  = signature;
    const int parenPos = name.indexOf(u'(');
    if (parenPos > 0)
        name.truncate(parenPos);
    QStringList result;
    for (const auto &func : list) {
        if (name == func->name())
            result += func->minimalSignature();
    }
    return result;
}

void AbstractMetaBuilderPrivate::traverseDom(const FileModelItem &dom,
                                             ApiExtractorFlags flags)
{
    const TypeDatabase *types = TypeDatabase::instance();

    pushScope(dom);

    // Start the generation...
    const ClassList &typeValues = dom->classes();

    ReportHandler::startProgress("Generating class model ("
                                 + QByteArray::number(typeValues.size()) + ")...");
    for (const ClassModelItem &item : typeValues) {
        if (const auto cls = traverseClass(dom, item, nullptr))
            addAbstractMetaClass(cls, item.get());
    }

    // We need to know all global enums
    const EnumList &enums = dom->enums();

    ReportHandler::startProgress("Generating enum model ("
                                 + QByteArray::number(enums.size()) + ")...");
    for (const EnumModelItem &item : enums) {
        auto metaEnum = traverseEnum(item, nullptr, QSet<QString>());
        if (metaEnum.has_value()) {
            if (metaEnum->typeEntry()->generateCode())
                m_globalEnums << metaEnum.value();
        }
    }

    const auto &namespaceTypeValues = dom->namespaces();
    ReportHandler::startProgress("Generating namespace model ("
                                 + QByteArray::number(namespaceTypeValues.size()) + ")...");
    for (const NamespaceModelItem &item : namespaceTypeValues)
        traverseNamespace(dom, item);

    // Go through all typedefs to see if we have defined any
    // specific typedefs to be used as classes.
    const TypeDefList typeDefs = dom->typeDefs();
    ReportHandler::startProgress("Resolving typedefs ("
                                 + QByteArray::number(typeDefs.size()) + ")...");
    for (const TypeDefModelItem &typeDef : typeDefs) {
        if (const auto cls = traverseTypeDef(dom, typeDef, nullptr))
            addAbstractMetaClass(cls, typeDef.get());
    }

    traverseTypesystemTypedefs();

    for (const ClassModelItem &item : typeValues)
        traverseClassMembers(item);

    for (const NamespaceModelItem &item : namespaceTypeValues)
        traverseNamespaceMembers(item);

    // Global functions
    const FunctionList &functions = dom->functions();
    for (const FunctionModelItem &func : functions) {
        if (func->accessPolicy() != Access::Public || func->name().startsWith(u"operator"))
            continue;

        FunctionTypeEntryPtr funcEntry = types->findFunctionType(func->name());
        if (!funcEntry || !funcEntry->generateCode())
            continue;

        AbstractMetaFunction *metaFunc = traverseFunction(func, nullptr);
        if (!metaFunc)
            continue;

        AbstractMetaFunctionCPtr metaFuncPtr(metaFunc);
        if (!funcEntry->hasSignature(metaFunc->minimalSignature()))
            continue;

        metaFunc->setTypeEntry(funcEntry);
        applyFunctionModifications(metaFunc);
        metaFunc->applyTypeModifications();

        setInclude(funcEntry, func->fileName());

        m_globalFunctions << metaFuncPtr;
    }

    ReportHandler::startProgress("Fixing class inheritance...");
    for (const auto &cls : std::as_const(m_metaClasses)) {
        if (cls->needsInheritanceSetup()) {
            setupInheritance(cls);
            traverseUsingMembers(cls);
            if (cls->templateBaseClass())
                inheritTemplateFunctions(cls);
            if (!cls->hasVirtualDestructor() && cls->baseClass()
                && cls->baseClass()->hasVirtualDestructor())
                cls->setHasVirtualDestructor(true);
        }
    }

    ReportHandler::startProgress("Detecting inconsistencies in class model...");
    for (const auto &cls : std::as_const(m_metaClasses)) {
        AbstractMetaClass::fixFunctions(cls);

        if (cls->canAddDefaultConstructor())
            AbstractMetaClass::addDefaultConstructor(cls);
        if (cls->canAddDefaultCopyConstructor())
            AbstractMetaClass::addDefaultCopyConstructor(cls);

        const bool avoidProtectedHack = flags.testFlag(ApiExtractorFlag::AvoidProtectedHack);
        const bool vco =
            AbstractMetaClass::determineValueTypeWithCopyConstructorOnly(cls, avoidProtectedHack);
        cls->setValueTypeWithCopyConstructorOnly(vco);
        cls->typeEntry()->setValueTypeWithCopyConstructorOnly(vco);
    }

    const auto &allEntries = types->entries();

    ReportHandler::startProgress("Detecting inconsistencies in typesystem ("
                                 + QByteArray::number(allEntries.size()) + ")...");
    for (auto it = allEntries.cbegin(), end = allEntries.cend(); it != end; ++it) {
        TypeEntryPtr entry = it.value();
        if (!entry->isPrimitive()) {
            if ((entry->isValue() || entry->isObject())
                && !types->shouldDropTypeEntry(entry->qualifiedCppName())
                && !entry->isContainer()
                && !entry->isCustom()
                && entry->generateCode()
                && !AbstractMetaClass::findClass(m_metaClasses, entry)) {
                qCWarning(lcShiboken, "%s", qPrintable(msgTypeNotDefined(entry)));
            } else if (entry->generateCode() && entry->type() == TypeEntry::FunctionType) {
                auto fte = std::static_pointer_cast<const FunctionTypeEntry>(entry);
                const QStringList &signatures = fte->signatures();
                for (const QString &signature : signatures) {
                    bool ok = false;
                    for (const auto &func : std::as_const(m_globalFunctions)) {
                        if (signature == func->minimalSignature()) {
                            ok = true;
                            break;
                        }
                    }
                    if (!ok) {
                        const QStringList candidates = functionCandidates(m_globalFunctions,
                                                                          signatures.constFirst());
                        qCWarning(lcShiboken, "%s",
                                  qPrintable(msgGlobalFunctionNotDefined(fte, signature, candidates)));
                    }
                }
            } else if (entry->isEnum() && entry->generateCode()) {
                const auto enumEntry = std::static_pointer_cast<const EnumTypeEntry>(entry);
                const auto cls = AbstractMetaClass::findClass(m_metaClasses,
                                                              enumEntry->parent());

                const bool enumFound = cls
                    ? cls->findEnum(entry->targetLangEntryName()).has_value()
                    : m_enums.contains(entry);

                if (!enumFound) {
                    entry->setCodeGeneration(TypeEntry::GenerateNothing);
                    qCWarning(lcShiboken, "%s",
                              qPrintable(msgEnumNotDefined(enumEntry)));
                }

            }
        }
    }

    {
        const FunctionList &hashFunctions = dom->findFunctions(u"qHash"_s);
        for (const FunctionModelItem &item : hashFunctions)
            registerHashFunction(item, nullptr);
    }

    registerToStringCapabilityIn(dom);

    for (const auto &func : dom->functions()) {
        switch (func->functionType()) {
        case CodeModel::ComparisonOperator:
        case CodeModel::ArithmeticOperator:
        case CodeModel::BitwiseOperator:
        case CodeModel::LogicalOperator:
            traverseOperatorFunction(func, nullptr);
            break;
        case CodeModel::ShiftOperator:
            if (!traverseStreamOperator(func, nullptr))
                traverseOperatorFunction(func, nullptr);
        default:
            break;
        }
    }

    ReportHandler::startProgress("Checking inconsistencies in function modifications...");

    checkFunctionModifications();

    ReportHandler::startProgress("Writing log files...");

    for (const auto &cls : std::as_const(m_metaClasses)) {
//         setupEquals(cls);
//         setupComparable(cls);
        setupExternalConversion(cls);

        // sort all inner classes topologically
        if (!cls->typeEntry()->codeGeneration() || cls->innerClasses().size() < 2)
            continue;

        cls->setInnerClasses(classesTopologicalSorted(cls->innerClasses()));
    }

    fixSmartPointers();

    dumpLog();

    sortLists();

    // Functions added to the module on the type system.
    QString errorMessage;
    const AddedFunctionList &globalUserFunctions = types->globalUserFunctions();
    for (const AddedFunctionPtr &addedFunc : globalUserFunctions) {
        if (!traverseAddedGlobalFunction(addedFunc, &errorMessage))
            throw Exception(errorMessage);
    }

    if (!m_codeModelTestMode) {
        m_itemToClass.clear();
        m_classToItem.clear();
        m_typeSystemTypeDefs.clear();
        m_scopes.clear();
    }

    ReportHandler::endProgress();
}

bool AbstractMetaBuilder::build(const QByteArrayList &arguments,
                                ApiExtractorFlags apiExtractorFlags,
                                bool addCompilerSupportArguments,
                                LanguageLevel level,
                                unsigned clangFlags)
{
    const FileModelItem dom = d->buildDom(arguments, addCompilerSupportArguments,
                                          level, clangFlags);
    if (!dom)
        return false;
    if (ReportHandler::isDebug(ReportHandler::MediumDebug))
        qCDebug(lcShiboken) << dom.get();
    d->traverseDom(dom, apiExtractorFlags);

    return true;
}

void AbstractMetaBuilder::setLogDirectory(const QString &logDir)
{
    d->m_logDirectory = logDir;
    if (!d->m_logDirectory.endsWith(QDir::separator()))
       d->m_logDirectory.append(QDir::separator());
}

void AbstractMetaBuilderPrivate::addAbstractMetaClass(const AbstractMetaClassPtr &cls,
                                                      const _CodeModelItem *item)
{
    m_itemToClass.insert(item, cls);
    m_classToItem.insert(cls, item);
    if (cls->typeEntry()->isContainer()) {
        m_templates << cls;
    } else if (cls->typeEntry()->isSmartPointer()) {
        m_smartPointers << cls;
    } else {
        m_metaClasses << cls;
    }
}

AbstractMetaClassPtr
    AbstractMetaBuilderPrivate::traverseNamespace(const FileModelItem &dom,
                                                  const NamespaceModelItem &namespaceItem)
{
    QString namespaceName = currentScope()->qualifiedName().join(u"::"_s);
    if (!namespaceName.isEmpty())
        namespaceName.append(u"::"_s);
    namespaceName.append(namespaceItem->name());

    if (TypeDatabase::instance()->isClassRejected(namespaceName)) {
        m_rejectedClasses.insert({AbstractMetaBuilder::GenerationDisabled,
                                  namespaceName, namespaceName, QString{}});
        return {};
    }

    auto type = TypeDatabase::instance()->findNamespaceType(namespaceName, namespaceItem->fileName());
    if (!type) {
        const QString rejectReason = msgNamespaceNoTypeEntry(namespaceItem, namespaceName);
        qCWarning(lcShiboken, "%s", qPrintable(rejectReason));
        m_rejectedClasses.insert({AbstractMetaBuilder::GenerationDisabled,
                                  namespaceName, namespaceName, rejectReason});
        return nullptr;
    }

    if (namespaceItem->type() == NamespaceType::Inline) {
        type->setInlineNamespace(true);
        TypeDatabase::instance()->addInlineNamespaceLookups(type);
    }

    // Continue populating namespace?
    AbstractMetaClassPtr metaClass = AbstractMetaClass::findClass(m_metaClasses, type);
    if (!metaClass) {
        metaClass.reset(new AbstractMetaClass);
        metaClass->setTypeEntry(type);
        addAbstractMetaClass(metaClass, namespaceItem.get());
        if (auto extendsType = type->extends()) {
            const auto extended = AbstractMetaClass::findClass(m_metaClasses, extendsType);
            if (!extended) {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgNamespaceToBeExtendedNotFound(extendsType->name(), extendsType->targetLangPackage())));
                return {};
            }
            metaClass->setExtendedNamespace(extended);
        }
    } else {
        m_itemToClass.insert(namespaceItem.get(), metaClass);
    }

    traverseEnums(namespaceItem, metaClass, namespaceItem->enumsDeclarations());

    pushScope(namespaceItem);

    const ClassList &classes = namespaceItem->classes();
    for (const ClassModelItem &cls : classes) {
        const auto mjc = traverseClass(dom, cls, metaClass);
        if (mjc) {
            metaClass->addInnerClass(mjc);
            mjc->setEnclosingClass(metaClass);
            addAbstractMetaClass(mjc, cls.get());
        }
    }

    // Go through all typedefs to see if we have defined any
    // specific typedefs to be used as classes.
    const TypeDefList typeDefs = namespaceItem->typeDefs();
    for (const TypeDefModelItem &typeDef : typeDefs) {
        const auto cls = traverseTypeDef(dom, typeDef, metaClass);
        if (cls) {
            metaClass->addInnerClass(cls);
            cls->setEnclosingClass(metaClass);
            addAbstractMetaClass(cls, typeDef.get());
        }
    }

    // Traverse namespaces recursively
    for (const NamespaceModelItem &ni : namespaceItem->namespaces()) {
        const auto mjc = traverseNamespace(dom, ni);
        if (mjc) {
            metaClass->addInnerClass(mjc);
            mjc->setEnclosingClass(metaClass);
            m_classToItem.insert(mjc, ni.get()); // Add for enum lookup.
            m_itemToClass.insert(ni.get(), mjc);
        }
    }

    popScope();

    if (!type->include().isValid())
        setInclude(type, namespaceItem->fileName());

    return metaClass;
}

std::optional<AbstractMetaEnum>
    AbstractMetaBuilderPrivate::traverseEnum(const EnumModelItem &enumItem,
                                             const AbstractMetaClassPtr &enclosing,
                                             const QSet<QString> &enumsDeclarations)
{
    QString qualifiedName = enumItem->qualifiedName().join(u"::"_s);

    TypeEntryPtr typeEntry;
    const auto enclosingTypeEntry = enclosing ? enclosing->typeEntry() : TypeEntryCPtr{};
    if (enumItem->accessPolicy() == Access::Private) {
        typeEntry.reset(new EnumTypeEntry(enumItem->qualifiedName().constLast(),
                                          QVersionNumber(0, 0), enclosingTypeEntry));
        TypeDatabase::instance()->addType(typeEntry);
    } else if (enumItem->enumKind() != AnonymousEnum) {
        typeEntry = TypeDatabase::instance()->findType(qualifiedName);
    } else {
        QStringList tmpQualifiedName = enumItem->qualifiedName();
        const EnumeratorList &enums = enumItem->enumerators();
        for (const EnumeratorModelItem &enumValue : enums) {
            tmpQualifiedName.removeLast();
            tmpQualifiedName << enumValue->name();
            qualifiedName = tmpQualifiedName.join(u"::"_s);
            typeEntry = TypeDatabase::instance()->findType(qualifiedName);
            if (typeEntry)
                break;
        }
    }

    QString enumName = enumItem->name();

    QString className;
    if (enclosingTypeEntry)
        className = enclosingTypeEntry->qualifiedCppName();

    QString rejectReason;
    if (TypeDatabase::instance()->isEnumRejected(className, enumName, &rejectReason)) {
        if (typeEntry)
            typeEntry->setCodeGeneration(TypeEntry::GenerateNothing);
        m_rejectedEnums.insert({AbstractMetaBuilder::GenerationDisabled, qualifiedName,
                                qualifiedName, rejectReason});
        return {};
    }

    const bool rejectionWarning = !enclosing || enclosing->typeEntry()->generateCode();

    if (!typeEntry) {
        const QString rejectReason = msgNoEnumTypeEntry(enumItem, className);
        if (rejectionWarning)
            qCWarning(lcShiboken, "%s", qPrintable(rejectReason));
        m_rejectedEnums.insert({AbstractMetaBuilder::NotInTypeSystem, qualifiedName,
                                qualifiedName, rejectReason});
        return {};
    }

    if (!typeEntry->isEnum()) {
        const QString rejectReason = msgNoEnumTypeConflict(enumItem, className, typeEntry);
        if (rejectionWarning)
            qCWarning(lcShiboken, "%s", qPrintable(rejectReason));
        m_rejectedEnums.insert({AbstractMetaBuilder::NotInTypeSystem, qualifiedName,
                                qualifiedName, rejectReason});
        return {};
    }

    AbstractMetaEnum metaEnum;
    metaEnum.setEnumKind(enumItem->enumKind());
    metaEnum.setDeprecated(enumItem->isDeprecated());
    metaEnum.setSigned(enumItem->isSigned());
    if (enumsDeclarations.contains(qualifiedName)
        || enumsDeclarations.contains(enumName)) {
        metaEnum.setHasQEnumsDeclaration(true);
    }

    auto enumTypeEntry = std::static_pointer_cast<EnumTypeEntry>(typeEntry);
    metaEnum.setTypeEntry(enumTypeEntry);
    metaEnum.setAccess(enumItem->accessPolicy());
    if (metaEnum.access() == Access::Private)
        typeEntry->setCodeGeneration(TypeEntry::GenerateNothing);

    const EnumeratorList &enums = enumItem->enumerators();
    for (const EnumeratorModelItem &value : enums) {

        AbstractMetaEnumValue metaEnumValue;
        metaEnumValue.setName(value->name());
        // Deciding the enum value...

        metaEnumValue.setStringValue(value->stringValue());
        metaEnumValue.setValue(value->value());
        metaEnumValue.setDeprecated(value->isDeprecated());
        metaEnum.addEnumValue(metaEnumValue);
    }

    if (!metaEnum.typeEntry()->include().isValid()) {
        auto te = std::const_pointer_cast<EnumTypeEntry>(metaEnum.typeEntry());
        setInclude(te, enumItem->fileName());
    }

    // Register all enum values on Type database
    const bool isScopedEnum = enumItem->enumKind() == EnumClass;
    const EnumeratorList &enumerators = enumItem->enumerators();
    for (const EnumeratorModelItem &e : enumerators) {
        auto enumValue = std::make_shared<EnumValueTypeEntry>(e->name(), e->stringValue(),
                                                              enumTypeEntry, isScopedEnum,
                                                              enumTypeEntry->version());
        TypeDatabase::instance()->addType(enumValue);
        if (e->value().isNullValue())
            enumTypeEntry->setNullValue(enumValue);
    }

    metaEnum.setEnclosingClass(enclosing);
    m_enums.insert(typeEntry, metaEnum);

    return metaEnum;
}

AbstractMetaClassPtr AbstractMetaBuilderPrivate::traverseTypeDef(const FileModelItem &,
                                                               const TypeDefModelItem &typeDef,
                                                               const AbstractMetaClassPtr &currentClass)
{
    TypeDatabase *types = TypeDatabase::instance();
    QString className = stripTemplateArgs(typeDef->name());

    QString fullClassName = className;
    // we have an inner class
    if (currentClass) {
        fullClassName = stripTemplateArgs(currentClass->typeEntry()->qualifiedCppName())
                          + u"::"_s + fullClassName;
    }

    // If this is the alias for a primitive type
    // we store the aliased type on the alias
    // TypeEntry
    const auto ptype = types->findPrimitiveType(className);
    const auto &targetNames = typeDef->type().qualifiedName();
    const auto pTarget = targetNames.size() == 1
        ? types->findPrimitiveType(targetNames.constFirst()) : PrimitiveTypeEntryPtr{};
    if (ptype) {
        ptype->setReferencedTypeEntry(pTarget);
        return nullptr;
    }

    // It is a (nested?) global typedef to a primitive type
    // (like size_t = unsigned)? Add it to the type DB.
    if (pTarget && isCppPrimitive(basicReferencedNonBuiltinTypeEntry(pTarget))
        && currentClass == nullptr) {
        auto pte = std::make_shared<PrimitiveTypeEntry>(className, QVersionNumber{},
                                                        TypeEntryCPtr{});
        pte->setReferencedTypeEntry(pTarget);
        pte->setBuiltIn(true);
        types->addType(pte);
        return nullptr;
    }

    // If we haven't specified anything for the typedef, then we don't care
    auto type = types->findComplexType(fullClassName);
    if (!type)
        return nullptr;

    auto metaClass = std::make_shared<AbstractMetaClass>();
    metaClass->setTypeDef(true);
    metaClass->setTypeEntry(type);
    metaClass->setBaseClassNames(QStringList(typeDef->type().toString()));

    // Set the default include file name
    if (!type->include().isValid())
        setInclude(type, typeDef->fileName());

    fillAddedFunctions(metaClass);

    return metaClass;
}

// Add the typedef'ed classes
void AbstractMetaBuilderPrivate::traverseTypesystemTypedefs()
{
    const auto &entries = TypeDatabase::instance()->typedefEntries();
    for (auto it = entries.begin(), end = entries.end(); it != end; ++it) {
        TypedefEntryPtr te = it.value();
        auto metaClass = std::make_shared<AbstractMetaClass>();
        metaClass->setTypeDef(true);
        metaClass->setTypeEntry(te->target());
        metaClass->setBaseClassNames(QStringList(te->sourceType()));
        fillAddedFunctions(metaClass);
        addAbstractMetaClass(metaClass, nullptr);
        // Ensure base classes are set up when traversing functions for the
        // type to be resolved.
        if (setupInheritance(metaClass)) {
            // Create an entry to look up up types obtained from parsing
            // functions in reverse. As opposed to container specializations,
            // which are generated into every instantiating module (indicated
            // by ContainerTypeEntry::targetLangPackage() being empty), the
            // correct index array of the module needs to be found by reverse
            // mapping the instantiations to the typedef entry.
            // Synthesize a AbstractMetaType which would be found by an
            // instantiation.
            AbstractMetaType sourceType;
            sourceType.setTypeEntry(metaClass->templateBaseClass()->typeEntry());
            sourceType.setInstantiations(metaClass->templateBaseClassInstantiations());
            sourceType.decideUsagePattern();
            m_typeSystemTypeDefs.append({sourceType, metaClass});
        }
    }
}

AbstractMetaClassPtr AbstractMetaBuilderPrivate::traverseClass(const FileModelItem &dom,
                                                             const ClassModelItem &classItem,
                                                             const AbstractMetaClassPtr &currentClass)
{
    QString className = stripTemplateArgs(classItem->name());
    QString fullClassName = className;

    // we have inner an class
    if (currentClass) {
        fullClassName = stripTemplateArgs(currentClass->typeEntry()->qualifiedCppName())
                          + u"::"_s + fullClassName;
    }

    const auto type = TypeDatabase::instance()->findComplexType(fullClassName);
    AbstractMetaBuilder::RejectReason reason = AbstractMetaBuilder::NoReason;

    if (TypeDatabase::instance()->isClassRejected(fullClassName)) {
        reason = AbstractMetaBuilder::GenerationDisabled;
    } else if (!type) {
        TypeEntryPtr te = TypeDatabase::instance()->findType(fullClassName);
        if (te && !te->isComplex()) {
            reason = AbstractMetaBuilder::RedefinedToNotClass;
            // Set the default include file name
            if (!te->include().isValid())
                setInclude(te, classItem->fileName());
        } else {
            reason = AbstractMetaBuilder::NotInTypeSystem;
        }
    } else if (type->codeGeneration() == TypeEntry::GenerateNothing) {
        reason = AbstractMetaBuilder::GenerationDisabled;
    }
    if (reason != AbstractMetaBuilder::NoReason) {
        if (fullClassName.isEmpty()) {
            QTextStream(&fullClassName) << "anonymous struct at " << classItem->fileName()
                << ':' << classItem->startLine();
        }
        m_rejectedClasses.insert({reason, fullClassName, fullClassName, QString{}});
        return nullptr;
    }

    auto metaClass = std::make_shared<AbstractMetaClass>();
    metaClass->setSourceLocation(classItem->sourceLocation());
    metaClass->setTypeEntry(type);
    if ((type->typeFlags() & ComplexTypeEntry::ForceAbstract) != 0)
        *metaClass += AbstractMetaClass::Abstract;

    if (classItem->isFinal())
        *metaClass += AbstractMetaClass::FinalCppClass;

    if (classItem->classType() == CodeModel::Struct)
        *metaClass += AbstractMetaClass::Struct;

    QStringList baseClassNames;
    const QList<_ClassModelItem::BaseClass> &baseClasses = classItem->baseClasses();
    for (const _ClassModelItem::BaseClass &baseClass : baseClasses) {
        if (baseClass.accessPolicy == Access::Public)
            baseClassNames.append(baseClass.name);
    }

    metaClass->setBaseClassNames(baseClassNames);
    if (type->stream())
        metaClass->setStream(true);

    if (ReportHandler::isDebug(ReportHandler::MediumDebug)) {
        const QString message = type->isContainer()
            ? u"container: '"_s + fullClassName + u'\''
            : u"class: '"_s + metaClass->fullName() + u'\'';
        qCInfo(lcShiboken, "%s", qPrintable(message));
    }

    TemplateParameterList template_parameters = classItem->templateParameters();
    TypeEntryCList template_args;
    template_args.clear();
    auto argumentParent = typeSystemTypeEntry(metaClass->typeEntry());
    for (qsizetype i = 0; i < template_parameters.size(); ++i) {
        const TemplateParameterModelItem &param = template_parameters.at(i);
        auto param_type =
            std::make_shared<TemplateArgumentEntry>(param->name(), type->version(),
                                                    argumentParent);
        param_type->setOrdinal(i);
        template_args.append(TypeEntryCPtr(param_type));
    }
    metaClass->setTemplateArguments(template_args);

    parseQ_Properties(metaClass, classItem->propertyDeclarations());

    traverseEnums(classItem, metaClass, classItem->enumsDeclarations());

    // Inner classes
    {
        const ClassList &innerClasses = classItem->classes();
        for (const ClassModelItem &ci : innerClasses) {
            const auto cl = traverseClass(dom, ci, metaClass);
            if (cl) {
                cl->setEnclosingClass(metaClass);
                metaClass->addInnerClass(cl);
                addAbstractMetaClass(cl, ci.get());
            }
        }

    }

    // Go through all typedefs to see if we have defined any
    // specific typedefs to be used as classes.
    const TypeDefList typeDefs = classItem->typeDefs();
    for (const TypeDefModelItem &typeDef : typeDefs) {
        const auto cls = traverseTypeDef(dom, typeDef, metaClass);
        if (cls) {
            cls->setEnclosingClass(metaClass);
            addAbstractMetaClass(cls, typeDef.get());
        }
    }

    // Set the default include file name
    if (!type->include().isValid())
        setInclude(type, classItem->fileName());

    return metaClass;
}

void AbstractMetaBuilderPrivate::traverseScopeMembers(const ScopeModelItem &item,
                                                      const AbstractMetaClassPtr &metaClass)
{
    // Classes/Namespace members
    traverseFields(item, metaClass);
    traverseFunctions(item, metaClass);

    // Inner classes
    const ClassList &innerClasses = item->classes();
    for (const ClassModelItem &ci : innerClasses)
        traverseClassMembers(ci);
}

void AbstractMetaBuilderPrivate::traverseClassMembers(const ClassModelItem &item)
{
    const auto metaClass = m_itemToClass.value(item.get());
    if (metaClass) // Class members
        traverseScopeMembers(item, metaClass);
}

void AbstractMetaBuilderPrivate::traverseUsingMembers(const AbstractMetaClassPtr &metaClass)
{
    const _CodeModelItem *item = m_classToItem.value(metaClass);
    if (item == nullptr || item->kind() != _CodeModelItem::Kind_Class)
        return;
    auto classItem = static_cast<const _ClassModelItem *>(item);
    for (const auto &um : classItem->usingMembers()) {
        QString className = um.className;
        int pos = className.indexOf(u'<'); // strip "QList<value>"
        if (pos != -1)
            className.truncate(pos);
        if (auto baseClass = findBaseClass(metaClass, className)) {
            QString name = um.memberName;
            const int lastQualPos = name.lastIndexOf(u"::"_s);
            if (lastQualPos != -1)
                name.remove(0, lastQualPos + 2);
            metaClass->addUsingMember({name, baseClass, um.access});
        } else {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUsingMemberClassNotFound(metaClass, um.className,
                                                             um.memberName)));
        }
    }
}

void AbstractMetaBuilderPrivate::traverseNamespaceMembers(const NamespaceModelItem &item)
{
    const auto metaClass = m_itemToClass.value(item.get());
    if (!metaClass)
        return;

    // Namespace members
    traverseScopeMembers(item, metaClass);

    // Inner namespaces
    for (const NamespaceModelItem &ni : item->namespaces())
        traverseNamespaceMembers(ni);

}

static inline QString fieldSignatureWithType(const VariableModelItem &field)
{
    return field->name() + QStringLiteral(" -> ") + field->type().toString();
}

static inline QString qualifiedFieldSignatureWithType(const QString &className,
                                                      const VariableModelItem &field)
{
    return className + u"::"_s + fieldSignatureWithType(field);
}

std::optional<AbstractMetaField>
    AbstractMetaBuilderPrivate::traverseField(const VariableModelItem &field,
                                              const AbstractMetaClassCPtr &cls)
{
    QString fieldName = field->name();
    QString className = cls->typeEntry()->qualifiedCppName();

    // Ignore friend decl.
    if (field->isFriend())
        return {};

    if (field->accessPolicy() == Access::Private)
        return {};

    QString rejectReason;
    if (TypeDatabase::instance()->isFieldRejected(className, fieldName, &rejectReason)) {
        const QString signature = qualifiedFieldSignatureWithType(className, field);
        m_rejectedFields.insert({AbstractMetaBuilder::GenerationDisabled,
                                 signature, signature, rejectReason});
        return {};
    }


    AbstractMetaField metaField;
    metaField.setName(fieldName);
    metaField.setEnclosingClass(cls);

    TypeInfo fieldType = field->type();
    auto metaType = translateType(fieldType, cls);

    if (!metaType.has_value()) {
        const QString type = TypeInfo::resolveType(fieldType, currentScope()).qualifiedName().join(u"::"_s);
        if (cls->typeEntry()->generateCode()) {
             qCWarning(lcShiboken, "%s",
                       qPrintable(msgSkippingField(field, cls->name(), type)));
        }
        return {};
    }

    metaField.setType(metaType.value());

    metaField.setStatic(field->isStatic());
    metaField.setAccess(field->accessPolicy());

    return metaField;
}

static bool applyFieldModifications(AbstractMetaField *f)
{
    const auto &modifications = f->modifications();
    for (const auto &mod : modifications) {
        if (mod.isRemoved())
            return false;
        if (mod.isRenameModifier()) {
            f->setOriginalName(f->name());
            f->setName(mod.renamedToName());
        } else if (!mod.isReadable()) {
            f->setGetterEnabled(false);
        } else if (!mod.isWritable()) {
            f->setSetterEnabled(false);
        }
    }
    return true;
}

void AbstractMetaBuilderPrivate::traverseFields(const ScopeModelItem &scope_item,
                                                const AbstractMetaClassPtr &metaClass)
{
    const VariableList &variables = scope_item->variables();
    for (const VariableModelItem &field : variables) {
        auto metaFieldO = traverseField(field, metaClass);
        if (metaFieldO.has_value()) {
            AbstractMetaField metaField = metaFieldO.value();
            if (applyFieldModifications(&metaField))
                metaClass->addField(metaField);
        }
    }
}

void AbstractMetaBuilderPrivate::fixReturnTypeOfConversionOperator(AbstractMetaFunction *metaFunction)
{
    if (!metaFunction->isConversionOperator())
        return;

    TypeDatabase *types = TypeDatabase::instance();
    static const QRegularExpression operatorRegExp(QStringLiteral("^operator "));
    Q_ASSERT(operatorRegExp.isValid());
    QString castTo = metaFunction->name().remove(operatorRegExp).trimmed();

    if (castTo.endsWith(u'&'))
        castTo.chop(1);
    if (castTo.startsWith(u"const "))
        castTo.remove(0, 6);

    TypeEntryPtr retType = types->findType(castTo);
    if (!retType)
        return;

    AbstractMetaType metaType(retType);
    metaType.decideUsagePattern();
    metaFunction->setType(metaType);
}

AbstractMetaFunctionRawPtrList
    AbstractMetaBuilderPrivate::classFunctionList(const ScopeModelItem &scopeItem,
                                                  AbstractMetaClass::Attributes *constructorAttributes,
                                                  const AbstractMetaClassPtr &currentClass)
{
    *constructorAttributes = {};
    AbstractMetaFunctionRawPtrList result;
    const FunctionList &scopeFunctionList = scopeItem->functions();
    result.reserve(scopeFunctionList.size());
    const bool isNamespace = currentClass->isNamespace();
    for (const FunctionModelItem &function : scopeFunctionList) {
        if (isNamespace && function->isOperator()) {
            traverseOperatorFunction(function, currentClass);
        } else if (function->isSpaceshipOperator() && !function->isDeleted()) {
            if (currentClass)
                AbstractMetaClass::addSynthesizedComparisonOperators(currentClass);
        } else if (auto *metaFunction = traverseFunction(function, currentClass)) {
            result.append(metaFunction);
        } else if (!function->isDeleted() && function->functionType() == CodeModel::Constructor) {
            auto arguments = function->arguments();
            *constructorAttributes |= AbstractMetaClass::HasRejectedConstructor;
            if (arguments.isEmpty() || arguments.constFirst()->defaultValue())
                *constructorAttributes |= AbstractMetaClass::HasRejectedDefaultConstructor;
        }
    }
    return result;
}

void AbstractMetaBuilderPrivate::traverseFunctions(ScopeModelItem scopeItem,
                                                   const AbstractMetaClassPtr &metaClass)
{
    AbstractMetaClass::Attributes constructorAttributes;
    const AbstractMetaFunctionRawPtrList functions =
        classFunctionList(scopeItem, &constructorAttributes, metaClass);
    metaClass->setAttributes(metaClass->attributes() | constructorAttributes);

    for (AbstractMetaFunction *metaFunction : functions) {
        if (metaClass->isNamespace())
            *metaFunction += AbstractMetaFunction::Static;

        const auto propertyFunction = metaClass->searchPropertyFunction(metaFunction->name());
        if (propertyFunction.index >= 0) {
           QPropertySpec prop = metaClass->propertySpecs().at(propertyFunction.index);
            switch (propertyFunction.function) {
            case AbstractMetaClass::PropertyFunction::Read:
                // Property reader must be in the form "<type> name()"
                if (!metaFunction->isSignal()
                    && prop.typeEntry() == metaFunction->type().typeEntry()
                    && metaFunction->arguments().isEmpty()) {
                    *metaFunction += AbstractMetaFunction::PropertyReader;
                    metaFunction->setPropertySpecIndex(propertyFunction.index);
                }
                break;
            case AbstractMetaClass::PropertyFunction::Write:
                // Property setter must be in the form "void name(<type>)"
                // Make sure the function was created with all arguments; some
                // argument can be missing during the parsing because of errors
                // in the typesystem.
                if (metaFunction->isVoid() && metaFunction->arguments().size() == 1
                    && (prop.typeEntry() == metaFunction->arguments().at(0).type().typeEntry())) {
                    *metaFunction += AbstractMetaFunction::PropertyWriter;
                    metaFunction->setPropertySpecIndex(propertyFunction.index);
                }
                break;
            case AbstractMetaClass::PropertyFunction::Reset:
                // Property resetter must be in the form "void name()"
                if (metaFunction->isVoid() && metaFunction->arguments().isEmpty()) {
                    *metaFunction += AbstractMetaFunction::PropertyResetter;
                    metaFunction->setPropertySpecIndex(propertyFunction.index);
                }
                break;
            case AbstractMetaClass::PropertyFunction::Notify:
                if (metaFunction->isSignal()) {
                    *metaFunction += AbstractMetaFunction::PropertyNotify;
                    metaFunction->setPropertySpecIndex(propertyFunction.index);
                }
            }
        }

        const bool isInvalidDestructor = metaFunction->isDestructor() && metaFunction->isPrivate();
        const bool isInvalidConstructor = metaFunction->functionType() == AbstractMetaFunction::ConstructorFunction
            && metaFunction->isPrivate();
        if (isInvalidConstructor)
            metaClass->setHasPrivateConstructor(true);
        if ((isInvalidDestructor || isInvalidConstructor)
            && !metaClass->hasNonPrivateConstructor()) {
            *metaClass += AbstractMetaClass::FinalInTargetLang;
        } else if (metaFunction->isConstructor() && !metaFunction->isPrivate()) {
            *metaClass -= AbstractMetaClass::FinalInTargetLang;
            metaClass->setHasNonPrivateConstructor(true);
        }

        if (!metaFunction->isDestructor()
            && !(metaFunction->isPrivate() && metaFunction->functionType() == AbstractMetaFunction::ConstructorFunction)) {

            if (metaFunction->isSignal() && metaClass->hasSignal(metaFunction))
                qCWarning(lcShiboken, "%s", qPrintable(msgSignalOverloaded(metaClass, metaFunction)));

            if (metaFunction->isConversionOperator())
                fixReturnTypeOfConversionOperator(metaFunction);

            AbstractMetaClass::addFunction(metaClass, AbstractMetaFunctionCPtr(metaFunction));
            applyFunctionModifications(metaFunction);
        } else if (metaFunction->isDestructor()) {
            metaClass->setHasPrivateDestructor(metaFunction->isPrivate());
            metaClass->setHasProtectedDestructor(metaFunction->isProtected());
            metaClass->setHasVirtualDestructor(metaFunction->isVirtual());
        }
        if (!metaFunction->ownerClass()) {
            delete metaFunction;
            metaFunction = nullptr;
        }
    }

    fillAddedFunctions(metaClass);
}

void AbstractMetaBuilderPrivate::fillAddedFunctions(const AbstractMetaClassPtr &metaClass)
{
    // Add the functions added by the typesystem
    QString errorMessage;
    const AddedFunctionList &addedFunctions = metaClass->typeEntry()->addedFunctions();
    for (const AddedFunctionPtr &addedFunc : addedFunctions) {
        if (!traverseAddedMemberFunction(addedFunc, metaClass, &errorMessage))
            throw Exception(qPrintable(errorMessage));
    }
}

QString AbstractMetaBuilder::getSnakeCaseName(const QString &name)
{
    const auto size = name.size();
    if (size < 3)
        return name;
    QString result;
    result.reserve(size + 4);
    for (qsizetype i = 0; i < size; ++i) {
        const QChar c = name.at(i);
        if (c.isUpper()) {
            if (i > 0) {
                if (name.at(i - 1).isUpper())
                    return name; // Give up at consecutive upper chars
                 result.append(u'_');
            }
            result.append(c.toLower());
        } else {
            result.append(c);
        }
    }
    return result;
}

// Names under which an item will be registered to Python depending on snakeCase
QStringList AbstractMetaBuilder::definitionNames(const QString &name,
                                                 TypeSystem::SnakeCase snakeCase)
{
    QStringList result;
    switch (snakeCase) {
    case TypeSystem::SnakeCase::Unspecified:
    case TypeSystem::SnakeCase::Disabled:
        result.append(name);
        break;
    case TypeSystem::SnakeCase::Enabled:
        result.append(AbstractMetaBuilder::getSnakeCaseName(name));
        break;
    case TypeSystem::SnakeCase::Both:
        result.append(AbstractMetaBuilder::getSnakeCaseName(name));
        if (name != result.constFirst())
            result.append(name);
        break;
    }
    return result;
}

void AbstractMetaBuilderPrivate::applyFunctionModifications(AbstractMetaFunction *func)
{
    AbstractMetaFunction& funcRef = *func;
    for (const FunctionModification &mod : func->modifications(func->implementingClass())) {
        if (mod.isRenameModifier()) {
            func->setOriginalName(func->name());
            func->setName(mod.renamedToName());
        } else if (mod.isAccessModifier()) {
            funcRef -= AbstractMetaFunction::Friendly;

            if (mod.isPublic())
                funcRef.modifyAccess(Access::Public);
            else if (mod.isProtected())
                funcRef.modifyAccess(Access::Protected);
            else if (mod.isPrivate())
                funcRef.modifyAccess(Access::Private);
            else if (mod.isFriendly())
                funcRef += AbstractMetaFunction::Friendly;
        }

        if (mod.isFinal())
            funcRef += AbstractMetaFunction::FinalInTargetLang;
        else if (mod.isNonFinal())
            funcRef -= AbstractMetaFunction::FinalInTargetLang;
    }
}

bool AbstractMetaBuilderPrivate::setupInheritance(const AbstractMetaClassPtr &metaClass)
{
    if (metaClass->inheritanceDone())
        return true;

    metaClass->setInheritanceDone(true);

    QStringList baseClasses = metaClass->baseClassNames();

    // we only support our own containers and ONLY if there is only one baseclass
    if (baseClasses.size() == 1 && baseClasses.constFirst().contains(u'<')) {
        TypeInfo info;
        ComplexTypeEntryPtr baseContainerType;
        const auto templ = findTemplateClass(baseClasses.constFirst(), metaClass,
                                             &info, &baseContainerType);
        if (templ) {
            setupInheritance(templ);
            inheritTemplate(metaClass, templ, info);
            metaClass->typeEntry()->setBaseContainerType(templ->typeEntry());
            return true;
        }

        if (baseContainerType) {
            // Container types are not necessarily wrapped as 'real' classes,
            // but there may still be classes derived from them. In such case,
            // we still need to set the base container type in order to
            // generate correct code for type conversion checking.
            //
            // Additionally, we consider this case as successfully setting up
            // inheritance.
            metaClass->typeEntry()->setBaseContainerType(baseContainerType);
            return true;
        }

        qCWarning(lcShiboken).noquote().nospace()
            << QStringLiteral("template baseclass '%1' of '%2' is not known")
                              .arg(baseClasses.constFirst(), metaClass->name());
        return false;
    }

    auto *types = TypeDatabase::instance();

    for (const auto &baseClassName : baseClasses) {
        if (!types->isClassRejected(baseClassName)) {
            auto typeEntry = types->findType(baseClassName);
            if (typeEntry == nullptr || !typeEntry->isComplex()) {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgBaseNotInTypeSystem(metaClass, baseClassName)));
                return false;
            }
            auto baseClass = AbstractMetaClass::findClass(m_metaClasses, typeEntry);
            if (!baseClass) {
                qCWarning(lcShiboken, "%s",
                          qPrintable(msgUnknownBase(metaClass, baseClassName)));
                return false;
            }
            metaClass->addBaseClass(baseClass);

            setupInheritance(baseClass);
        }
    }

    // Super class set by attribute "default-superclass".
    const QString defaultSuperclassName = metaClass->typeEntry()->defaultSuperclass();
    if (!defaultSuperclassName.isEmpty()) {
        auto defaultSuper = AbstractMetaClass::findClass(m_metaClasses, defaultSuperclassName);
        if (defaultSuper != nullptr) {
            metaClass->setDefaultSuperclass(defaultSuper);
        } else {
            QString message;
            QTextStream(&message) << "Class \"" << defaultSuperclassName
                << "\" specified as \"default-superclass\" of \"" << metaClass->name()
                << "\" could not be found in the code model.";
            qCWarning(lcShiboken, "%s", qPrintable(message));
        }
    }

    return true;
}

void AbstractMetaBuilderPrivate::traverseEnums(const ScopeModelItem &scopeItem,
                                               const AbstractMetaClassPtr &metaClass,
                                               const QStringList &enumsDeclarations)
{
    const EnumList &enums = scopeItem->enums();
    const QSet<QString> enumsDeclarationSet(enumsDeclarations.cbegin(), enumsDeclarations.cend());
    for (const EnumModelItem &enumItem : enums) {
        auto metaEnum = traverseEnum(enumItem, metaClass, enumsDeclarationSet);
        if (metaEnum.has_value()) {
            metaClass->addEnum(metaEnum.value());
        }
    }
}

static void applyDefaultExpressionModifications(const FunctionModificationList &functionMods,
                                                int i, AbstractMetaArgument *metaArg)
{
    // use replace/remove-default-expression for set default value
    for (const auto &modification : functionMods) {
        for (const auto &argumentModification : modification.argument_mods()) {
            if (argumentModification.index() == i + 1) {
                if (argumentModification.removedDefaultExpression()) {
                    metaArg->setDefaultValueExpression(QString());
                    break;
                }
                if (!argumentModification.replacedDefaultExpression().isEmpty()) {
                    metaArg->setDefaultValueExpression(argumentModification.replacedDefaultExpression());
                    break;
                }
            }
        }
    }
}

static AbstractMetaFunction::FunctionType functionTypeFromName(const QString &);

bool AbstractMetaBuilderPrivate::traverseAddedGlobalFunction(const AddedFunctionPtr &addedFunc,
                                                             QString *errorMessage)
{
    AbstractMetaFunction *metaFunction =
        traverseAddedFunctionHelper(addedFunc, nullptr, errorMessage);
    if (metaFunction == nullptr)
        return false;
    m_globalFunctions << AbstractMetaFunctionCPtr(metaFunction);
    return true;
}

AbstractMetaFunction *
    AbstractMetaBuilderPrivate::traverseAddedFunctionHelper(const AddedFunctionPtr &addedFunc,
                                                            const AbstractMetaClassPtr &metaClass /* = {} */,
                                                            QString *errorMessage)
{
    auto returnType = translateType(addedFunc->returnType(), metaClass, {}, errorMessage);
    if (!returnType.has_value()) {
        *errorMessage =
            msgAddedFunctionInvalidReturnType(addedFunc->name(),
                                              addedFunc->returnType().qualifiedName(),
                                              *errorMessage, metaClass);
        return nullptr;
    }

    auto *metaFunction = new AbstractMetaFunction(addedFunc);
    metaFunction->setType(returnType.value());
    metaFunction->setFunctionType(functionTypeFromName(addedFunc->name()));

    const auto &args = addedFunc->arguments();

    qsizetype argCount = args.size();
    // Check "foo(void)"
    if (argCount == 1 && args.constFirst().typeInfo.isVoid())
        argCount = 0;
    for (qsizetype i = 0; i < argCount; ++i) {
        const AddedFunction::Argument &arg = args.at(i);
        auto type = translateType(arg.typeInfo, metaClass, {}, errorMessage);
        if (Q_UNLIKELY(!type.has_value())) {
            *errorMessage =
                msgAddedFunctionInvalidArgType(addedFunc->name(),
                                               arg.typeInfo.qualifiedName(), i + 1,
                                               *errorMessage, metaClass);
            delete metaFunction;
            return nullptr;
        }
        type->decideUsagePattern();

        AbstractMetaArgument metaArg;
        if (!args.at(i).name.isEmpty())
            metaArg.setName(args.at(i).name);
        metaArg.setType(type.value());
        metaArg.setArgumentIndex(i);
        metaArg.setDefaultValueExpression(arg.defaultValue);
        metaArg.setOriginalDefaultValueExpression(arg.defaultValue);
        metaFunction->addArgument(metaArg);
    }

    AbstractMetaArgumentList &metaArguments = metaFunction->arguments();

    if (metaFunction->isOperatorOverload() && !metaFunction->isCallOperator()) {
        if (metaArguments.size() > 2) {
            qCWarning(lcShiboken) << "An operator overload need to have 0, 1 or 2 arguments if it's reverse.";
        } else if (metaArguments.size() == 2) {
            // Check if it's a reverse operator
            if (metaArguments[1].type().typeEntry() == metaClass->typeEntry()) {
                metaFunction->setReverseOperator(true);
                // we need to call these two function to cache the old signature (with two args)
                // we do this buggy behaviour to comply with the original apiextractor buggy behaviour.
                metaFunction->signature();
                metaFunction->minimalSignature();
                metaArguments.removeLast();
                metaFunction->setArguments(metaArguments);
            } else {
                qCWarning(lcShiboken) << "Operator overload can have two arguments only if it's a reverse operator!";
            }
        }
    }


    // Find the correct default values
    const FunctionModificationList functionMods = metaFunction->modifications(metaClass);
    for (qsizetype i = 0; i < metaArguments.size(); ++i) {
        AbstractMetaArgument &metaArg = metaArguments[i];

        // use replace-default-expression for set default value
        applyDefaultExpressionModifications(functionMods, i, &metaArg);
        metaArg.setOriginalDefaultValueExpression(metaArg.defaultValueExpression()); // appear unmodified
    }

    if (!metaArguments.isEmpty())
        fixArgumentNames(metaFunction, metaFunction->modifications(metaClass));

    return metaFunction;
}

bool AbstractMetaBuilderPrivate::traverseAddedMemberFunction(const AddedFunctionPtr &addedFunc,
                                                             const AbstractMetaClassPtr &metaClass,
                                                             QString *errorMessage)
{
    AbstractMetaFunction *metaFunction =
        traverseAddedFunctionHelper(addedFunc, metaClass, errorMessage);
    if (metaFunction == nullptr)
        return false;

    const AbstractMetaArgumentList fargs = metaFunction->arguments();
    if (metaClass->isNamespace())
        *metaFunction += AbstractMetaFunction::Static;
    if (metaFunction->name() == metaClass->name()) {
        metaFunction->setFunctionType(AbstractMetaFunction::ConstructorFunction);
        if (fargs.size() == 1) {
            const auto te = fargs.constFirst().type().typeEntry();
            if (te->isCustom())
                metaFunction->setExplicit(true);
            if (te->name() == metaFunction->name())
                metaFunction->setFunctionType(AbstractMetaFunction::CopyConstructorFunction);
        }
    }

    metaFunction->setDeclaringClass(metaClass);
    metaFunction->setImplementingClass(metaClass);
    AbstractMetaClass::addFunction(metaClass, AbstractMetaFunctionCPtr(metaFunction));
    metaClass->setHasNonPrivateConstructor(true);
    return true;
}

void AbstractMetaBuilderPrivate::fixArgumentNames(AbstractMetaFunction *func, const FunctionModificationList &mods)
{
    AbstractMetaArgumentList &arguments = func->arguments();

    for (const FunctionModification &mod : mods) {
        for (const ArgumentModification &argMod : mod.argument_mods()) {
            if (!argMod.renamedToName().isEmpty())
                arguments[argMod.index() - 1].setName(argMod.renamedToName(), false);
        }
    }

    for (qsizetype i = 0, size = arguments.size(); i < size; ++i) {
        if (arguments.at(i).name().isEmpty())
            arguments[i].setName(u"arg__"_s + QString::number(i + 1), false);
    }
}

static QString functionSignature(const FunctionModelItem &functionItem)
{
    QStringList args;
    const ArgumentList &arguments = functionItem->arguments();
    for (const ArgumentModelItem &arg : arguments)
        args << arg->type().toString();
    return functionItem->name() + u'(' + args.join(u',') + u')';
}

static inline QString qualifiedFunctionSignatureWithType(const FunctionModelItem &functionItem,
                                                         const QString &className = QString())
{
    QString result = functionItem->type().toString() + u' ';
    if (!className.isEmpty())
        result += className + u"::"_s;
    result += functionSignature(functionItem);
    return result;
}
static inline AbstractMetaFunction::FunctionType functionTypeFromCodeModel(CodeModel::FunctionType ft)
{
    AbstractMetaFunction::FunctionType result = AbstractMetaFunction::NormalFunction;
    switch (ft) {
    case CodeModel::Constructor:
        result = AbstractMetaFunction::ConstructorFunction;
        break;
    case CodeModel::CopyConstructor:
        result = AbstractMetaFunction::CopyConstructorFunction;
        break;
    case CodeModel::MoveConstructor:
        result = AbstractMetaFunction::MoveConstructorFunction;
        break;
    case CodeModel::Destructor:
        result = AbstractMetaFunction::DestructorFunction;
        break;
    case CodeModel::AssignmentOperator:
        result = AbstractMetaFunction::AssignmentOperatorFunction;
        break;
    case CodeModel::CallOperator:
        result = AbstractMetaFunction::CallOperator;
        break;
    case CodeModel::ConversionOperator:
        result = AbstractMetaFunction::ConversionOperator;
        break;
    case CodeModel::DereferenceOperator:
        result = AbstractMetaFunction::DereferenceOperator;
        break;
    case CodeModel::ReferenceOperator:
        result = AbstractMetaFunction::ReferenceOperator;
        break;
    case CodeModel::ArrowOperator:
        result = AbstractMetaFunction::ArrowOperator;
        break;
    case CodeModel::ArithmeticOperator:
        result = AbstractMetaFunction::ArithmeticOperator;
        break;
    case CodeModel::IncrementOperator:
        result = AbstractMetaFunction::IncrementOperator;
        break;
    case CodeModel::DecrementOperator:
        result = AbstractMetaFunction::DecrementOperator;
        break;
    case CodeModel::BitwiseOperator:
        result = AbstractMetaFunction::BitwiseOperator;
        break;
    case CodeModel::LogicalOperator:
        result = AbstractMetaFunction::LogicalOperator;
        break;
    case CodeModel::ShiftOperator:
        result = AbstractMetaFunction::ShiftOperator;
        break;
    case CodeModel::SubscriptOperator:
        result = AbstractMetaFunction::SubscriptOperator;
        break;
    case CodeModel::ComparisonOperator:
        result = AbstractMetaFunction::ComparisonOperator;
        break;
    case CodeModel::Normal:
        break;
    case CodeModel::Signal:
        result = AbstractMetaFunction::SignalFunction;
        break;
    case CodeModel::Slot:
        result = AbstractMetaFunction::SlotFunction;
        break;
    }
    return result;
}

static AbstractMetaFunction::FunctionType functionTypeFromName(const QString &name)
{
    if (name == u"__getattro__")
        return AbstractMetaFunction::GetAttroFunction;
    if (name == u"__setattro__")
        return AbstractMetaFunction::SetAttroFunction;
    const auto typeOpt = _FunctionModelItem::functionTypeFromName(name);
    if (typeOpt.has_value())
        return functionTypeFromCodeModel(typeOpt.value());
    return AbstractMetaFunction::NormalFunction;
}

// Apply the <array> modifications of the arguments
static bool applyArrayArgumentModifications(const FunctionModificationList &functionMods,
                                            AbstractMetaFunction *func,
                                            QString *errorMessage)
{
    for (const FunctionModification &mod : functionMods) {
        for (const ArgumentModification &argMod : mod.argument_mods()) {
            if (argMod.isArray()) {
                const int i = argMod.index() - 1;
                if (i < 0 || i >= func->arguments().size()) {
                    *errorMessage = msgCannotSetArrayUsage(func->minimalSignature(), i,
                                                           u"Index out of range."_s);
                    return false;
                }
                auto t = func->arguments().at(i).type();
                if (!t.applyArrayModification(errorMessage)) {
                    *errorMessage = msgCannotSetArrayUsage(func->minimalSignature(), i, *errorMessage);
                    return false;
                }
                func->arguments()[i].setType(t);
            }
        }
    }
    return true;
}

// Create the meta type for a view (std::string_view -> std::string)
static AbstractMetaType createViewOnType(const AbstractMetaType &metaType,
                                         const TypeEntryCPtr &viewOnTypeEntry)
{
    auto result = metaType;
    result.setTypeEntry(viewOnTypeEntry);
    if (!metaType.isContainer() || !viewOnTypeEntry->isContainer())
        return result;
    // For containers, when sth with several template parameters
    // (std::span<T, int N>) is mapped onto a std::vector<T>,
    // remove the superfluous template parameters and strip 'const'.
    const auto vcte = std::static_pointer_cast<const ContainerTypeEntry>(viewOnTypeEntry);
    const auto instantiations = metaType.instantiations();
    AbstractMetaTypeList viewInstantiations;
    const auto size = std::min(vcte->templateParameterCount(), instantiations.size());
    for (qsizetype i = 0; i < size; ++i) {
        auto ins = instantiations.at(i);
        ins.setConstant(false);
        viewInstantiations.append(ins);
    }
    result.setInstantiations(viewInstantiations);
    return result;
}

void AbstractMetaBuilderPrivate::rejectFunction(const FunctionModelItem &functionItem,
                                                const AbstractMetaClassPtr &currentClass,
                                                AbstractMetaBuilder::RejectReason reason,
                                                const QString &rejectReason)
{
    QString sortKey;
    if (currentClass)
        sortKey += currentClass->typeEntry()->qualifiedCppName() + u"::"_s;
    sortKey += functionSignature(functionItem); // Sort without return type
    const QString signatureWithType = functionItem->type().toString() + u' ' + sortKey;
    m_rejectedFunctions.insert({reason, signatureWithType, sortKey, rejectReason});
}

AbstractMetaFunction *AbstractMetaBuilderPrivate::traverseFunction(const FunctionModelItem &functionItem,
                                                                   const AbstractMetaClassPtr &currentClass)
{
    const auto *tdb = TypeDatabase::instance();

    if (!functionItem->templateParameters().isEmpty())
        return nullptr;

    if (functionItem->isDeleted()) {
        switch (functionItem->functionType()) {
        case CodeModel::Constructor:
            if (functionItem->isDefaultConstructor())
                currentClass->setHasDeletedDefaultConstructor(true);
            break;
        case CodeModel::CopyConstructor:
            currentClass->setHasDeletedCopyConstructor(true);
            break;
        default:
            break;
        }
        return nullptr;
    }
    const QString &functionName = functionItem->name();
    const QString className = currentClass != nullptr ?
        currentClass->typeEntry()->qualifiedCppName() : QString{};

    if (m_apiExtractorFlags.testFlag(ApiExtractorFlag::UsePySideExtensions)) {
        // Skip enum helpers generated by Q_ENUM
        if ((currentClass == nullptr || currentClass->isNamespace())
            && (functionName == u"qt_getEnumMetaObject" || functionName == u"qt_getEnumName")) {
                return nullptr;
            }

        // Clang: Skip qt_metacast(), qt_metacall(), expanded from Q_OBJECT
        // and overridden metaObject(), QGADGET helpers
        if (currentClass != nullptr) {
            if (functionName == u"qt_check_for_QGADGET_macro"
                || functionName.startsWith(u"qt_meta")) {
                return nullptr;
            }
            if (functionName == u"metaObject" && className != u"QObject")
                return nullptr;
        }
    } // PySide extensions

    QString rejectReason;
    if (tdb->isFunctionRejected(className, functionName, &rejectReason)) {
        rejectFunction(functionItem, currentClass,
                       AbstractMetaBuilder::GenerationDisabled, rejectReason);
        return nullptr;
    }

    const QString &signature = functionSignature(functionItem);
    if (tdb->isFunctionRejected(className, signature, &rejectReason)) {
        rejectFunction(functionItem, currentClass,
                       AbstractMetaBuilder::GenerationDisabled, rejectReason);
        if (ReportHandler::isDebug(ReportHandler::MediumDebug)) {
            qCInfo(lcShiboken, "%s::%s was rejected by the type database (%s).",
                   qPrintable(className), qPrintable(signature), qPrintable(rejectReason));
        }
        return nullptr;
    }

    if (functionItem->isFriend())
        return nullptr;

    const bool deprecated = functionItem->isDeprecated();
    if (deprecated && m_skipDeprecated) {
        rejectFunction(functionItem, currentClass,
                       AbstractMetaBuilder::GenerationDisabled, u" is deprecated."_s);
        return nullptr;
    }

    AbstractMetaFunction::Flags flags;
    auto *metaFunction = new AbstractMetaFunction(functionName);
    const QByteArray cSignature = signature.toUtf8();
    const QString unresolvedSignature =
        QString::fromUtf8(QMetaObject::normalizedSignature(cSignature.constData()));
    metaFunction->setUnresolvedSignature(unresolvedSignature);
    if (functionItem->isHiddenFriend())
        flags.setFlag(AbstractMetaFunction::Flag::HiddenFriend);
    metaFunction->setSourceLocation(functionItem->sourceLocation());
    if (deprecated)
        *metaFunction += AbstractMetaFunction::Deprecated;

    // Additional check for assignment/move assignment down below
    metaFunction->setFunctionType(functionTypeFromCodeModel(functionItem->functionType()));
    metaFunction->setConstant(functionItem->isConstant());
    metaFunction->setExceptionSpecification(functionItem->exceptionSpecification());

    if (functionItem->isAbstract())
        *metaFunction += AbstractMetaFunction::Abstract;

    if (functionItem->isVirtual()) {
        *metaFunction += AbstractMetaFunction::VirtualCppMethod;
        if (functionItem->isOverride())
            *metaFunction += AbstractMetaFunction::OverriddenCppMethod;
        if (functionItem->isFinal())
            *metaFunction += AbstractMetaFunction::FinalCppMethod;
    } else {
        *metaFunction += AbstractMetaFunction::FinalInTargetLang;
    }

    if (functionItem->isInvokable())
        *metaFunction += AbstractMetaFunction::Invokable;

    if (functionItem->isStatic()) {
        *metaFunction += AbstractMetaFunction::Static;
        *metaFunction += AbstractMetaFunction::FinalInTargetLang;
    }

    // Access rights
    metaFunction->setAccess(functionItem->accessPolicy());

    QString errorMessage;
    switch (metaFunction->functionType()) {
    case AbstractMetaFunction::DestructorFunction:
         metaFunction->setType(AbstractMetaType::createVoid());
        break;
    case AbstractMetaFunction::ConstructorFunction:
        metaFunction->setExplicit(functionItem->isExplicit());
        metaFunction->setName(currentClass->name());
        metaFunction->setType(AbstractMetaType::createVoid());
        break;
    default: {
        TypeInfo returnType = functionItem->type();

        if (tdb->isReturnTypeRejected(className, returnType.toString(), &rejectReason)) {
            rejectFunction(functionItem, currentClass,
                           AbstractMetaBuilder::GenerationDisabled, rejectReason);
            delete metaFunction;
            return nullptr;
        }

        TranslateTypeFlags flags;
        if (functionItem->scopeResolution())
            flags.setFlag(AbstractMetaBuilder::NoClassScopeLookup);
        auto type = translateType(returnType, currentClass, flags, &errorMessage);
        if (!type.has_value()) {
            const QString reason = msgUnmatchedReturnType(functionItem, errorMessage);
            const QString signature = qualifiedFunctionSignatureWithType(functionItem, className);
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgSkippingFunction(functionItem, signature, reason)));
            rejectFunction(functionItem, currentClass,
                           AbstractMetaBuilder::UnmatchedReturnType, reason);
            delete metaFunction;
            return nullptr;
        }

        metaFunction->setType(type.value());
    }
        break;
    }

    ArgumentList arguments = functionItem->arguments();
    // Add private signals for documentation purposes
    if (!arguments.isEmpty()
        && m_apiExtractorFlags.testFlag(ApiExtractorFlag::UsePySideExtensions)
        && functionItem->functionType() == CodeModel::Signal
        && arguments.constLast()->type().qualifiedName().constLast() == u"QPrivateSignal") {
        flags.setFlag(AbstractMetaFunction::Flag::PrivateSignal);
        arguments.removeLast();
    }

    if (arguments.size() == 1) {
        ArgumentModelItem arg = arguments.at(0);
        TypeInfo type = arg->type();
        if (type.qualifiedName().constFirst() == u"void" && type.indirections() == 0)
            arguments.pop_front();
    }

    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const ArgumentModelItem &arg = arguments.at(i);

        if (tdb->isArgumentTypeRejected(className, arg->type().toString(), &rejectReason)) {
            rejectFunction(functionItem, currentClass,
                           AbstractMetaBuilder::GenerationDisabled, rejectReason);
            delete metaFunction;
            return nullptr;
        }

        TranslateTypeFlags flags;
        if (arg->scopeResolution())
            flags.setFlag(AbstractMetaBuilder::NoClassScopeLookup);
        auto metaTypeO = translateType(arg->type(), currentClass, flags, &errorMessage);
        if (!metaTypeO.has_value()) {
            // If an invalid argument has a default value, simply remove it
            // unless the function is virtual (since the override in the
            // wrapper can then not correctly be generated).
            if (arg->defaultValue() && !functionItem->isVirtual()) {
                if (!currentClass || currentClass->typeEntry()->generateCode()) {
                    const QString signature = qualifiedFunctionSignatureWithType(functionItem, className);
                    qCWarning(lcShiboken, "%s",
                              qPrintable(msgStrippingArgument(functionItem, i, signature, arg)));
                }
                break;
            }
            const QString reason = msgUnmatchedParameterType(arg, i, errorMessage);
            const QString signature = qualifiedFunctionSignatureWithType(functionItem, className);
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgSkippingFunction(functionItem, signature, reason)));
            rejectFunction(functionItem, currentClass,
                           AbstractMetaBuilder::UnmatchedArgumentType, reason);
            delete metaFunction;
            return nullptr;
        }

        auto metaType = metaTypeO.value();
        // Add view substitution for simple view types of function arguments
        // std::string_view -> std::string for foo(std::string_view)
        auto viewOnTypeEntry = metaType.typeEntry()->viewOn();
        if (viewOnTypeEntry != nullptr && metaType.indirections() == 0
            && metaType.arrayElementType() == nullptr
            && (!metaType.hasInstantiations() || metaType.isContainer())) {
            metaType.setViewOn(createViewOnType(metaType, viewOnTypeEntry));
        }

        AbstractMetaArgument metaArgument;
        metaArgument.setType(metaType);
        metaArgument.setName(arg->name());
        metaArgument.setArgumentIndex(i);
        metaFunction->addArgument(metaArgument);
    }

    AbstractMetaArgumentList &metaArguments = metaFunction->arguments();

    const FunctionModificationList functionMods = currentClass
        ? AbstractMetaFunction::findClassModifications(metaFunction, currentClass)
        : AbstractMetaFunction::findGlobalModifications(metaFunction);

    for (const FunctionModification &mod : functionMods) {
        if (mod.exceptionHandling() != TypeSystem::ExceptionHandling::Unspecified)
            metaFunction->setExceptionHandlingModification(mod.exceptionHandling());
        if (mod.allowThread() != TypeSystem::AllowThread::Unspecified)
            metaFunction->setAllowThreadModification(mod.allowThread());
    }

    // Find the correct default values
    for (qsizetype i = 0, size = metaArguments.size(); i < size; ++i) {
        const ArgumentModelItem &arg = arguments.at(i);
        AbstractMetaArgument &metaArg = metaArguments[i];

        const QString originalDefaultExpression =
            fixDefaultValue(arg->defaultValueExpression(), metaArg.type(), currentClass);

        metaArg.setOriginalDefaultValueExpression(originalDefaultExpression);
        metaArg.setDefaultValueExpression(originalDefaultExpression);

        applyDefaultExpressionModifications(functionMods, i, &metaArg);

        //Check for missing argument name
        if (!metaArg.defaultValueExpression().isEmpty()
            && !metaArg.hasName()
            && !metaFunction->isOperatorOverload()
            && !metaFunction->isSignal()
            && metaFunction->argumentName(i + 1, false, currentClass).isEmpty()) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgUnnamedArgumentDefaultExpression(currentClass, i + 1,
                                                                     className, metaFunction)));
        }

    }

    if (!metaArguments.isEmpty()) {
        fixArgumentNames(metaFunction, functionMods);
        QString errorMessage;
        if (!applyArrayArgumentModifications(functionMods, metaFunction, &errorMessage)) {
            qCWarning(lcShiboken, "%s",
                      qPrintable(msgArrayModificationFailed(functionItem, className, errorMessage)));
        }
    }

    // Determine class special functions
    if (currentClass && metaFunction->arguments().size() == 1) {
        const AbstractMetaType &argType = metaFunction->arguments().constFirst().type();
        if (argType.typeEntry() == currentClass->typeEntry() && argType.indirections() == 0) {
            if (metaFunction->name() == u"operator=") {
                switch (argType.referenceType()) {
                case NoReference:
                    metaFunction->setFunctionType(AbstractMetaFunction::AssignmentOperatorFunction);
                    break;
                case LValueReference:
                    if (argType.isConstant())
                        metaFunction->setFunctionType(AbstractMetaFunction::AssignmentOperatorFunction);
                    break;
                case RValueReference:
                    metaFunction->setFunctionType(AbstractMetaFunction::MoveAssignmentOperatorFunction);
                    break;
                }
            }
        }
    }
    metaFunction->setFlags(flags);
    return metaFunction;
}

static TypeEntryCPtr findTypeEntryUsingContext(const AbstractMetaClassCPtr &metaClass,
                                               const QString& qualifiedName)
{
    TypeEntryCPtr type;
    QStringList context = metaClass->qualifiedCppName().split(u"::"_s);
    while (!type && !context.isEmpty()) {
        type = TypeDatabase::instance()->findType(context.join(u"::"_s) + u"::"_s + qualifiedName);
        context.removeLast();
    }
    return type;
}

// Helper for findTypeEntries/translateTypeStatic()
TypeEntryCList AbstractMetaBuilderPrivate::findTypeEntriesHelper(const QString &qualifiedName,
                                                                 const QString &name,
                                                                 TranslateTypeFlags flags,
                                                                 const AbstractMetaClassCPtr &currentClass,
                                                                 AbstractMetaBuilderPrivate *d)
{
    // 5.1 - Try first using the current scope
    if (currentClass != nullptr
        && !flags.testFlag(AbstractMetaBuilder::NoClassScopeLookup)) {
        if (auto type = findTypeEntryUsingContext(currentClass, qualifiedName))
            return {type};

        // 5.1.1 - Try using the class parents' scopes
        if (d && !currentClass->baseClassNames().isEmpty()) {
            const auto &baseClasses = d->getBaseClasses(currentClass);
            for (const auto &cls : baseClasses) {
                if (auto type = findTypeEntryUsingContext(cls, qualifiedName))
                    return {type};
            }
        }
    }

    // 5.2 - Try without scope
    auto types = TypeDatabase::instance()->findCppTypes(qualifiedName);
    if (!types.isEmpty())
        return types;

    // 6. No? Try looking it up as a flags type
    if (auto type = TypeDatabase::instance()->findFlagsType(qualifiedName))
        return {type};

    // 7. No? Try looking it up as a container type
    if (auto type = TypeDatabase::instance()->findContainerType(name))
        return {type};

    // 8. No? Check if the current class is a template and this type is one
    //    of the parameters.
    if (currentClass) {
        const auto &template_args = currentClass->templateArguments();
        for (const auto &te : template_args) {
            if (te->name() == qualifiedName)
                return {te};
        }
    }
    return {};
}

// Helper for translateTypeStatic() that calls findTypeEntriesHelper()
// and does some error checking.
TypeEntryCList AbstractMetaBuilderPrivate::findTypeEntries(const QString &qualifiedName,
                                                           const QString &name,
                                                           TranslateTypeFlags flags,
                                                           const AbstractMetaClassCPtr &currentClass,
                                                           AbstractMetaBuilderPrivate *d,
                                                           QString *errorMessage)
{
    TypeEntryCList types = findTypeEntriesHelper(qualifiedName, name, flags,
                                                 currentClass, d);
    if (types.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = msgCannotFindTypeEntry(qualifiedName);
        return {};
    }

    // Resolve entries added by metabuilder (for example, "GLenum") to match
    // the signatures for modifications.
    for (qsizetype i = 0, size = types.size(); i < size; ++i) {
        const auto &e = types.at(i);
        if (e->isPrimitive()) {
            const auto pte = std::static_pointer_cast<const PrimitiveTypeEntry>(e);
            types[i] = basicReferencedNonBuiltinTypeEntry(pte);
        }
    }

    if (types.size() == 1)
        return types;

    const auto typeEntryType = types.constFirst()->type();
    const bool sameType = std::all_of(types.cbegin() + 1, types.cend(),
                                      [typeEntryType](const TypeEntryCPtr &e) {
                                          return e->type() == typeEntryType;
                                      });

    if (!sameType) {
        if (errorMessage != nullptr)
            *errorMessage = msgAmbiguousVaryingTypesFound(qualifiedName, types);
        return {};
    }
    // Ambiguous primitive/smart pointer types are possible (when
    // including type systems).
    if (typeEntryType != TypeEntry::PrimitiveType
        && typeEntryType != TypeEntry::SmartPointerType) {
        if (errorMessage != nullptr)
            *errorMessage = msgAmbiguousTypesFound(qualifiedName, types);
        return {};
    }
    return types;
}

// Reverse lookup of AbstractMetaType representing a template specialization
// found during traversing function arguments to its type system typedef'ed
// class.
AbstractMetaClassCPtr AbstractMetaBuilderPrivate::resolveTypeSystemTypeDef(const AbstractMetaType &t) const
{
    if (t.hasInstantiations()) {
        auto pred = [t](const TypeClassEntry &e) { return e.type.equals(t); };
        auto it = std::find_if(m_typeSystemTypeDefs.cbegin(), m_typeSystemTypeDefs.cend(), pred);
        if (it != m_typeSystemTypeDefs.cend())
            return it->klass;
    }
    return nullptr;
}

// The below helpers and AbstractMetaBuilderPrivate::fixSmartPointers()
// synthesize missing smart pointer functions and classes. For example for
// std::shared_ptr, the full class declaration or base classes from
// internal, compiler-dependent STL implementation headers might not be exposed
// to the parser unless those headers are specified as <system-include>.

static void synthesizeWarning(const AbstractMetaFunctionCPtr &f)
{
    qCWarning(lcShiboken, "Synthesizing \"%s\"...",
              qPrintable(f->classQualifiedSignature()));
}

static AbstractMetaFunctionPtr
    addMethod(const AbstractMetaClassPtr &s, const AbstractMetaType &returnType,
              const QString &name, bool isConst = true)
{
    auto function = std::make_shared<AbstractMetaFunction>(name);
    function->setType(returnType);
    AbstractMetaClass::addFunction(s, function);
    function->setConstant(isConst);
    synthesizeWarning(function);
    return function;
}

static AbstractMetaFunctionPtr
    addMethod(const AbstractMetaClassPtr &s, const QString &returnTypeName,
              const QString &name, bool isConst = true)
{
    auto typeEntry = TypeDatabase::instance()->findPrimitiveType(returnTypeName);
    Q_ASSERT(typeEntry);
    AbstractMetaType returnType(typeEntry);
    returnType.decideUsagePattern();
    return addMethod(s, returnType, name, isConst);
}

// Create the instantiation type of a smart pointer
static AbstractMetaType instantiationType(const AbstractMetaClassCPtr &s,
                                          const SmartPointerTypeEntryCPtr &ste)
{
    AbstractMetaType type(s->templateArguments().constFirst());
    if (ste->smartPointerType() != TypeSystem::SmartPointerType::ValueHandle)
        type.addIndirection();
    type.decideUsagePattern();
    return type;
}

// Create the pointee argument of a smart pointer constructor or reset()
static AbstractMetaArgument pointeeArgument(const AbstractMetaClassCPtr &s,
                                            const SmartPointerTypeEntryCPtr &ste)
{
    AbstractMetaArgument pointee;
    pointee.setType(instantiationType(s, ste));
    pointee.setName(u"pointee"_s);
    return pointee;
}

// Add the smart pointer constructors. For MSVC, (when not specifying
// <system-header>), clang only sees the default constructor.
static void fixSmartPointerConstructors(const AbstractMetaClassPtr &s,
                                        const SmartPointerTypeEntryCPtr &ste)
{
    const auto ctors = s->queryFunctions(FunctionQueryOption::Constructors);
    bool seenDefaultConstructor = false;
    bool seenParameter = false;
    for (const auto &ctor : ctors) {
        if (ctor->arguments().isEmpty())
            seenDefaultConstructor = true;
        else
            seenParameter = true;
    }

    if (!seenParameter) {
        auto constructor = std::make_shared<AbstractMetaFunction>(s->name());
        constructor->setFunctionType(AbstractMetaFunction::ConstructorFunction);
        constructor->addArgument(pointeeArgument(s, ste));
        AbstractMetaClass::addFunction(s, constructor);
        synthesizeWarning(constructor);
    }

    if (!seenDefaultConstructor) {
        auto constructor = std::make_shared<AbstractMetaFunction>(s->name());
        constructor->setFunctionType(AbstractMetaFunction::ConstructorFunction);
        AbstractMetaClass::addFunction(s, constructor);
        synthesizeWarning(constructor);
    }
}

// Similarly, add the smart pointer reset() functions
static void fixSmartPointerReset(const AbstractMetaClassPtr &s,
                                 const SmartPointerTypeEntryCPtr &ste)
{
    const QString resetMethodName = ste->resetMethod();
    const auto functions = s->findFunctions(resetMethodName);
    bool seenParameterLess = false;
    bool seenParameter = false;
    for (const auto &function : functions) {
        if (function->arguments().isEmpty())
            seenParameterLess = true;
        else
            seenParameter = true;
    }

    if (!seenParameter) {
        auto f = std::make_shared<AbstractMetaFunction>(resetMethodName);
        f->addArgument(pointeeArgument(s, ste));
        AbstractMetaClass::addFunction(s, f);
        synthesizeWarning(f);
    }

    if (!seenParameterLess) {
        auto f = std::make_shared<AbstractMetaFunction>(resetMethodName);
        AbstractMetaClass::addFunction(s, f);
        synthesizeWarning(f);
    }
}

// Add the relevant missing smart pointer functions.
static void fixSmartPointerClass(const AbstractMetaClassPtr &s,
                                 const SmartPointerTypeEntryCPtr &ste)
{
    fixSmartPointerConstructors(s, ste);

    if (!ste->resetMethod().isEmpty())
        fixSmartPointerReset(s, ste);

    const QString getterName = ste->getter();
    if (!s->findFunction(getterName))
        addMethod(s, instantiationType(s, ste), getterName);

    const QString refCountName = ste->refCountMethodName();
    if (!refCountName.isEmpty() && !s->findFunction(refCountName))
        addMethod(s, u"int"_s, refCountName);

    const QString valueCheckMethod = ste->valueCheckMethod();
    if (!valueCheckMethod.isEmpty() && !s->findFunction(valueCheckMethod)) {
        auto f = addMethod(s, u"bool"_s, valueCheckMethod);
        if (valueCheckMethod == u"operator bool")
            f->setFunctionType(AbstractMetaFunction::ConversionOperator);
    }

    const QString nullCheckMethod = ste->nullCheckMethod();
    if (!nullCheckMethod.isEmpty() && !s->findFunction(nullCheckMethod))
        addMethod(s, u"bool"_s, nullCheckMethod);
}

// Create a missing smart pointer class
static AbstractMetaClassPtr createSmartPointerClass(const SmartPointerTypeEntryCPtr &ste,
                                                   const AbstractMetaClassList &allClasses)
{
    auto result = std::make_shared<AbstractMetaClass>();
    result->setTypeEntry(std::const_pointer_cast<SmartPointerTypeEntry>(ste));
    auto templateArg = std::make_shared<TemplateArgumentEntry>(u"T"_s, ste->version(),
                                                               typeSystemTypeEntry(ste));
    result->setTemplateArguments({templateArg});
    fixSmartPointerClass(result, ste);
    auto enclosingTe = ste->parent();
    if (!enclosingTe->isTypeSystem()) {
        const auto enclosing = AbstractMetaClass::findClass(allClasses, enclosingTe);
        if (!enclosing)
            throw Exception(msgEnclosingClassNotFound(ste));
        result->setEnclosingClass(enclosing);
        auto inner = enclosing->innerClasses();
        inner.append(std::const_pointer_cast<const AbstractMetaClass>(result));
        enclosing->setInnerClasses(inner);
    }
    return result;
}

void AbstractMetaBuilderPrivate::fixSmartPointers()
{
    const auto smartPointerTypes = TypeDatabase::instance()->smartPointerTypes();
    for (const auto &ste : smartPointerTypes) {
        const auto smartPointerClass =
            AbstractMetaClass::findClass(m_smartPointers, ste);
        if (smartPointerClass) {
            fixSmartPointerClass(std::const_pointer_cast<AbstractMetaClass>(smartPointerClass),
                                 ste);
        } else {
            qCWarning(lcShiboken, "Synthesizing smart pointer \"%s\"...",
                      qPrintable(ste->qualifiedCppName()));
            m_smartPointers.append(createSmartPointerClass(ste, m_metaClasses));
        }
    }
}

std::optional<AbstractMetaType>
    AbstractMetaBuilderPrivate::translateType(const TypeInfo &_typei,
                                              const AbstractMetaClassCPtr &currentClass,
                                              TranslateTypeFlags flags,
                                              QString *errorMessage)
{
    return translateTypeStatic(_typei, currentClass, this, flags, errorMessage);
}

static bool isNumber(const QString &s)
{
    return std::all_of(s.cbegin(), s.cend(),
                       [](QChar c) { return c.isDigit(); });
}

// A type entry relevant only for non type template "X<5>"
static bool isNonTypeTemplateArgument(const TypeEntryCPtr &te)
{
    const auto type = te->type();
    return type == TypeEntry::EnumValue || type == TypeEntry::ConstantValueType;
}

std::optional<AbstractMetaType>
    AbstractMetaBuilderPrivate::translateTypeStatic(const TypeInfo &_typei,
                                                    const AbstractMetaClassCPtr &currentClass,
                                                    AbstractMetaBuilderPrivate *d,
                                                    TranslateTypeFlags flags,
                                                    QString *errorMessageIn)
{
    if (_typei.isVoid())
        return AbstractMetaType::createVoid();

    // 1. Test the type info without resolving typedefs in case this is present in the
    //    type system
    const bool resolveType = !flags.testFlag(AbstractMetaBuilder::DontResolveType);
    if (resolveType) {
        auto resolved =
            translateTypeStatic(_typei, currentClass, d,
                                flags | AbstractMetaBuilder::DontResolveType,
                                errorMessageIn);
        if (resolved.has_value())
            return resolved;
    }

    TypeInfo typeInfo = _typei;
    if (resolveType) {
        // Go through all parts of the current scope (including global namespace)
        // to resolve typedefs. The parser does not properly resolve typedefs in
        // the global scope when they are referenced from inside a namespace.
        // This is a work around to fix this bug since fixing it in resolveType
        // seemed non-trivial
        qsizetype i = d ? d->m_scopes.size() - 1 : -1;
        while (i >= 0) {
            typeInfo = TypeInfo::resolveType(_typei, d->m_scopes.at(i--));
            if (typeInfo.qualifiedName().join(u"::"_s) != _typei.qualifiedName().join(u"::"_s))
                break;
        }

    }

    if (typeInfo.isFunctionPointer()) {
        if (errorMessageIn)
            *errorMessageIn = msgUnableToTranslateType(_typei, u"Unsupported function pointer."_s);
        return {};
    }

    QString errorMessage;

    // 2. Handle arrays.
    // 2.1 Handle char arrays with unspecified size (aka "const char[]") as "const char*" with
    // NativePointerPattern usage.
    bool oneDimensionalArrayOfUnspecifiedSize =
            typeInfo.arrayElements().size() == 1
            && typeInfo.arrayElements().at(0).isEmpty();

    bool isConstCharStarCase =
            oneDimensionalArrayOfUnspecifiedSize
            && typeInfo.qualifiedName().size() == 1
            && typeInfo.qualifiedName().at(0) == QStringLiteral("char")
            && typeInfo.indirections() == 0
            && typeInfo.isConstant()
            && typeInfo.referenceType() == NoReference
            && typeInfo.arguments().isEmpty();

    if (isConstCharStarCase)
        typeInfo.setIndirections(typeInfo.indirections() + typeInfo.arrayElements().size());

    // 2.2 Handle regular arrays.
    if (!typeInfo.arrayElements().isEmpty() && !isConstCharStarCase) {
        TypeInfo newInfo;
        //newInfo.setArguments(typeInfo.arguments());
        newInfo.setIndirectionsV(typeInfo.indirectionsV());
        newInfo.setConstant(typeInfo.isConstant());
        newInfo.setVolatile(typeInfo.isVolatile());
        newInfo.setFunctionPointer(typeInfo.isFunctionPointer());
        newInfo.setQualifiedName(typeInfo.qualifiedName());
        newInfo.setReferenceType(typeInfo.referenceType());
        newInfo.setVolatile(typeInfo.isVolatile());

        auto elementType = translateTypeStatic(newInfo, currentClass, d, flags, &errorMessage);
        if (!elementType.has_value()) {
            if (errorMessageIn) {
                errorMessage.prepend(u"Unable to translate array element: "_s);
                *errorMessageIn = msgUnableToTranslateType(_typei, errorMessage);
            }
            return {};
        }

        for (auto i = typeInfo.arrayElements().size() - 1; i >= 0; --i) {
            AbstractMetaType arrayType;
            arrayType.setArrayElementType(elementType.value());
            const QString &arrayElement = typeInfo.arrayElements().at(i);
            if (!arrayElement.isEmpty()) {
                bool _ok;
                const qint64 elems = d
                    ? d->findOutValueFromString(arrayElement, _ok)
                    : arrayElement.toLongLong(&_ok, 0);
                if (_ok)
                    arrayType.setArrayElementCount(int(elems));
            }
            auto elementTypeEntry = elementType->typeEntry();
            auto at = std::make_shared<ArrayTypeEntry>(elementTypeEntry, elementTypeEntry->version(),
                                                       elementTypeEntry->parent());
            arrayType.setTypeEntry(at);
            arrayType.decideUsagePattern();

            elementType = arrayType;
        }

        return elementType;
    }

    QStringList qualifierList = typeInfo.qualifiedName();
    if (qualifierList.isEmpty()) {
        errorMessage = msgUnableToTranslateType(_typei, u"horribly broken type"_s);
        if (errorMessageIn)
            *errorMessageIn = errorMessage;
        else
            qCWarning(lcShiboken,"%s", qPrintable(errorMessage));
        return {};
    }

    QString qualifiedName = qualifierList.join(u"::"_s);
    QString name = qualifierList.takeLast();

    // 4. Special case QFlags (include instantiation in name)
    if (qualifiedName == u"QFlags") {
        qualifiedName = typeInfo.toString();
        typeInfo.clearInstantiations();
    }

    TypeEntryCList types = findTypeEntries(qualifiedName, name, flags,
                                           currentClass, d, errorMessageIn);
    if (!flags.testFlag(AbstractMetaBuilder::TemplateArgument)) {
        // Avoid clashes between QByteArray and enum value QMetaType::QByteArray
        // unless we are looking for template arguments.
        auto end = std::remove_if(types.begin(), types.end(),
                                  isNonTypeTemplateArgument);
        types.erase(end, types.end());
    }

    if (types.isEmpty()) {
        if (errorMessageIn != nullptr)
            *errorMessageIn = msgUnableToTranslateType(_typei, *errorMessageIn);
        return {};
    }

    TypeEntryCPtr type = types.constFirst();
    const TypeEntry::Type typeEntryType = type->type();

    AbstractMetaType metaType;
    metaType.setIndirectionsV(typeInfo.indirectionsV());
    metaType.setReferenceType(typeInfo.referenceType());
    metaType.setConstant(typeInfo.isConstant());
    metaType.setVolatile(typeInfo.isVolatile());
    metaType.setOriginalTypeDescription(_typei.toString());

    const auto &templateArguments = typeInfo.instantiations();
    for (qsizetype t = 0, size = templateArguments.size(); t < size; ++t) {
        const  TypeInfo &ti = templateArguments.at(t);
        auto targType = translateTypeStatic(ti, currentClass, d,
                                            flags | AbstractMetaBuilder::TemplateArgument,
                                            &errorMessage);
        // For non-type template parameters, create a dummy type entry on the fly
        // as is done for classes.
        if (!targType.has_value()) {
            const QString value = ti.qualifiedName().join(u"::"_s);
            if (isNumber(value)) {
                auto module = typeSystemTypeEntry(type);
                TypeDatabase::instance()->addConstantValueTypeEntry(value, module);
                targType = translateTypeStatic(ti, currentClass, d, flags, &errorMessage);
            }
        }
        if (!targType) {
            if (errorMessageIn)
                *errorMessageIn = msgCannotTranslateTemplateArgument(t, ti, errorMessage);
            return {};
        }

        metaType.addInstantiation(targType.value());
    }

    if (typeEntryType == TypeEntry::SmartPointerType) {
        // Find a matching instantiation
        if (metaType.instantiations().size() != 1) {
            if (errorMessageIn)
                *errorMessageIn = msgInvalidSmartPointerType(_typei);
            return {};
        }
        auto instantiationType = metaType.instantiations().constFirst().typeEntry();
        if (instantiationType->type() == TypeEntry::TemplateArgumentType) {
            // Member functions of the template itself, SharedPtr(const SharedPtr &)
            type = instantiationType;
        } else {
            auto it = std::find_if(types.cbegin(), types.cend(),
                                   [instantiationType](const TypeEntryCPtr &e) {
                auto smartPtr = std::static_pointer_cast<const SmartPointerTypeEntry>(e);
                return smartPtr->matchesInstantiation(instantiationType);
            });
            if (it == types.cend()) {
                if (errorMessageIn)
                    *errorMessageIn = msgCannotFindSmartPointerInstantion(_typei);
                return {};
            }
            type =*it;
        }
    }

    metaType.setTypeEntry(type);

    // The usage pattern *must* be decided *after* the possible template
    // instantiations have been determined, or else the absence of
    // such instantiations will break the caching scheme of
    // AbstractMetaType::cppSignature().
    metaType.decideUsagePattern();

    if (d) {
        // Reverse lookup of type system typedefs. Replace by class.
        if (auto klass = d->resolveTypeSystemTypeDef(metaType)) {
            metaType = AbstractMetaType{};
            metaType.setTypeEntry(klass->typeEntry());
            metaType.decideUsagePattern();
        }
    }

    return metaType;
}

std::optional<AbstractMetaType>
    AbstractMetaBuilder::translateType(const TypeInfo &_typei,
                                       const AbstractMetaClassPtr &currentClass,
                                       TranslateTypeFlags flags,
                                       QString *errorMessage)
{
    return AbstractMetaBuilderPrivate::translateTypeStatic(_typei, currentClass,
                                                           nullptr, flags,
                                                           errorMessage);
}

std::optional<AbstractMetaType>
    AbstractMetaBuilder::translateType(const QString &t,
                                      const AbstractMetaClassPtr &currentClass,
                                      TranslateTypeFlags flags,
                                      QString *errorMessageIn)
{
    QString errorMessage;
    TypeInfo typeInfo = TypeParser::parse(t, &errorMessage);
    if (typeInfo.qualifiedName().isEmpty()) {
        errorMessage = msgUnableToTranslateType(t, errorMessage);
        if (errorMessageIn)
            *errorMessageIn = errorMessage;
        else
            qCWarning(lcShiboken, "%s", qPrintable(errorMessage));
        return {};
    }
    return translateType(typeInfo, currentClass, flags, errorMessageIn);
}

qint64 AbstractMetaBuilderPrivate::findOutValueFromString(const QString &stringValue, bool &ok)
{
    qint64 value = stringValue.toLongLong(&ok);
    if (ok)
        return value;

    if (stringValue == u"true" || stringValue == u"false") {
        ok = true;
        return (stringValue == u"true");
    }

    // This is a very lame way to handle expression evaluation,
    // but it is not critical and will do for the time being.
    static const QRegularExpression variableNameRegExp(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    Q_ASSERT(variableNameRegExp.isValid());
    if (!variableNameRegExp.match(stringValue).hasMatch()) {
        ok = true;
        return 0;
    }

    auto enumValue = AbstractMetaClass::findEnumValue(m_metaClasses, stringValue);
    if (enumValue.has_value()) {
        ok = true;
        return enumValue->value().value();
    }

    for (const AbstractMetaEnum &metaEnum : std::as_const(m_globalEnums)) {
        auto ev = metaEnum.findEnumValue(stringValue);
        if (ev.has_value()) {
            ok = true;
            return ev->value().value();
        }
    }

    ok = false;
    return 0;
}

// Return whether candidate is some underqualified specification of qualifiedType
// ("B::C" should be qualified to "A::B::C")
static bool isUnderQualifiedSpec(QStringView qualifiedType, QStringView candidate)
{
    const auto candidateSize = candidate.size();
    const auto qualifiedTypeSize = qualifiedType.size();
    return candidateSize < qualifiedTypeSize
        && qualifiedType.endsWith(candidate)
        && qualifiedType.at(qualifiedTypeSize - candidateSize - 1) == u':';
}

QString AbstractMetaBuilder::fixEnumDefault(const AbstractMetaType &type,
                                            const QString &expr,
                                            const AbstractMetaClassCPtr &klass) const
{
    return d->fixEnumDefault(type, expr, klass);
}

void AbstractMetaBuilder::setCodeModelTestMode(bool b)
{
    AbstractMetaBuilderPrivate::m_codeModelTestMode = b;
}

// Helper to fix a simple default value (field or enum reference) in a
// class context.
QString AbstractMetaBuilderPrivate::fixSimpleDefaultValue(QStringView expr,
                                                          const AbstractMetaClassCPtr &klass) const
{
    const QString field = qualifyStaticField(klass, expr);

    if (!field.isEmpty())
        return field;
    const auto cit = m_classToItem.constFind(klass);
    if (cit == m_classToItem.cend())
        return {};
    auto *scope = dynamic_cast<const _ScopeModelItem *>(cit.value());
    if (!scope)
        return {};
    if (auto enumValue = scope->findEnumByValue(expr))
        return enumValue.qualifiedName;
    return {};
}

// see TestResolveType::testFixDefaultArguments()
QString AbstractMetaBuilderPrivate::fixDefaultValue(QString expr, const AbstractMetaType &type,
                                                    const AbstractMetaClassCPtr &implementingClass) const
{
    expr.replace(u'\n', u' '); // breaks signature parser

    if (AbstractMetaBuilder::dontFixDefaultValue(expr))
        return expr;

    if (type.isFlags() || type.isEnum()) {
        expr = fixEnumDefault(type, expr, implementingClass);
    } else if (type.isContainer() && expr.contains(u'<')) {
        // Expand a container of a nested class, fex
        // "QList<FormatRange>()" -> "QList<QTextLayout::FormatRange>()"
        if (type.instantiations().size() != 1)
            return expr; // Only simple types are handled, not QMap<int, int>.
        auto innerTypeEntry = type.instantiations().constFirst().typeEntry();
        if (!innerTypeEntry->isComplex())
            return expr;
        const QString &qualifiedInnerTypeName = innerTypeEntry->qualifiedCppName();
        if (!qualifiedInnerTypeName.contains(u"::")) // Nothing to qualify here
            return expr;
        const auto openPos = expr.indexOf(u'<');
        const auto closingPos = expr.lastIndexOf(u'>');
        if (openPos == -1 || closingPos == -1)
            return expr;
        const auto innerPos = openPos + 1;
        const auto innerLen = closingPos - innerPos;
        const auto innerType = QStringView{expr}.mid(innerPos, innerLen).trimmed();
        if (isUnderQualifiedSpec(qualifiedInnerTypeName, innerType))
            expr.replace(innerPos, innerLen, qualifiedInnerTypeName);
    } else {
        // Here the default value is supposed to be a constructor, a class field,
        // a constructor receiving a static class field or an enum. Consider
        // class QSqlDatabase { ...
        //     static const char *defaultConnection;
        //     QSqlDatabase(const QString &connection = QLatin1String(defaultConnection))
        //                  -> = QLatin1String(QSqlDatabase::defaultConnection)
        //     static void foo(QSqlDatabase db = QSqlDatabase(defaultConnection));
        //                     -> = QSqlDatabase(QSqlDatabase::defaultConnection)
        //
        // Enum values from the class as defaults of int and others types (via
        // implicit conversion) are handled here as well:
        // class QStyleOption { ...
        //     enum StyleOptionType { Type = SO_Default };
        //     QStyleOption(..., int type = SO_Default);
        //                  ->  = QStyleOption::StyleOptionType::SO_Default

        // Is this a single field or an enum?
        if (isQualifiedCppIdentifier(expr)) {
            const QString fixed = fixSimpleDefaultValue(expr, implementingClass);
            return fixed.isEmpty() ? expr : fixed;
        }

        // Is this sth like "QLatin1String(field)", "Class(Field)", "Class()"?
        const auto parenPos = expr.indexOf(u'(');
        if (parenPos == -1 || !expr.endsWith(u')'))
            return expr;
        // Is the term within parentheses a class field or enum?
        const auto innerLength = expr.size() - parenPos - 2;
        if (innerLength > 0) { // Not some function call "defaultFunc()"
            const auto inner = QStringView{expr}.mid(parenPos + 1, innerLength);
            if (isQualifiedCppIdentifier(inner)
                && !AbstractMetaBuilder::dontFixDefaultValue(inner)) {
                const QString replacement = fixSimpleDefaultValue(inner, implementingClass);
                if (!replacement.isEmpty() && replacement != inner)
                    expr.replace(parenPos + 1, innerLength, replacement);
            }
        }
        // Is this a class constructor "Class(Field)"? Expand it.
        const auto te = type.typeEntry();
        if (!te->isComplex())
            return expr;
        const QString &qualifiedTypeName = te->qualifiedCppName();
        if (!qualifiedTypeName.contains(u"::")) // Nothing to qualify here
            return expr;
        const auto className = QStringView{expr}.left(parenPos);
        if (isUnderQualifiedSpec(qualifiedTypeName, className))
            expr.replace(0, className.size(), qualifiedTypeName);
    }

    return expr;
}

QString AbstractMetaBuilder::fixDefaultValue(const QString &expr, const AbstractMetaType &type,
                                             const AbstractMetaClassCPtr &c) const
{
    return d->fixDefaultValue(expr, type, c);
}

bool AbstractMetaBuilderPrivate::isEnum(const FileModelItem &dom, const QStringList& qualified_name)
{
    CodeModelItem item = dom->model()->findItem(qualified_name, dom);
    return item && item->kind() == _EnumModelItem::__node_kind;
}

AbstractMetaClassPtr
    AbstractMetaBuilderPrivate::findTemplateClass(const QString &name,
                                                  const AbstractMetaClassCPtr &context,
                                                  TypeInfo *info,
                                                  ComplexTypeEntryPtr *baseContainerType) const
{
    if (baseContainerType)
        baseContainerType->reset();
    auto *types = TypeDatabase::instance();

    QStringList scope = context->typeEntry()->qualifiedCppName().split(u"::"_s);
    QString errorMessage;
    scope.removeLast();
    for (auto i = scope.size(); i >= 0; --i) {
        QString prefix = i > 0 ? QStringList(scope.mid(0, i)).join(u"::"_s) + u"::"_s : QString();
        QString completeName = prefix + name;
        const TypeInfo parsed = TypeParser::parse(completeName, &errorMessage);
        QString qualifiedName = parsed.qualifiedName().join(u"::"_s);
        if (qualifiedName.isEmpty()) {
            qWarning().noquote().nospace() << "Unable to parse type \"" << completeName
                << "\" while looking for template \"" << name << "\": " << errorMessage;
            continue;
        }
        if (info)
            *info = parsed;

        AbstractMetaClassPtr templ;
        for (const auto &c : std::as_const(m_templates)) {
            if (c->typeEntry()->name() == qualifiedName) {
                templ = c;
                break;
            }
        }

        if (!templ)
            templ = AbstractMetaClass::findClass(m_metaClasses, qualifiedName);

        if (templ)
            return templ;

        if (baseContainerType)
            *baseContainerType = types->findContainerType(qualifiedName);
    }

    return nullptr;
}

AbstractMetaClassCList
    AbstractMetaBuilderPrivate::getBaseClasses(const AbstractMetaClassCPtr &metaClass) const
{
    // Shortcut if inheritance has already been set up
    if (metaClass->inheritanceDone() || !metaClass->needsInheritanceSetup())
        return metaClass->baseClasses();
    AbstractMetaClassCList baseClasses;
    const QStringList &baseClassNames = metaClass->baseClassNames();
    for (const QString& parent : baseClassNames) {
        const auto cls = parent.contains(u'<')
            ? findTemplateClass(parent, metaClass)
            : AbstractMetaClass::findClass(m_metaClasses, parent);

        if (cls)
            baseClasses << cls;
    }
    return baseClasses;
}

std::optional<AbstractMetaType>
    AbstractMetaBuilderPrivate::inheritTemplateType(const AbstractMetaTypeList &templateTypes,
                                                    const AbstractMetaType &metaType)
{
    auto returned = metaType;

    if (!metaType.typeEntry()->isTemplateArgument() && !metaType.hasInstantiations())
        return returned;

    returned.setOriginalTemplateType(metaType);

    if (returned.typeEntry()->isTemplateArgument()) {
        const auto tae = std::static_pointer_cast<const TemplateArgumentEntry>(returned.typeEntry());

        // If the template is intantiated with void we special case this as rejecting the functions that use this
        // parameter from the instantiation.
        const AbstractMetaType &templateType = templateTypes.value(tae->ordinal());
        if (templateType.typeEntry()->isVoid())
            return {};

        AbstractMetaType t = returned;
        t.setTypeEntry(templateType.typeEntry());
        t.setIndirections(templateType.indirections() + t.indirections() ? 1 : 0);
        t.decideUsagePattern();

        return inheritTemplateType(templateTypes, t);
    }

    if (returned.hasInstantiations()) {
        AbstractMetaTypeList instantiations = returned.instantiations();
        for (qsizetype i = 0; i < instantiations.size(); ++i) {
            auto ins = inheritTemplateType(templateTypes, instantiations.at(i));
            if (!ins.has_value())
                return {};
            instantiations[i] = ins.value();
        }
        returned.setInstantiations(instantiations);
    }

    return returned;
}

AbstractMetaClassPtr
    AbstractMetaBuilder::inheritTemplateClass(const ComplexTypeEntryPtr &te,
                                              const AbstractMetaClassCPtr &templateClass,
                                              const AbstractMetaTypeList &templateTypes,
                                              InheritTemplateFlags flags)
{
    auto result = std::make_shared<AbstractMetaClass>();
    result->setTypeDef(true);

    result->setTypeEntry(te);
    if (!AbstractMetaBuilderPrivate::inheritTemplate(result, templateClass,
                                                     templateTypes, flags)) {
        return {};
    }
    AbstractMetaBuilderPrivate::inheritTemplateFunctions(result);
    return result;
}

bool AbstractMetaBuilderPrivate::inheritTemplate(const AbstractMetaClassPtr &subclass,
                                                 const AbstractMetaClassCPtr &templateClass,
                                                 const TypeInfo &info)
{
    AbstractMetaTypeList  templateTypes;

    for (const TypeInfo &i : info.instantiations()) {
        QString typeName = i.qualifiedName().join(u"::"_s);
        TypeDatabase *typeDb = TypeDatabase::instance();
        TypeEntryPtr t;
        // Check for a non-type template integer parameter, that is, for a base
        // "template <int R, int C> Matrix<R, C>" and subclass
        // "typedef Matrix<2,3> Matrix2x3;". If so, create dummy entries of
        // EnumValueTypeEntry for the integer values encountered on the fly.
        if (isNumber(typeName)) {
            t = typeDb->findType(typeName);
            if (!t) {
                auto parent = typeSystemTypeEntry(subclass->typeEntry());
                t = TypeDatabase::instance()->addConstantValueTypeEntry(typeName, parent);
            }
        } else {
            QStringList possibleNames;
            possibleNames << subclass->qualifiedCppName() + u"::"_s + typeName;
            possibleNames << templateClass->qualifiedCppName() + u"::"_s + typeName;
            if (subclass->enclosingClass())
                possibleNames << subclass->enclosingClass()->qualifiedCppName() + u"::"_s + typeName;
            possibleNames << typeName;

            for (const QString &possibleName : std::as_const(possibleNames)) {
                t = typeDb->findType(possibleName);
                if (t)
                    break;
            }
        }

        if (t) {
            AbstractMetaType temporaryType(t);
            temporaryType.setConstant(i.isConstant());
            temporaryType.setReferenceType(i.referenceType());
            temporaryType.setIndirectionsV(i.indirectionsV());
            temporaryType.decideUsagePattern();
            templateTypes << temporaryType;
        } else {
            qCWarning(lcShiboken).noquote().nospace()
                << "Ignoring template parameter " << typeName << " from "
                << info.toString() << ". The corresponding type was not found in the typesystem.";
        }
    }
    return inheritTemplate(subclass, templateClass, templateTypes);
}

bool AbstractMetaBuilderPrivate::inheritTemplate(const AbstractMetaClassPtr &subclass,
                                                 const AbstractMetaClassCPtr &templateClass,
                                                 const AbstractMetaTypeList &templateTypes,
                                                 InheritTemplateFlags flags)
{
    subclass->setTemplateBaseClass(templateClass);
    if (flags.testFlag(InheritTemplateFlag::SetEnclosingClass))
        subclass->setEnclosingClass(templateClass->enclosingClass());
    subclass->setTemplateBaseClassInstantiations(templateTypes);
    subclass->setBaseClass(templateClass->baseClass());
    return true;
}

AbstractMetaFunctionPtr
    AbstractMetaBuilderPrivate::inheritTemplateFunction(const AbstractMetaFunctionCPtr &function,
                                                        const AbstractMetaTypeList &templateTypes)
{
    AbstractMetaFunctionPtr f(function->copy());
    f->setArguments(AbstractMetaArgumentList());
    f->setFlags(f->flags() | AbstractMetaFunction::Flag::InheritedFromTemplate);

    if (!function->isVoid()) {
        auto returnType = inheritTemplateType(templateTypes, function->type());
        if (!returnType.has_value())
            return {};
        f->setType(returnType.value());
    }

    const AbstractMetaArgumentList &arguments = function->arguments();
    for (const AbstractMetaArgument &argument : arguments) {
        auto argType = inheritTemplateType(templateTypes, argument.type());
        if (!argType.has_value())
            return {};
        AbstractMetaArgument arg = argument;
        arg.setType(argType.value());
        f->addArgument(arg);
    }

    return f;
}

AbstractMetaFunctionPtr
    AbstractMetaBuilder::inheritTemplateFunction(const AbstractMetaFunctionCPtr &function,
                                                 const AbstractMetaTypeList &templateTypes)
{
    return AbstractMetaBuilderPrivate::inheritTemplateFunction(function, templateTypes);
}

AbstractMetaFunctionPtr
    AbstractMetaBuilderPrivate::inheritTemplateMember(const AbstractMetaFunctionCPtr &function,
                                                      const AbstractMetaTypeList &templateTypes,
                                                      const AbstractMetaClassCPtr &templateClass,
                                                      const AbstractMetaClassPtr &subclass)
{
    AbstractMetaFunctionPtr f = inheritTemplateFunction(function, templateTypes);
    if (!f)
        return {};

    // There is no base class in the target language to inherit from here, so
    // the template instantiation is the class that implements the function.
    f->setImplementingClass(subclass);

    // We also set it as the declaring class, since the superclass is
    // supposed to disappear. This allows us to make certain function modifications
    // on the inherited functions.
    f->setDeclaringClass(subclass);

    if (f->isConstructor()) {
        f->setName(subclass->name());
        f->setOriginalName(subclass->name());
    }

    ComplexTypeEntryPtr te = subclass->typeEntry();
    const FunctionModificationList mods = function->modifications(templateClass);

    for (auto mod : mods) {
        mod.setSignature(f->minimalSignature());

// If we ever need it... Below is the code to do
// substitution of the template instantation type inside
// injected code..
#if 0
    if (mod.modifiers & Modification::CodeInjection) {
        for (int j = 0; j < template_types.size(); ++j) {
            CodeSnip &snip = mod.snips.last();
            QString code = snip.code();
            code.replace(QString::fromLatin1("$$QT_TEMPLATE_%1$$").arg(j),
                         template_types.at(j)->typeEntry()->qualifiedCppName());
            snip.codeList.clear();
            snip.addCode(code);
        }
    }
#endif
        te->addFunctionModification(mod);
    }

    QString errorMessage;
    if (!applyArrayArgumentModifications(f->modifications(subclass), f.get(),
                                         &errorMessage)) {
        qCWarning(lcShiboken, "While specializing %s (%s): %s",
                  qPrintable(subclass->name()), qPrintable(templateClass->name()),
                  qPrintable(errorMessage));
    }
    return f;
}

AbstractMetaFunctionPtr
    AbstractMetaBuilder::inheritTemplateMember(const AbstractMetaFunctionCPtr &function,
                                               const AbstractMetaTypeList &templateTypes,
                                               const AbstractMetaClassCPtr &templateClass,
                                               const AbstractMetaClassPtr &subclass)
{
    return AbstractMetaBuilderPrivate::inheritTemplateMember(function, templateTypes,
                                                             templateClass, subclass);
}

static bool doInheritTemplateFunction(const AbstractMetaFunctionCPtr &function,
                                      const AbstractMetaFunctionCList &existingSubclassFuncs,
                                      const AbstractMetaClassCPtr &templateBaseClass,
                                      const AbstractMetaClassCPtr &subclass)
{
    // If the function is modified or the instantiation has an equally named
    // function we are shadowing, so we need to skip it (unless the subclass
    // declares it via "using").
    if (function->isModifiedRemoved())
        return false;
    if (function->isConstructor() && !subclass->isTypeDef())
        return false;
    return AbstractMetaFunction::find(existingSubclassFuncs, function->name()) == nullptr
        || subclass->isUsingMember(templateBaseClass, function->name(), Access::Protected);
}

void AbstractMetaBuilderPrivate::inheritTemplateFunctions(const AbstractMetaClassPtr &subclass)
{
    auto templateClass = subclass->templateBaseClass();

    if (subclass->isTypeDef()) {
        subclass->setHashFunction(templateClass->hashFunction());
        subclass->setHasNonPrivateConstructor(templateClass->hasNonPrivateConstructor());
        subclass->setHasPrivateDestructor(templateClass->hasPrivateDestructor());
        subclass->setHasProtectedDestructor(templateClass->hasProtectedDestructor());
        subclass->setHasVirtualDestructor(templateClass->hasVirtualDestructor());
    }

    const auto &templateTypes = subclass->templateBaseClassInstantiations();
    const AbstractMetaFunctionCList existingSubclassFuncs =
        subclass->functions(); // Take copy
    const auto &templateClassFunctions = templateClass->functions();
    for (const auto &function : templateClassFunctions) {
        if (doInheritTemplateFunction(function, existingSubclassFuncs,
                                      templateClass, subclass)) {
            AbstractMetaFunctionCPtr f = inheritTemplateMember(function, templateTypes,
                                                               templateClass, subclass);
            if (f)
                AbstractMetaClass::addFunction(subclass, f);
        }
    }

     // Take copy
    const AbstractMetaFieldList existingSubclassFields = subclass->fields();
    const AbstractMetaFieldList &templateClassFields = templateClass->fields();
    for (const AbstractMetaField &field : templateClassFields) {
        // If the field is modified or the instantiation has a field named
        // the same as an existing field we have shadowing, so we need to skip it.
        if (field.isModifiedRemoved() || field.isStatic()
            || AbstractMetaField::find(existingSubclassFields, field.name()).has_value()) {
            continue;
        }

        AbstractMetaField f = field;
        f.setEnclosingClass(subclass);
        auto fieldType = inheritTemplateType(templateTypes, field.type());
        if (!fieldType.has_value())
            continue;
        f.setType(fieldType.value());
        subclass->addField(f);
    }
}

void AbstractMetaBuilderPrivate::parseQ_Properties(const AbstractMetaClassPtr &metaClass,
                                                   const QStringList &declarations)
{
    const QStringList scopes = currentScope()->qualifiedName();
    QString errorMessage;
    int i = 0;
    for (; i < declarations.size(); ++i) {
        auto spec = QPropertySpec::parseQ_Property(this, metaClass, declarations.at(i), scopes, &errorMessage);
        if (spec.has_value()) {
            spec->setIndex(i);
            metaClass->addPropertySpec(spec.value());
        } else {
            QString message;
            QTextStream str(&message);
            str << metaClass->sourceLocation() << errorMessage;
            qCWarning(lcShiboken, "%s", qPrintable(message));
        }
    }

    // User-added properties
    auto typeEntry = metaClass->typeEntry();
    for (const TypeSystemProperty &tp : typeEntry->properties()) {
        std::optional<QPropertySpec> spec;
        if (metaClass->propertySpecByName(tp.name))
            errorMessage = msgPropertyExists(metaClass->name(), tp.name);
        else
            spec = QPropertySpec::fromTypeSystemProperty(this, metaClass, tp, scopes, &errorMessage);

        if (spec.has_value()) {
            spec->setIndex(i++);
            metaClass->addPropertySpec(spec.value());
        } else {
            QString message;
            QTextStream str(&message);
            str << typeEntry->sourceLocation() << errorMessage;
            qCWarning(lcShiboken, "%s", qPrintable(message));
        }
    }
}

void AbstractMetaBuilderPrivate::setupExternalConversion(const AbstractMetaClassCPtr &cls)
{
    const auto &convOps = cls->operatorOverloads(OperatorQueryOption::ConversionOp);
    for (const auto &func : convOps) {
        if (func->isModifiedRemoved())
            continue;
        const auto metaClass =
            AbstractMetaClass::findClass(m_metaClasses, func->type().typeEntry());
        if (!metaClass)
            continue;
        metaClass->addExternalConversionOperator(func);
    }
    for (const auto &innerClass : cls->innerClasses())
        setupExternalConversion(innerClass);
}

static void writeRejectLogFile(const QString &name,
                               const AbstractMetaBuilderPrivate::RejectSet &rejects)
{
    static const QHash<AbstractMetaBuilder::RejectReason, QByteArray> descriptions ={
        {AbstractMetaBuilder::NotInTypeSystem,  "Not in type system"_ba},
        {AbstractMetaBuilder::GenerationDisabled, "Generation disabled by type system"_ba},
        {AbstractMetaBuilder::RedefinedToNotClass, "Type redefined to not be a class"_ba},
        {AbstractMetaBuilder::UnmatchedReturnType, "Unmatched return type"_ba},
        {AbstractMetaBuilder::UnmatchedArgumentType, "Unmatched argument type"_ba},
        {AbstractMetaBuilder::UnmatchedOperator, "Unmatched operator"_ba},
        {AbstractMetaBuilder::Deprecated, "Deprecated"_ba}
    };

    QFile f(name);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcShiboken, "%s", qPrintable(msgCannotOpenForWriting(f)));
        return;
    }

    QTextStream s(&f);

    int lastReason = -1;
    for (const auto &e : rejects) {
        if (e.reason != lastReason) {
            const QByteArray description = descriptions.value(e.reason, "Unknown reason"_ba);
            const QByteArray underline(description.size(), '*');
            if (lastReason != -1)
                s << '\n';
            s << underline << '\n' << description << '\n' << underline << "\n\n";
            lastReason = e.reason;
        }

        s << " - " << e << '\n';
    }
}

void AbstractMetaBuilderPrivate::dumpLog() const
{
    writeRejectLogFile(m_logDirectory + u"mjb_rejected_classes.log"_s, m_rejectedClasses);
    writeRejectLogFile(m_logDirectory + u"mjb_rejected_enums.log"_s, m_rejectedEnums);
    writeRejectLogFile(m_logDirectory + u"mjb_rejected_functions.log"_s, m_rejectedFunctions);
    writeRejectLogFile(m_logDirectory + u"mjb_rejected_fields.log"_s, m_rejectedFields);
}

// Topological sorting of classes. Templates for use with
// AbstractMetaClassList/AbstractMetaClassCList.
// Add a dependency of the class associated with typeEntry on clazz.
template <class MetaClass>
static bool addClassDependency(const QList<std::shared_ptr<MetaClass> > &classList,
                               const TypeEntryCPtr &typeEntry,
                               std::shared_ptr<MetaClass> clazz,
                               Graph<std::shared_ptr<MetaClass> > *graph)
{
    if (!typeEntry->isComplex() || typeEntry == clazz->typeEntry())
        return false;
    const auto c = AbstractMetaClass::findClass(classList, typeEntry);
    if (c == nullptr || c->enclosingClass() == clazz)
        return false;
    return graph->addEdge(c, clazz);
}

template <class MetaClass>
static QList<std::shared_ptr<MetaClass> >
    topologicalSortHelper(const QList<std::shared_ptr<MetaClass> > &classList,
                          const Dependencies &additionalDependencies)
{
    Graph<std::shared_ptr<MetaClass> > graph(classList.cbegin(), classList.cend());

    for (const auto &dep : additionalDependencies) {
        if (!graph.addEdge(dep.parent, dep.child)) {
            qCWarning(lcShiboken).noquote().nospace()
                << "AbstractMetaBuilder::classesTopologicalSorted(): Invalid additional dependency: "
                << dep.child->name() << " -> " << dep.parent->name() << '.';
        }
    }

    for (const auto &clazz : classList) {
        if (auto enclosingC = clazz->enclosingClass()) {
            const auto enclosing = std::const_pointer_cast<MetaClass>(enclosingC);
            graph.addEdge(enclosing, clazz);
        }

        for (const auto &baseClass : clazz->baseClasses())
            graph.addEdge(std::const_pointer_cast<MetaClass>(baseClass), clazz);

        for (const auto &func : clazz->functions()) {
            const AbstractMetaArgumentList &arguments = func->arguments();
            for (const AbstractMetaArgument &arg : arguments) {
                // Check methods with default args: If a class is instantiated by value,
                // ("QString s = QString()"), add a dependency.
                if (!arg.originalDefaultValueExpression().isEmpty()
                    && arg.type().isValue()) {
                    addClassDependency(classList, arg.type().typeEntry(),
                                       clazz, &graph);
                }
            }
        }
        // Member fields need to be initialized
        for (const AbstractMetaField &field : clazz->fields()) {
            auto typeEntry = field.type().typeEntry();
            if (typeEntry->isEnum()) // Enum defined in class?
                typeEntry = typeEntry->parent();
            if (typeEntry != nullptr)
                addClassDependency(classList, typeEntry, clazz, &graph);
        }
    }

    const auto result = graph.topologicalSort();
    if (!result.isValid() && graph.nodeCount()) {
        QTemporaryFile tempFile(QDir::tempPath() + u"/cyclic_depXXXXXX.dot"_s);
        tempFile.setAutoRemove(false);
        tempFile.open();
        graph.dumpDot(tempFile.fileName(),
                      [] (const AbstractMetaClassCPtr &c) { return c->name(); });

        QString message;
        QTextStream str(&message);
        str << "Cyclic dependency of classes found:";
        for (auto c : result.cyclic)
            str << ' ' << c->name();
        str << ". Graph can be found at \"" << QDir::toNativeSeparators(tempFile.fileName()) << '"';
        qCWarning(lcShiboken, "%s", qPrintable(message));
    }

    return result.result;
}

AbstractMetaClassList
   AbstractMetaBuilderPrivate::classesTopologicalSorted(const AbstractMetaClassList &classList,
                                                        const Dependencies &additionalDependencies)
{
    return topologicalSortHelper(classList, additionalDependencies);
}

AbstractMetaClassCList
    AbstractMetaBuilderPrivate::classesTopologicalSorted(const AbstractMetaClassCList &classList,
                                                         const Dependencies &additionalDependencies)
{
    return topologicalSortHelper(classList, additionalDependencies);
}

void AbstractMetaBuilderPrivate::pushScope(const NamespaceModelItem &item)
{
    // For purposes of type lookup, join all namespaces of the same name
    // within the parent item.
    QList<NamespaceModelItem> candidates;
    const QString name = item->name();
    if (!m_scopes.isEmpty()) {
        for (const auto &n : m_scopes.constLast()->namespaces()) {
            if (n->name() == name)
                candidates.append(n);
        }
    }
    if (candidates.size() > 1) {
        auto joined = std::make_shared<_NamespaceModelItem>(m_scopes.constLast()->model(),
                                                            name, _CodeModelItem::Kind_Namespace);
        joined->setScope(item->scope());
        for (const auto &n : candidates)
            joined->appendNamespace(*n);
        m_scopes << joined;
    } else {
        m_scopes << item;
    }
}

void AbstractMetaBuilder::setGlobalHeaders(const QFileInfoList &globalHeaders)
{
    d->m_globalHeaders = globalHeaders;
}

void AbstractMetaBuilder::setHeaderPaths(const HeaderPaths &hp)
{
    for (const auto & h: hp) {
        if (h.type != HeaderType::Framework && h.type != HeaderType::FrameworkSystem)
            d->m_headerPaths.append(QFile::decodeName(h.path));
    }
}

void AbstractMetaBuilder::setUseGlobalHeader(bool h)
{
    AbstractMetaBuilderPrivate::m_useGlobalHeader = h;
}

void AbstractMetaBuilder::setSkipDeprecated(bool value)
{
    d->m_skipDeprecated = value;
}

void AbstractMetaBuilder::setApiExtractorFlags(ApiExtractorFlags flags)
{
    d->m_apiExtractorFlags = flags;
}

// PYSIDE-975: When receiving an absolute path name from the code model, try
// to resolve it against the include paths set on shiboken in order to recreate
// relative paths like #include <foo/bar.h>.

static inline bool isFileSystemSlash(QChar c)
{
    return c == u'/' || c == u'\\';
}

static bool matchHeader(const QString &headerPath, const QString &fileName)
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    static const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    static const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    const auto pathSize = headerPath.size();
    return fileName.size() > pathSize
        && isFileSystemSlash(fileName.at(pathSize))
        && fileName.startsWith(headerPath, caseSensitivity);
}

void AbstractMetaBuilderPrivate::setInclude(const TypeEntryPtr &te, const QString &path) const
{
    auto it = m_resolveIncludeHash.find(path);
    if (it == m_resolveIncludeHash.end()) {
        QFileInfo info(path);
        const QString fileName = info.fileName();
        if (!m_useGlobalHeader
            && std::any_of(m_globalHeaders.cbegin(), m_globalHeaders.cend(),
                           [fileName] (const QFileInfo &fi) {
                               return fi.fileName() == fileName; })) {
            return;
        }

        int bestMatchLength = 0;
        for (const auto &headerPath : m_headerPaths) {
            if (headerPath.size() > bestMatchLength && matchHeader(headerPath, path))
                bestMatchLength = headerPath.size();
        }
        const QString include = bestMatchLength > 0
            ? path.right(path.size() - bestMatchLength - 1) : fileName;
        it = m_resolveIncludeHash.insert(path, {Include::IncludePath, include});
    }
    te->setInclude(it.value());
}

#ifndef QT_NO_DEBUG_STREAM
template <class Container>
static void debugFormatSequence(QDebug &d, const char *key, const Container& c,
                                const char *separator = ", ")
{
    if (c.isEmpty())
        return;
    const auto begin = c.begin();
    const auto end = c.end();
    d << "\n  " << key << '[' << c.size() << "]=(";
    for (auto it = begin; it != end; ++it) {
        if (it != begin)
            d << separator;
        d << *it;
    }
    d << ')';
}

void AbstractMetaBuilder::formatDebug(QDebug &debug) const
{
    debug << "m_globalHeader=" << d->m_globalHeaders;
    debugFormatSequence(debug, "globalEnums", d->m_globalEnums, "\n");
    debugFormatSequence(debug, "globalFunctions", d->m_globalFunctions, "\n");
    if (const auto scopeCount = d->m_scopes.size()) {
        debug << "\n  scopes[" << scopeCount << "]=(";
        for (qsizetype i = 0; i < scopeCount; ++i) {
            if (i)
                debug << ", ";
            _CodeModelItem::formatKind(debug, d->m_scopes.at(i)->kind());
            debug << " \"" << d->m_scopes.at(i)->name() << '"';
        }
        debug << ')';
    }
    debugFormatSequence(debug, "classes", d->m_metaClasses, "\n");
    debugFormatSequence(debug, "templates", d->m_templates, "\n");
}

QDebug operator<<(QDebug d, const AbstractMetaBuilder &ab)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "AbstractMetaBuilder(";
    ab.formatDebug(d);
    d << ')';
    return d;
}
#endif // !QT_NO_DEBUG_STREAM

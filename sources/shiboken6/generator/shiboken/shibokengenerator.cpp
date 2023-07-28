// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "shibokengenerator.h"
#include "generatorargument.h"
#include "defaultvalue.h"
#include "generatorcontext.h"
#include "apiextractorresult.h"
#include "codesnip.h"
#include "customconversion.h"
#include "ctypenames.h"
#include <abstractmetabuilder.h>
#include <abstractmetaenum.h>
#include <abstractmetafield.h>
#include <abstractmetafunction.h>
#include <abstractmetalang.h>
#include <abstractmetalang_helpers.h>
#include <usingmember.h>
#include <exception.h>
#include <messages.h>
#include <modifications.h>
#include "overloaddata.h"
#include "propertyspec.h"
#include "pytypenames.h"
#include <reporthandler.h>
#include <textstream.h>
#include <typedatabase.h>
#include <abstractmetabuilder.h>
#include <containertypeentry.h>
#include <customtypenentry.h>
#include <enumtypeentry.h>
#include <flagstypeentry.h>
#include <namespacetypeentry.h>
#include <primitivetypeentry.h>
#include <pythontypeentry.h>
#include <valuetypeentry.h>

#include <iostream>

#include "qtcompat.h"

#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <QtCore/QRegularExpression>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

static const char PARENT_CTOR_HEURISTIC[] = "enable-parent-ctor-heuristic";
static const char RETURN_VALUE_HEURISTIC[] = "enable-return-value-heuristic";
static const char DISABLE_VERBOSE_ERROR_MESSAGES[] = "disable-verbose-error-messages";
static const char USE_ISNULL_AS_NB_NONZERO[] = "use-isnull-as-nb_nonzero";
static const char USE_OPERATOR_BOOL_AS_NB_NONZERO[] = "use-operator-bool-as-nb_nonzero";
static const char WRAPPER_DIAGNOSTICS[] = "wrapper-diagnostics";
static const char NO_IMPLICIT_CONVERSIONS[] = "no-implicit-conversions";
static const char LEAN_HEADERS[] = "lean-headers";

const QString CPP_ARG = u"cppArg"_s;
const QString CPP_ARG_REMOVED = u"removed_cppArg"_s;
const QString CPP_RETURN_VAR = u"cppResult"_s;
const QString CPP_SELF_VAR = u"cppSelf"_s;
const QString NULL_PTR = u"nullptr"_s;
const QString PYTHON_ARG = u"pyArg"_s;
const QString PYTHON_ARGS = u"pyArgs"_s;
const QString PYTHON_OVERRIDE_VAR = u"pyOverride"_s;
const QString PYTHON_RETURN_VAR = u"pyResult"_s;
const QString PYTHON_TO_CPP_VAR = u"pythonToCpp"_s;

const QString CONV_RULE_OUT_VAR_SUFFIX = u"_out"_s;
const QString BEGIN_ALLOW_THREADS =
    u"PyThreadState *_save = PyEval_SaveThread(); // Py_BEGIN_ALLOW_THREADS"_s;
const QString END_ALLOW_THREADS = u"PyEval_RestoreThread(_save); // Py_END_ALLOW_THREADS"_s;

struct GeneratorClassInfoCacheEntry
{
    ShibokenGenerator::FunctionGroups functionGroups;
    bool needsGetattroFunction = false;
};

using GeneratorClassInfoCache = QHash<AbstractMetaClassCPtr, GeneratorClassInfoCacheEntry>;

Q_GLOBAL_STATIC(GeneratorClassInfoCache, generatorClassInfoCache)

static const char CHECKTYPE_REGEX[] = R"(%CHECKTYPE\[([^\[]*)\]\()";
static const char ISCONVERTIBLE_REGEX[] = R"(%ISCONVERTIBLE\[([^\[]*)\]\()";
static const char CONVERTTOPYTHON_REGEX[] = R"(%CONVERTTOPYTHON\[([^\[]*)\]\()";
// Capture a '*' leading the variable name into the target
// so that "*valuePtr = %CONVERTTOCPP..." works as expected.
static const char CONVERTTOCPP_REGEX[] =
    R"((\*?%?[a-zA-Z_][\w\.]*(?:\[[^\[^<^>]+\])*)(?:\s+)=(?:\s+)%CONVERTTOCPP\[([^\[]*)\]\()";

const ShibokenGenerator::TypeSystemConverterRegExps &
    ShibokenGenerator::typeSystemConvRegExps()
{
    static const TypeSystemConverterRegExps result = {
        QRegularExpression(QLatin1StringView(CHECKTYPE_REGEX)),
        QRegularExpression(QLatin1StringView(ISCONVERTIBLE_REGEX)),
        QRegularExpression(QLatin1StringView(CONVERTTOCPP_REGEX)),
        QRegularExpression(QLatin1StringView(CONVERTTOPYTHON_REGEX))
    };
    return result;
}

ShibokenGenerator::ShibokenGenerator() = default;

ShibokenGenerator::~ShibokenGenerator() = default;

// Correspondences between primitive and Python types.
static const QHash<QString, QString> &primitiveTypesCorrespondences()
{
    static const QHash<QString, QString> result = {
        {u"bool"_s, pyBoolT()},
        {u"char"_s, sbkCharT()},
        {u"signed char"_s, sbkCharT()},
        {u"unsigned char"_s, sbkCharT()},
        {intT(), pyLongT()},
        {u"signed int"_s, pyLongT()},
        {u"uint"_s, pyLongT()},
        {u"unsigned int"_s, pyLongT()},
        {shortT(), pyLongT()},
        {u"ushort"_s, pyLongT()},
        {u"signed short"_s, pyLongT()},
        {u"signed short int"_s, pyLongT()},
        {unsignedShortT(), pyLongT()},
        {u"unsigned short int"_s, pyLongT()},
        {longT(), pyLongT()},
        {doubleT(), pyFloatT()},
        {floatT(), pyFloatT()},
        {u"unsigned long"_s, pyLongT()},
        {u"signed long"_s, pyLongT()},
        {u"ulong"_s, pyLongT()},
        {u"unsigned long int"_s, pyLongT()},
        {u"long long"_s, pyLongT()},
        {u"__int64"_s, pyLongT()},
        {u"unsigned long long"_s, pyLongT()},
        {u"unsigned __int64"_s, pyLongT()},
        {u"size_t"_s, pyLongT()}
    };
    return result;
}

const QHash<QString, QChar> &ShibokenGenerator::formatUnits()
{
    static const QHash<QString, QChar> result = {
        {u"char"_s, u'b'},
        {u"unsigned char"_s, u'B'},
        {intT(), u'i'},
        {u"unsigned int"_s, u'I'},
        {shortT(), u'h'},
        {unsignedShortT(), u'H'},
        {longT(), u'l'},
        {unsignedLongLongT(), u'k'},
        {longLongT(), u'L'},
        {u"__int64"_s, u'L'},
        {unsignedLongLongT(), u'K'},
        {u"unsigned __int64"_s, u'K'},
        {doubleT(), u'd'},
        {floatT(), u'f'},
    };
    return result;
}

QString ShibokenGenerator::translateTypeForWrapperMethod(const AbstractMetaType &cType,
                                                         const AbstractMetaClassCPtr &context,
                                                         Options options) const
{
    if (cType.isArray()) {
        return translateTypeForWrapperMethod(*cType.arrayElementType(), context, options)
               + u"[]"_s;
    }

    if (avoidProtectedHack() && cType.isEnum()) {
        auto metaEnum = api().findAbstractMetaEnum(cType.typeEntry());
        if (metaEnum && metaEnum->isProtected())
            return protectedEnumSurrogateName(metaEnum.value());
    }

    return translateType(cType, context, options);
}

bool ShibokenGenerator::shouldGenerateCppWrapper(const AbstractMetaClassCPtr &metaClass) const
{
    const auto wrapper = metaClass->cppWrapper();
    return wrapper.testFlag(AbstractMetaClass::CppVirtualMethodWrapper)
        || (avoidProtectedHack()
            && wrapper.testFlag(AbstractMetaClass::CppProtectedHackWrapper));
}

ShibokenGenerator::FunctionGeneration
    ShibokenGenerator::functionGeneration(const AbstractMetaFunctionCPtr &func) const
{
    FunctionGeneration result;

    const auto functionType = func->functionType();
    switch (functionType) {
    case AbstractMetaFunction::ConversionOperator:
    case AbstractMetaFunction::AssignmentOperatorFunction:
    case AbstractMetaFunction::MoveAssignmentOperatorFunction:
    case AbstractMetaFunction::DestructorFunction:
    case AbstractMetaFunction::SignalFunction:
    case AbstractMetaFunction::GetAttroFunction:
    case AbstractMetaFunction::SetAttroFunction:
        return result;
    default:
        if (func->isUserAdded() || func->usesRValueReferences() || !func->isWhiteListed())
            return result;
        break;
    }

    const bool notModifiedRemoved = !func->isModifiedRemoved();
    const bool isPrivate = func->isPrivate() && !func->isVisibilityModifiedToPrivate();
    switch (functionType) {
    case AbstractMetaFunction::ConstructorFunction:
        if (!isPrivate && notModifiedRemoved)
             result.setFlag(FunctionGenerationFlag::WrapperConstructor);
        return result;
    case AbstractMetaFunction::CopyConstructorFunction:
        if (!isPrivate && notModifiedRemoved)
            result.setFlag(FunctionGenerationFlag::WrapperSpecialCopyConstructor);
        return result;
    case AbstractMetaFunction::NormalFunction:
    case AbstractMetaFunction::SlotFunction:
        if (avoidProtectedHack() && func->isProtected())
            result.setFlag(FunctionGenerationFlag::ProtectedWrapper);
        break;
    default:
        break;
    }

    // Check on virtuals (including operators).
    const bool isAbstract = func->isAbstract();
    if (!(isAbstract || func->isVirtual())
        || func->attributes().testFlag(AbstractMetaFunction::FinalCppMethod)
        || func->isModifiedFinal()) {
        return result;
    }

    // MetaObject virtuals only need to be declared; CppGenerator creates a
    // special implementation.
    if (functionType == AbstractMetaFunction::NormalFunction
        && usePySideExtensions() && isQObject(func->ownerClass())) {
        const QString &name = func->name();
        if (name == u"metaObject"_s || name == u"qt_metacall") {
            result.setFlag(FunctionGenerationFlag::QMetaObjectMethod);
            return result;
        }
    }

    // Pure virtual functions need a default implementation even if private.
    if (isAbstract || (notModifiedRemoved && !isPrivate))
        result.setFlag(FunctionGenerationFlag::VirtualMethod);

    return result;
}

AbstractMetaFunctionCList ShibokenGenerator::implicitConversions(const TypeEntryCPtr &t) const
{
    if (!generateImplicitConversions() || !t->isValue())
        return {};
    auto vte = std::static_pointer_cast<const ValueTypeEntry>(t);
    auto customConversion = vte->customConversion();
    if (customConversion && customConversion->replaceOriginalTargetToNativeConversions())
        return {};

    auto result = api().implicitConversions(t);
    auto end = std::remove_if(result.begin(), result.end(),
                              [](const AbstractMetaFunctionCPtr &f) {
                                  return f->isUserAdded();
                              });
    result.erase(end, result.end());
    return result;
}

QString ShibokenGenerator::wrapperName(const AbstractMetaClassCPtr &metaClass) const
{
    Q_ASSERT(shouldGenerateCppWrapper(metaClass));
    QString result = metaClass->name();
    if (metaClass->enclosingClass()) // is a inner class
        result.replace(u"::"_s, u"_"_s);
    return result + u"Wrapper"_s;
}

QString ShibokenGenerator::fullPythonClassName(const AbstractMetaClassCPtr &metaClass)
{
    QString fullClassName = metaClass->name();
    auto enclosing = metaClass->enclosingClass();
    while (enclosing) {
        if (NamespaceTypeEntry::isVisibleScope(enclosing->typeEntry()))
            fullClassName.prepend(enclosing->name() + u'.');
        enclosing = enclosing->enclosingClass();
    }
    fullClassName.prepend(packageName() + u'.');
    return fullClassName;
}

QString ShibokenGenerator::headerFileNameForContext(const GeneratorContext &context)
{
    return fileNameForContextHelper(context, u"_wrapper.h"_s);
}

// PYSIDE-500: When avoiding the protected hack, also include the inherited
// wrapper classes of the *current* module, because without the protected hack,
// we sometimes need to cast inherited wrappers. Inherited classes
// of *other* modules are completely regenerated by the header generator
// since the wrapper headers are not installed.

IncludeGroup ShibokenGenerator::baseWrapperIncludes(const GeneratorContext &classContext) const
{
    IncludeGroup result{u"Wrappers"_s, {}};
    if (!classContext.useWrapper() || !avoidProtectedHack()
        || classContext.forSmartPointer()) {
        return result;
    }

    const auto moduleEntry = TypeDatabase::instance()->defaultTypeSystemType();
    const auto &baseClasses = allBaseClasses(classContext.metaClass());
    for (const auto &base : baseClasses) {
        const auto te = base->typeEntry();
        if (te->codeGeneration() == TypeEntry::GenerateCode) { // current module
            const auto context = contextForClass(base);
            if (context.useWrapper()) {
                const QString header = headerFileNameForContext(context);
                const auto type = typeSystemTypeEntry(te) == moduleEntry
                    ? Include::LocalPath : Include::IncludePath;
                result.append(Include(type, header));
            }
        }
    }
    return result;
}

QString ShibokenGenerator::fullPythonFunctionName(const AbstractMetaFunctionCPtr &func, bool forceFunc)
{
    QString funcName;
    if (func->isOperatorOverload())
        funcName = ShibokenGenerator::pythonOperatorFunctionName(func);
    else
       funcName = func->name();
    if (func->ownerClass()) {
        QString fullClassName = fullPythonClassName(func->ownerClass());
        if (func->isConstructor()) {
            funcName = fullClassName;
            if (forceFunc)
                funcName.append(u".__init__"_s);
        }
        else {
            funcName.prepend(fullClassName + u'.');
        }
    }
    else {
        funcName = packageName() + u'.' + func->name();
    }
    return funcName;
}

QString ShibokenGenerator::protectedEnumSurrogateName(const AbstractMetaEnum &metaEnum)
{
    QString result = metaEnum.fullName();
    result.replace(u'.', u'_');
    result.replace(u"::"_s, u"_"_s);
    return result + u"_Surrogate"_s;
}

QString ShibokenGenerator::cpythonFunctionName(const AbstractMetaFunctionCPtr &func)
{
    QString result;

    // PYSIDE-331: For inherited functions, we need to find the same labels.
    // Therefore we use the implementing class.
    if (func->implementingClass()) {
        result = cpythonBaseName(func->implementingClass()->typeEntry());
        if (func->isConstructor()) {
            result += u"_Init"_s;
        } else {
            result += u"Func_"_s;
            if (func->isOperatorOverload())
                result += ShibokenGenerator::pythonOperatorFunctionName(func);
            else
                result += func->name();
        }
    } else {
        result = u"Sbk"_s + moduleName() + u"Module_"_s + func->name();
    }

    return result;
}

QString ShibokenGenerator::cpythonMethodDefinitionName(const AbstractMetaFunctionCPtr &func)
{
    if (!func->ownerClass())
        return QString();
    return cpythonBaseName(func->ownerClass()->typeEntry()) + u"Method_"_s
           + func->name();
}

QString ShibokenGenerator::cpythonGettersSettersDefinitionName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonBaseName(metaClass) + u"_getsetlist"_s;
}

QString ShibokenGenerator::cpythonSetattroFunctionName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonBaseName(metaClass) + u"_setattro"_s;
}


QString ShibokenGenerator::cpythonGetattroFunctionName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonBaseName(metaClass) + u"_getattro"_s;
}

QString ShibokenGenerator::cpythonGetterFunctionName(const QString &name,
                                                     const AbstractMetaClassCPtr &enclosingClass)
{
    return cpythonBaseName(enclosingClass) + QStringLiteral("_get_") + name;
}

QString ShibokenGenerator::cpythonSetterFunctionName(const QString &name,
                                                     const AbstractMetaClassCPtr &enclosingClass)
{
    return cpythonBaseName(enclosingClass) + QStringLiteral("_set_") + name;
}

QString ShibokenGenerator::cpythonGetterFunctionName(const AbstractMetaField &metaField)
{
    return cpythonGetterFunctionName(metaField.name(), metaField.enclosingClass());
}

QString ShibokenGenerator::cpythonSetterFunctionName(const AbstractMetaField &metaField)
{
    return cpythonSetterFunctionName(metaField.name(), metaField.enclosingClass());
}

QString ShibokenGenerator::cpythonGetterFunctionName(const QPropertySpec &property,
                                                     const AbstractMetaClassCPtr &metaClass)
{
    return cpythonGetterFunctionName(property.name(), metaClass);
}

QString ShibokenGenerator::cpythonSetterFunctionName(const QPropertySpec &property,
                                                     const AbstractMetaClassCPtr &metaClass)
{
    return cpythonSetterFunctionName(property.name(), metaClass);
}

static QString cpythonEnumFlagsName(const QString &moduleName,
                                    const QString &qualifiedCppName)
{
    QString result = u"Sbk"_s + moduleName + u'_' + qualifiedCppName;
    result.replace(u"::"_s, u"_"_s);
    return result;
}

QString ShibokenGenerator::cpythonEnumName(const EnumTypeEntryCPtr &enumEntry)
{
    QString p = enumEntry->targetLangPackage();
    p.replace(u'.', u'_');
    return cpythonEnumFlagsName(p, enumEntry->qualifiedCppName());
}

QString ShibokenGenerator::cpythonEnumName(const AbstractMetaEnum &metaEnum)
{
    return cpythonEnumName(metaEnum.typeEntry());
}

QString ShibokenGenerator::cpythonFlagsName(const FlagsTypeEntryCPtr &flagsEntry)
{
    QString p = flagsEntry->targetLangPackage();
    p.replace(u'.', u'_');
    return cpythonEnumFlagsName(p, flagsEntry->originalName());
}

QString ShibokenGenerator::cpythonFlagsName(const AbstractMetaEnum *metaEnum)
{
    const auto flags = metaEnum->typeEntry()->flags();
    return flags ? cpythonFlagsName(flags) : QString{};
}

QString ShibokenGenerator::cpythonSpecialCastFunctionName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonBaseName(metaClass->typeEntry()) + u"SpecialCastFunction"_s;
}

QString ShibokenGenerator::cpythonWrapperCPtr(const AbstractMetaClassCPtr &metaClass,
                                              const QString &argName)
{
    return cpythonWrapperCPtr(metaClass->typeEntry(), argName);
}

QString ShibokenGenerator::cpythonWrapperCPtr(const AbstractMetaType &metaType,
                                              const QString &argName)
{
    if (!metaType.isWrapperType())
        return QString();
    return u"reinterpret_cast< ::"_s + metaType.cppSignature()
        + u" *>(Shiboken::Conversions::cppPointer("_s + cpythonTypeNameExt(metaType)
        + u", reinterpret_cast<SbkObject *>("_s + argName + u")))"_s;
}

QString ShibokenGenerator::cpythonWrapperCPtr(const TypeEntryCPtr &type,
                                              const QString &argName)
{
    if (!type->isWrapperType())
        return QString();
    return u"reinterpret_cast< ::"_s + type->qualifiedCppName()
        + u" *>(Shiboken::Conversions::cppPointer("_s + cpythonTypeNameExt(type)
        + u", reinterpret_cast<SbkObject *>("_s + argName + u")))"_s;
}

void ShibokenGenerator::writeToPythonConversion(TextStream & s, const AbstractMetaType &type,
                                                const AbstractMetaClassCPtr & /* context */,
                                                const QString &argumentName)
{
    s << cpythonToPythonConversionFunction(type) << argumentName << ')';
}

void ShibokenGenerator::writeToCppConversion(TextStream &s,
                                             const AbstractMetaClassCPtr &metaClass,
                                             const QString &inArgName, const QString &outArgName)
{
    s << cpythonToCppConversionFunction(metaClass) << inArgName << ", &" << outArgName << ')';
}

void ShibokenGenerator::writeToCppConversion(TextStream &s, const AbstractMetaType &type,
                                             const AbstractMetaClassCPtr &context, const QString &inArgName,
                                             const QString &outArgName)
{
    s << cpythonToCppConversionFunction(type, context) << inArgName << ", &" << outArgName << ')';
}

bool ShibokenGenerator::shouldRejectNullPointerArgument(const AbstractMetaFunctionCPtr &func,
                                                        int argIndex)
{
    if (argIndex < 0 || argIndex >= func->arguments().size())
        return false;

    const AbstractMetaArgument &arg = func->arguments().at(argIndex);
    if (arg.type().isValueTypeWithCopyConstructorOnly())
        return true;

    // Argument type is not a pointer, a None rejection should not be
    // necessary because the type checking would handle that already.
    if (!arg.type().isPointer())
        return false;
    if (arg.isModifiedRemoved())
        return false;
    for (const auto &funcMod : func->modifications()) {
        for (const ArgumentModification &argMod : funcMod.argument_mods()) {
            if (argMod.index() == argIndex + 1 && argMod.noNullPointers())
                return true;
        }
    }
    return false;
}

QString ShibokenGenerator::cpythonBaseName(const AbstractMetaType &type)
{
    if (type.isCString())
        return u"PyString"_s;
    return cpythonBaseName(type.typeEntry());
}

QString ShibokenGenerator::cpythonBaseName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonBaseName(metaClass->typeEntry());
}

QString ShibokenGenerator::containerCpythonBaseName(const ContainerTypeEntryCPtr &ctype)
{
    switch (ctype->containerKind()) {
    case ContainerTypeEntry::SetContainer:
        return u"PySet"_s;
    case ContainerTypeEntry::MapContainer:
    case ContainerTypeEntry::MultiMapContainer:
        return u"PyDict"_s;
    case ContainerTypeEntry::ListContainer:
    case ContainerTypeEntry::PairContainer:
    case ContainerTypeEntry::SpanContainer:
        break;
    default:
        Q_ASSERT(false);
    }
    return cPySequenceT();
}

QString ShibokenGenerator::cpythonBaseName(const TypeEntryCPtr &type)
{
    QString baseName;
    if (type->isWrapperType() || type->isNamespace()) { // && type->referenceType() == NoReference) {
        baseName = u"Sbk_"_s + type->name();
    } else if (type->isPrimitive()) {
        const auto ptype = basicReferencedTypeEntry(type);
        baseName = ptype->hasTargetLangApiType()
                   ? ptype->targetLangApiName() : pythonPrimitiveTypeName(ptype->name());
    } else if (type->isEnum()) {
        baseName = cpythonEnumName(std::static_pointer_cast<const EnumTypeEntry>(type));
    } else if (type->isFlags()) {
        baseName = cpythonFlagsName(std::static_pointer_cast<const FlagsTypeEntry>(type));
    } else if (type->isContainer()) {
        const auto ctype = std::static_pointer_cast<const ContainerTypeEntry>(type);
        baseName = containerCpythonBaseName(ctype);
    } else {
        baseName = cPyObjectT();
    }
    return baseName.replace(u"::"_s, u"_"_s);
}

QString ShibokenGenerator::cpythonTypeName(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonTypeName(metaClass->typeEntry());
}

QString ShibokenGenerator::cpythonTypeName(const TypeEntryCPtr &type)
{
    return cpythonBaseName(type) + u"_TypeF()"_s;
}

QString ShibokenGenerator::cpythonTypeNameExt(const TypeEntryCPtr &type)
{
    return cppApiVariableName(type->targetLangPackage()) + u'['
            + getTypeIndexVariableName(type) + u']';
}

QString ShibokenGenerator::converterObject(const AbstractMetaType &type)
{
    if (type.isCString())
        return u"Shiboken::Conversions::PrimitiveTypeConverter<const char *>()"_s;
    if (type.isVoidPointer())
        return u"Shiboken::Conversions::PrimitiveTypeConverter<void *>()"_s;
    const AbstractMetaTypeList nestedArrayTypes = type.nestedArrayTypes();
    if (!nestedArrayTypes.isEmpty() && nestedArrayTypes.constLast().isCppPrimitive()) {
        return QStringLiteral("Shiboken::Conversions::ArrayTypeConverter<")
            + nestedArrayTypes.constLast().minimalSignature()
            + u">("_s + QString::number(nestedArrayTypes.size())
            + u')';
    }

    auto typeEntry = type.typeEntry();
    if (typeEntry->isContainer() || typeEntry->isSmartPointer()) {
        return convertersVariableName(typeEntry->targetLangPackage())
               + u'[' + getTypeIndexVariableName(type) + u']';
    }
    return converterObject(typeEntry);
}

QString ShibokenGenerator::converterObject(const TypeEntryCPtr &type)
{
    if (isExtendedCppPrimitive(type))
        return QString::fromLatin1("Shiboken::Conversions::PrimitiveTypeConverter<%1>()")
                                  .arg(type->qualifiedCppName());
    if (type->isWrapperType())
        return QString::fromLatin1("PepType_SOTP(reinterpret_cast<PyTypeObject *>(%1))->converter")
                                  .arg(cpythonTypeNameExt(type));
    if (type->isEnum())
        return QString::fromLatin1("PepType_SETP(reinterpret_cast<SbkEnumType *>(%1))->converter")
                                  .arg(cpythonTypeNameExt(type));
    if (type->isFlags())
        return QString::fromLatin1("PepType_PFTP(reinterpret_cast<PySideQFlagsType *>(%1))->converter")
                                  .arg(cpythonTypeNameExt(type));

    if (type->isArray()) {
        qDebug() << "Warning: no idea how to handle the Qt5 type " << type->qualifiedCppName();
        return QString();
    }

    /* the typedef'd primitive types case */
    auto pte = std::dynamic_pointer_cast<const PrimitiveTypeEntry>(type);
    if (!pte) {
        qDebug() << "Warning: the Qt5 primitive type is unknown" << type->qualifiedCppName();
        return QString();
    }
    pte = basicReferencedTypeEntry(pte);
    if (pte->isPrimitive() && !isCppPrimitive(pte) && !pte->customConversion()) {
        return u"Shiboken::Conversions::PrimitiveTypeConverter<"_s
               + pte->qualifiedCppName() + u">()"_s;
    }

    return convertersVariableName(type->targetLangPackage())
           + u'[' + getTypeIndexVariableName(type) + u']';
}

QString ShibokenGenerator::cpythonTypeNameExt(const AbstractMetaType &type)
{
    return cppApiVariableName(type.typeEntry()->targetLangPackage()) + u'['
           + getTypeIndexVariableName(type) + u']';
}

static inline QString unknownOperator() { return QStringLiteral("__UNKNOWN_OPERATOR__"); }

QString ShibokenGenerator::fixedCppTypeName(const TargetToNativeConversion &toNative)
{
    if (toNative.sourceType())
        return fixedCppTypeName(toNative.sourceType());
    return toNative.sourceTypeName();
}
QString ShibokenGenerator::fixedCppTypeName(const AbstractMetaType &type)
{
    return fixedCppTypeName(type.typeEntry(), type.cppSignature());
}

static QString _fixedCppTypeName(QString typeName)
{
    typeName.remove(u' ');
    typeName.replace(u'.',  u'_');
    typeName.replace(u',',  u'_');
    typeName.replace(u'<',  u'_');
    typeName.replace(u'>',  u'_');
    typeName.replace(u"::"_s, u"_"_s);
    typeName.replace(u"*"_s,  u"PTR"_s);
    typeName.replace(u"&"_s,  u"REF"_s);
    return typeName;
}
QString ShibokenGenerator::fixedCppTypeName(const TypeEntryCPtr &type, QString typeName)
{
    if (typeName.isEmpty())
        typeName = type->qualifiedCppName();
    if (!type->generateCode()) {
        typeName.prepend(u'_');
        typeName.prepend(type->targetLangPackage());
    }
    return _fixedCppTypeName(typeName);
}

QString ShibokenGenerator::pythonPrimitiveTypeName(const QString &cppTypeName)
{
    const auto &mapping = primitiveTypesCorrespondences();
    const auto it = mapping.constFind(cppTypeName);
    if (it == mapping.cend())
        throw Exception(u"Primitive type not found: "_s + cppTypeName);
    return it.value();
}

QString ShibokenGenerator::pythonOperatorFunctionName(const AbstractMetaFunctionCPtr &func)
{
    QString op = Generator::pythonOperatorFunctionName(func->originalName());
    if (op.isEmpty()) {
        qCWarning(lcShiboken).noquote().nospace() << msgUnknownOperator(func.get());
        return unknownOperator();
    }
    if (func->arguments().isEmpty()) {
        if (op == u"__sub__")
            op = u"__neg__"_s;
        else if (op == u"__add__")
            op = u"__pos__"_s;
    } else if (func->isStatic() && func->arguments().size() == 2) {
        // If a operator overload function has 2 arguments and
        // is static we assume that it is a reverse operator.
        op = op.insert(2, u'r');
    }
    return op;
}

bool ShibokenGenerator::isNumber(const QString &cpythonApiName)
{
    return cpythonApiName == pyFloatT() || cpythonApiName == pyLongT()
       || cpythonApiName == pyBoolT();
}

static std::optional<TypeSystem::CPythonType>
    targetLangApiCPythonType(const PrimitiveTypeEntryCPtr &t)
{
    if (!t->hasTargetLangApiType())
        return {};
    const auto cte = t->targetLangApiType();
    if (cte->type() != TypeEntry::PythonType)
        return {};
    return std::static_pointer_cast<const PythonTypeEntry>(cte)->cPythonType();
}

bool ShibokenGenerator::isNumber(const TypeEntryCPtr &type)
{
    if (!type->isPrimitive())
        return false;
    const auto pte = basicReferencedTypeEntry(type);
    const auto cPythonTypeOpt = targetLangApiCPythonType(pte);
    // FIXME PYSIDE-1660: Return false here after making primitive types built-in?
    if (!cPythonTypeOpt.has_value()) {
        const auto &mapping = primitiveTypesCorrespondences();
        const auto it = mapping.constFind(pte->name());
        return it != mapping.cend() && isNumber(it.value());
    }
    const auto cPythonType = cPythonTypeOpt.value();
    return cPythonType == TypeSystem::CPythonType::Bool
           || cPythonType == TypeSystem::CPythonType::Float
           || cPythonType == TypeSystem::CPythonType::Integer;
}

bool ShibokenGenerator::isNumber(const AbstractMetaType &type)
{
    return isNumber(type.typeEntry());
}

bool ShibokenGenerator::isPyInt(const TypeEntryCPtr &type)
{
    if (!type->isPrimitive())
        return false;
    const auto pte = basicReferencedTypeEntry(type);
    const auto cPythonTypeOpt = targetLangApiCPythonType(pte);
    // FIXME PYSIDE-1660: Return false here after making primitive types built-in?
    if (!cPythonTypeOpt.has_value()) {
        const auto &mapping = primitiveTypesCorrespondences();
        const auto it = mapping.constFind(pte->name());
        return it != mapping.cend() && it.value() ==  pyLongT();
    }
    return cPythonTypeOpt.value() == TypeSystem::CPythonType::Integer;
}

bool ShibokenGenerator::isPyInt(const AbstractMetaType &type)
{
    return isPyInt(type.typeEntry());
}

bool ShibokenGenerator::isNullPtr(const QString &value)
{
    return value == u"0" || value == u"nullptr"
        || value == u"NULLPTR" || value == u"{}";
}

QString ShibokenGenerator::cpythonCheckFunction(AbstractMetaType metaType)
{
    const auto typeEntry = metaType.typeEntry();
    if (typeEntry->isCustom()) {
        const auto cte = std::static_pointer_cast<const CustomTypeEntry>(typeEntry);
        if (cte->hasCheckFunction())
            return cte->checkFunction();
        throw Exception(msgUnknownCheckFunction(typeEntry));
    }

    if (metaType.isExtendedCppPrimitive()) {
        if (metaType.isCString())
            return u"Shiboken::String::check"_s;
        if (metaType.isVoidPointer())
            return u"true"_s;
        return cpythonCheckFunction(typeEntry);
    }

    if (typeEntry->isContainer()) {
        QString typeCheck = u"Shiboken::Conversions::"_s;
        ContainerTypeEntry::ContainerKind type =
            std::static_pointer_cast<const ContainerTypeEntry>(typeEntry)->containerKind();
        if (type == ContainerTypeEntry::ListContainer
            || type == ContainerTypeEntry::SetContainer) {
            const QString containerType = type == ContainerTypeEntry::SetContainer
                ? u"Iterable"_s : u"Sequence"_s;
            const AbstractMetaType &type = metaType.instantiations().constFirst();
            if (type.isPointerToWrapperType()) {
                typeCheck += u"check"_s + containerType + u"Types("_s
                             + cpythonTypeNameExt(type) + u", "_s;
            } else if (type.isWrapperType()) {
                typeCheck += u"convertible"_s + containerType
                             + u"Types("_s + cpythonTypeNameExt(type) + u", "_s;
            } else {
                typeCheck += u"convertible"_s + containerType
                             + u"Types("_s + converterObject(type) + u", "_s;
            }
        } else if (type == ContainerTypeEntry::MapContainer
            || type == ContainerTypeEntry::MultiMapContainer
            || type == ContainerTypeEntry::PairContainer) {

            QString pyType;
            if (type == ContainerTypeEntry::PairContainer)
                pyType = u"Pair"_s;
            else if (type == ContainerTypeEntry::MultiMapContainer)
                pyType = u"MultiDict"_s;
            else
                pyType = u"Dict"_s;

            const AbstractMetaType &firstType = metaType.instantiations().constFirst();
            const AbstractMetaType &secondType = metaType.instantiations().constLast();
            if (firstType.isPointerToWrapperType() && secondType.isPointerToWrapperType()) {
                QTextStream(&typeCheck) << "check" << pyType << "Types("
                    << cpythonTypeNameExt(firstType) << ", "
                    << cpythonTypeNameExt(secondType) << ", ";
            } else {
                QTextStream(&typeCheck) << "convertible" << pyType << "Types("
                    << converterObject(firstType) << ", "
                    << (firstType.isPointerToWrapperType() ? "true" : "false")
                    << ", " << converterObject(secondType) << ", "
                    << (secondType.isPointerToWrapperType() ? "true" :"false")
                    << ", ";
            }
        }
        return typeCheck;
    }
    return cpythonCheckFunction(typeEntry);
}

QString ShibokenGenerator::cpythonCheckFunction(TypeEntryCPtr type)
{
    if (type->isCustom()) {
        const auto cte = std::static_pointer_cast<const CustomTypeEntry>(type);
        if (cte->hasCheckFunction())
            return cte->checkFunction();
        throw Exception(msgUnknownCheckFunction(type));
    }

    if (type->isEnum() || type->isFlags() || type->isWrapperType())
        return u"SbkObject_TypeCheck("_s + cpythonTypeNameExt(type) + u", "_s;

    if (type->isPrimitive())
        type = basicReferencedTypeEntry(type);

    if (auto tla = type->targetLangApiType()) {
        if (tla->hasCheckFunction())
            return tla->checkFunction();
    }

    if (isExtendedCppPrimitive(type))
        return pythonPrimitiveTypeName(type->name()) + u"_Check"_s;

    return cpythonIsConvertibleFunction(type);
}

QString ShibokenGenerator::cpythonIsConvertibleFunction(const TypeEntryCPtr &type)
{
    if (type->isWrapperType()) {
        QString result = u"Shiboken::Conversions::"_s;
        bool isValue = false;
        if (type->isValue()) {
            const auto cte = std::static_pointer_cast<const ComplexTypeEntry>(type);
            isValue = !cte->isValueTypeWithCopyConstructorOnly();
        }
        result += isValue ? u"isPythonToCppValueConvertible"_s
                          : u"isPythonToCppPointerConvertible"_s;
        result += u"("_s + cpythonTypeNameExt(type) + u", "_s;
        return result;
    }
    return QString::fromLatin1("Shiboken::Conversions::isPythonToCppConvertible(%1, ")
              .arg(converterObject(type));
}

QString ShibokenGenerator::cpythonIsConvertibleFunction(AbstractMetaType metaType)
{
    const auto typeEntry = metaType.typeEntry();
    if (typeEntry->isCustom()) {
        const auto cte = std::static_pointer_cast<const CustomTypeEntry>(typeEntry);
        if (cte->hasCheckFunction())
            return cte->checkFunction();
        throw Exception(msgUnknownCheckFunction(typeEntry));
    }

    QString result = u"Shiboken::Conversions::"_s;
    if (metaType.generateOpaqueContainer()) {
        result += u"pythonToCppReferenceConversion("_s
                  + converterObject(metaType) + u", "_s;
        return result;
    }
    if (metaType.isWrapperType()) {
        if (metaType.isPointer() || metaType.isValueTypeWithCopyConstructorOnly())
            result += u"pythonToCppPointerConversion"_s;
        else if (metaType.referenceType() == LValueReference)
            result += u"pythonToCppReferenceConversion"_s;
        else
            result += u"pythonToCppValueConversion"_s;
        result += u'(' + cpythonTypeNameExt(metaType) + u", "_s;
        return result;
    }
    result += u"pythonToCppConversion("_s + converterObject(metaType);
    // Write out array sizes if known
    const AbstractMetaTypeList nestedArrayTypes = metaType.nestedArrayTypes();
    if (!nestedArrayTypes.isEmpty() && nestedArrayTypes.constLast().isCppPrimitive()) {
        const int dim1 = metaType.arrayElementCount();
        const int dim2 = nestedArrayTypes.constFirst().isArray()
            ? nestedArrayTypes.constFirst().arrayElementCount() : -1;
        result += u", "_s + QString::number(dim1)
            + u", "_s + QString::number(dim2);
    }
    result += u", "_s;
    return result;
}

QString ShibokenGenerator::cpythonIsConvertibleFunction(const AbstractMetaArgument &metaArg)
{
    return cpythonIsConvertibleFunction(metaArg.type());
}

QString ShibokenGenerator::cpythonToCppConversionFunction(const AbstractMetaClassCPtr &metaClass)
{
    return u"Shiboken::Conversions::pythonToCppPointer("_s
        + cpythonTypeNameExt(metaClass->typeEntry()) + u", "_s;
}

QString ShibokenGenerator::cpythonToCppConversionFunction(const AbstractMetaType &type,
                                                          AbstractMetaClassCPtr  /* context */)
{
    if (type.isWrapperType()) {
        return u"Shiboken::Conversions::pythonToCpp"_s
            + (type.isPointer() ? u"Pointer"_s : u"Copy"_s)
            + u'(' + cpythonTypeNameExt(type) + u", "_s;
    }
    return QStringLiteral("Shiboken::Conversions::pythonToCppCopy(%1, ")
              .arg(converterObject(type));
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const AbstractMetaType &type,
                                                             AbstractMetaClassCPtr  /* context */)
{
    if (type.isWrapperType()) {
        QString conversion;
        if (type.referenceType() == LValueReference
            && !(type.isValue() && type.isConstant()) && !type.isPointer()) {
            conversion = u"reference"_s;
        } else if (type.isValue() || type.isSmartPointer()) {
            conversion = u"copy"_s;
        } else {
            conversion = u"pointer"_s;
        }
        QString result = u"Shiboken::Conversions::"_s + conversion
            + u"ToPython("_s
            + cpythonTypeNameExt(type) + u", "_s;
        if (conversion != u"pointer")
            result += u'&';
        return result;
    }

    const auto indirections = type.indirections() - 1;
    return u"Shiboken::Conversions::copyToPython("_s + converterObject(type)
           + u", "_s + AbstractMetaType::dereferencePrefix(indirections);
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const AbstractMetaClassCPtr &metaClass)
{
    return cpythonToPythonConversionFunction(metaClass->typeEntry());
}

QString ShibokenGenerator::cpythonToPythonConversionFunction(const TypeEntryCPtr &type)
{
    if (type->isWrapperType()) {
        const QString conversion = type->isValue() ? u"copy"_s : u"pointer"_s;
         QString result = u"Shiboken::Conversions::"_s + conversion
             + u"ToPython("_s + cpythonTypeNameExt(type)
             + u", "_s;
         if (conversion != u"pointer")
             result += u'&';
        return result;
    }

    return u"Shiboken::Conversions::copyToPython("_s
           + converterObject(type) + u", &"_s;
}

QString ShibokenGenerator::argumentString(const AbstractMetaFunctionCPtr &func,
                                          const AbstractMetaArgument &argument,
                                          Options options) const
{
    auto type = options.testFlag(OriginalTypeDescription)
        ? argument.type() : argument.modifiedType();


    QString arg = translateType(type, func->implementingClass(), options);

    if (argument.isTypeModified())
        arg.replace(u'$', u'.'); // Haehh?

    // "int a", "int a[]"
    const int arrayPos = arg.indexOf(u'[');
    if (arrayPos != -1)
        arg.insert(arrayPos, u' ' + argument.name());
    else
        arg.append(u' ' + argument.name());

    if ((options & Generator::SkipDefaultValues) != Generator::SkipDefaultValues &&
        !argument.originalDefaultValueExpression().isEmpty())
    {
        QString default_value = argument.originalDefaultValueExpression();
        if (default_value == u"NULL")
            default_value = NULL_PTR;

        //WORKAROUND: fix this please
        if (default_value.startsWith(u"new "))
            default_value.remove(0, 4);

        arg += u" = "_s + default_value;
    }

    return arg;
}

void ShibokenGenerator::writeArgument(TextStream &s,
                                      const AbstractMetaFunctionCPtr &func,
                                      const AbstractMetaArgument &argument,
                                      Options options) const
{
    s << argumentString(func, argument, options);
}

void ShibokenGenerator::writeFunctionArguments(TextStream &s,
                                               const AbstractMetaFunctionCPtr &func,
                                               Options options) const
{
    int argUsed = 0;
    for (const auto &arg : func->arguments()) {
        if (options.testFlag(Generator::SkipRemovedArguments) && arg.isModifiedRemoved())
            continue;

        if (argUsed != 0)
            s << ", ";
        writeArgument(s, func, arg, options);
        argUsed++;
    }
}

GeneratorContext ShibokenGenerator::contextForClass(const AbstractMetaClassCPtr &c) const
{
    GeneratorContext result = Generator::contextForClass(c);
    if (shouldGenerateCppWrapper(c)) {
        result.m_type = GeneratorContext::WrappedClass;
        result.m_wrappername = wrapperName(c);
    }
    return result;
}

QString ShibokenGenerator::functionReturnType(const AbstractMetaFunctionCPtr &func, Options options) const
{
    if (func->isTypeModified() && !options.testFlag(OriginalTypeDescription))
        return func->modifiedTypeName();
    return translateType(func->type(), func->implementingClass(), options);
}

QString ShibokenGenerator::functionSignature(const AbstractMetaFunctionCPtr &func,
                                             const QString &prepend,
                                             const QString &append,
                                             Options options,
                                             int /* argCount */) const
{
    StringStream s(TextStream::Language::Cpp);
    // The actual function
    if (func->isEmptyFunction() || func->needsReturnType())
        s << functionReturnType(func, options) << ' ';
    else
        options |= Generator::SkipReturnType;

    // name
    QString name(func->originalName());
    if (func->isConstructor())
        name = wrapperName(func->ownerClass());

    s << prepend << name << append << '(';
    writeFunctionArguments(s, func, options);
    s << ')';

    if (func->isConstant())
        s << " const";

    if (func->exceptionSpecification() == ExceptionSpecification::NoExcept)
        s << " noexcept";

    return s;
}

void ShibokenGenerator::writeArgumentNames(TextStream &s,
                                           const AbstractMetaFunctionCPtr &func,
                                           Options options)
{
    const AbstractMetaArgumentList arguments = func->arguments();
    int argCount = 0;
    for (const auto &argument : arguments) {
        const int index = argument.argumentIndex() + 1;
        if (options.testFlag(Generator::SkipRemovedArguments) && argument.isModifiedRemoved())
            continue;
        const auto &type = argument.type();
        if (argCount > 0)
            s << ", ";
        const bool isVirtualCall = options.testFlag(Option::VirtualCall);
        const bool useStdMove = isVirtualCall && type.isUniquePointer() && type.passByValue();
        s << (useStdMove ? stdMove(argument.name()) : argument.name());

        if (!isVirtualCall
            && (func->hasConversionRule(TypeSystem::NativeCode, index)
                || func->hasConversionRule(TypeSystem::TargetLangCode, index))
            && !func->isConstructor()) {
           s << CONV_RULE_OUT_VAR_SUFFIX;
        }

        argCount++;
    }
}

void ShibokenGenerator::writeFunctionCall(TextStream &s,
                                          const AbstractMetaFunctionCPtr &func,
                                          Options options)
{
    s << (func->isConstructor() ? func->ownerClass()->qualifiedCppName() : func->originalName())
        << '(';
    writeArgumentNames(s, func, options);
    s << ')';
}

ShibokenGenerator::ExtendedConverterData ShibokenGenerator::getExtendedConverters() const
{
    ExtendedConverterData extConvs;
    for (const auto &metaClass : api().classes()) {
        // Use only the classes for the current module.
        if (!shouldGenerate(metaClass->typeEntry()))
            continue;
        const auto &overloads = metaClass->operatorOverloads(OperatorQueryOption::ConversionOp);
        for (const auto &convOp : overloads) {
            // Get only the conversion operators that return a type from another module,
            // that are value-types and were not removed in the type system.
            const auto convType = convOp->type().typeEntry();
            if (convType->generateCode() || !convType->isValue()
                || convOp->isModifiedRemoved())
                continue;
            extConvs[convType].append(convOp->ownerClass());
        }
    }
    return extConvs;
}

QList<CustomConversionPtr> ShibokenGenerator::getPrimitiveCustomConversions()
{
    QList<CustomConversionPtr> conversions;
    const auto &primitiveTypeList = primitiveTypes();
    for (const auto &type : primitiveTypeList) {
        if (type->shouldGenerate() && isUserPrimitive(type) && type->hasCustomConversion())
            conversions << type->customConversion();
    }
    return conversions;
}

static QString getArgumentsFromMethodCall(const QString &str)
{
    // It would be way nicer to be able to use a Perl like
    // regular expression that accepts temporary variables
    // to count the parenthesis.
    // For more information check this:
    // http://perl.plover.com/yak/regex/samples/slide083.html
    static QLatin1String funcCall("%CPPSELF.%FUNCTION_NAME");
    int pos = str.indexOf(funcCall);
    if (pos == -1)
        return QString();
    pos = pos + funcCall.size();
    while (str.at(pos) == u' ' || str.at(pos) == u'\t')
        ++pos;
    if (str.at(pos) == u'(')
        ++pos;
    int begin = pos;
    int counter = 1;
    while (counter != 0) {
        if (str.at(pos) == u'(')
            ++counter;
        else if (str.at(pos) == u')')
            --counter;
        ++pos;
    }
    return str.mid(begin, pos-begin-1);
}

QString ShibokenGenerator::getCodeSnippets(const CodeSnipList &codeSnips,
                                           TypeSystem::CodeSnipPosition position,
                                           TypeSystem::Language language)
{
    QString code;
    for (const CodeSnip &snip : codeSnips) {
        if ((position != TypeSystem::CodeSnipPositionAny && snip.position != position) || !(snip.language & language))
            continue;
        code.append(snip.code());
    }
    return code;
}

void ShibokenGenerator::processClassCodeSnip(QString &code, const GeneratorContext &context) const
{
    auto metaClass = context.metaClass();
    // Replace template variable by the Python Type object
    // for the class context in which the variable is used.
    code.replace(u"%PYTHONTYPEOBJECT"_s,
                 u"(*"_s + cpythonTypeName(metaClass) + u')');
    const QString className = context.effectiveClassName();
    code.replace(u"%TYPE"_s, className);
    code.replace(u"%CPPTYPE"_s, metaClass->name());

    processCodeSnip(code);
}

void ShibokenGenerator::processCodeSnip(QString &code) const
{
    // replace "toPython" converters
    replaceConvertToPythonTypeSystemVariable(code);

    // replace "toCpp" converters
    replaceConvertToCppTypeSystemVariable(code);

    // replace "isConvertible" check
    replaceIsConvertibleToCppTypeSystemVariable(code);

    // replace "checkType" check
    replaceTypeCheckTypeSystemVariable(code);
}

ShibokenGenerator::ArgumentVarReplacementList
    ShibokenGenerator::getArgumentReplacement(const AbstractMetaFunctionCPtr &func,
                                              bool usePyArgs, TypeSystem::Language language,
                                              const AbstractMetaArgument *lastArg)
{
    ArgumentVarReplacementList argReplacements;
    TypeSystem::Language convLang = (language == TypeSystem::TargetLangCode)
                                    ? TypeSystem::NativeCode : TypeSystem::TargetLangCode;
    int removed = 0;
    for (qsizetype i = 0; i < func->arguments().size(); ++i) {
        const AbstractMetaArgument &arg = func->arguments().at(i);
        QString argValue;
        if (language == TypeSystem::TargetLangCode) {
            const bool hasConversionRule = func->hasConversionRule(convLang, i + 1);
            const bool argRemoved = arg.isModifiedRemoved();
            if (argRemoved)
                ++removed;
            if (argRemoved && hasConversionRule)
                argValue = arg.name() + CONV_RULE_OUT_VAR_SUFFIX;
            else if (argRemoved || (lastArg && arg.argumentIndex() > lastArg->argumentIndex()))
                argValue = CPP_ARG_REMOVED + QString::number(i);
            if (!argRemoved && argValue.isEmpty()) {
                int argPos = i - removed;
                AbstractMetaType type = arg.modifiedType();
                if (type.typeEntry()->isCustom()) {
                    argValue = usePyArgs
                               ? pythonArgsAt(argPos) : PYTHON_ARG;
                } else {
                    argValue = hasConversionRule
                               ? arg.name() + CONV_RULE_OUT_VAR_SUFFIX
                               : CPP_ARG + QString::number(argPos);
                    const auto generatorArg = GeneratorArgument::fromMetaType(type);
                    AbstractMetaType::applyDereference(&argValue, generatorArg.indirections);
                }
            }
        } else {
            argValue = arg.name();
        }
        if (!argValue.isEmpty())
            argReplacements << ArgumentVarReplacementPair(arg, argValue);

    }
    return argReplacements;
}

void ShibokenGenerator::writeClassCodeSnips(TextStream &s,
                                       const CodeSnipList &codeSnips,
                                       TypeSystem::CodeSnipPosition position,
                                       TypeSystem::Language language,
                                       const GeneratorContext &context) const
{
    QString code = getCodeSnippets(codeSnips, position, language);
    if (code.isEmpty())
        return;
    processClassCodeSnip(code, context);
    s << "// Begin code injection\n" << code << "// End of code injection\n\n";
}

void ShibokenGenerator::writeCodeSnips(TextStream &s,
                                       const CodeSnipList &codeSnips,
                                       TypeSystem::CodeSnipPosition position,
                                       TypeSystem::Language language) const
{
    QString code = getCodeSnippets(codeSnips, position, language);
    if (code.isEmpty())
        return;
    processCodeSnip(code);
    s << "// Begin code injection\n" << code << "// End of code injection\n\n";
}

static void replacePyArg0(TypeSystem::Language language, QString *code)
{
    static const QString pyArg0 = u"%PYARG_0"_s;

    if (!code->contains(pyArg0))
        return;
    if (language != TypeSystem::NativeCode) {
        code->replace(pyArg0, PYTHON_RETURN_VAR);
        return;
    }

    // pyResult is an AutoDecRef in overridden methods of wrapper classes which
    // has a cast operator for PyObject *. This may however not work in all
    // situations (fex _PyVarObject_CAST(op) defined as ((PyVarObject*)(op))).
    // Append ".object()" unless it is followed by a '.' indicating explicit
    // AutoDecRef member invocation.
    static const QString pyObject = PYTHON_RETURN_VAR + u".object()"_s;
    qsizetype pos{};
    while ( (pos = code->indexOf(pyArg0)) >= 0) {
        const auto next = pos + pyArg0.size();
        const bool memberInvocation = next < code->size() && code->at(next) == u'.';
        code->replace(pos, pyArg0.size(),
                      memberInvocation ? PYTHON_RETURN_VAR : pyObject);
    }
}

void ShibokenGenerator::writeCodeSnips(TextStream &s,
                                       const CodeSnipList &codeSnips,
                                       TypeSystem::CodeSnipPosition position,
                                       TypeSystem::Language language,
                                       const AbstractMetaFunctionCPtr &func,
                                       bool usePyArgs,
                                       const AbstractMetaArgument *lastArg) const
{
    QString code = getCodeSnippets(codeSnips, position, language);
    if (code.isEmpty())
        return;

    // Replace %PYARG_# variables.
    replacePyArg0(language, &code);

    static const QRegularExpression pyArgsRegex(QStringLiteral("%PYARG_(\\d+)"));
    Q_ASSERT(pyArgsRegex.isValid());
    if (language == TypeSystem::TargetLangCode) {
        if (usePyArgs) {
            code.replace(pyArgsRegex, PYTHON_ARGS + u"[\\1-1]"_s);
        } else {
            static const QRegularExpression pyArgsRegexCheck(QStringLiteral("%PYARG_([2-9]+)"));
            Q_ASSERT(pyArgsRegexCheck.isValid());
            const QRegularExpressionMatch match = pyArgsRegexCheck.match(code);
            if (match.hasMatch()) {
                qCWarning(lcShiboken).noquote().nospace()
                    << msgWrongIndex("%PYARG", match.captured(1), func.get());
                return;
            }
            code.replace(u"%PYARG_1"_s, PYTHON_ARG);
        }
    } else {
        // Replaces the simplest case of attribution to a
        // Python argument on the binding virtual method.
        static const QRegularExpression pyArgsAttributionRegex(QStringLiteral("%PYARG_(\\d+)\\s*=[^=]\\s*([^;]+)"));
        Q_ASSERT(pyArgsAttributionRegex.isValid());
        code.replace(pyArgsAttributionRegex, u"PyTuple_SET_ITEM("_s
                     + PYTHON_ARGS + u", \\1-1, \\2)"_s);
        code.replace(pyArgsRegex, u"PyTuple_GET_ITEM("_s
                     + PYTHON_ARGS + u", \\1-1)"_s);
    }

    // Replace %ARG#_TYPE variables.
    const AbstractMetaArgumentList &arguments = func->arguments();
    for (const AbstractMetaArgument &arg : arguments) {
        QString argTypeVar = u"%ARG"_s + QString::number(arg.argumentIndex() + 1)
                             + u"_TYPE"_s;
        QString argTypeVal = arg.type().cppSignature();
        code.replace(argTypeVar, argTypeVal);
    }

    static const QRegularExpression cppArgTypeRegexCheck(QStringLiteral("%ARG(\\d+)_TYPE"));
    Q_ASSERT(cppArgTypeRegexCheck.isValid());
    QRegularExpressionMatchIterator rit = cppArgTypeRegexCheck.globalMatch(code);
    while (rit.hasNext()) {
        QRegularExpressionMatch match = rit.next();
        qCWarning(lcShiboken).noquote().nospace()
            << msgWrongIndex("%ARG#_TYPE", match.captured(1), func.get());
    }

    // Replace template variable for return variable name.
    if (func->isConstructor()) {
        code.replace(u"%0."_s, u"cptr->"_s);
        code.replace(u"%0"_s, u"cptr"_s);
    } else if (!func->isVoid()) {
        QString returnValueOp = func->type().isPointerToWrapperType()
            ? u"%1->"_s : u"%1."_s;
        if (func->type().isWrapperType())
            code.replace(u"%0."_s, returnValueOp.arg(CPP_RETURN_VAR));
        code.replace(u"%0"_s, CPP_RETURN_VAR);
    }

    // Replace template variable for self Python object.
    QString pySelf = language == TypeSystem::NativeCode
        ? u"pySelf"_s : u"self"_s;
    code.replace(u"%PYSELF"_s, pySelf);

    // Replace template variable for a pointer to C++ of this object.
    if (func->implementingClass()) {
        QString replacement = func->isStatic() ? u"%1::"_s : u"%1->"_s;
        QString cppSelf;
        if (func->isStatic())
            cppSelf = func->ownerClass()->qualifiedCppName();
        else if (language == TypeSystem::NativeCode)
            cppSelf = u"this"_s;
        else
            cppSelf = CPP_SELF_VAR;

        // On comparison operator CPP_SELF_VAR is always a reference.
        if (func->isComparisonOperator())
            replacement = u"%1."_s;

        if (func->isVirtual() && !func->isAbstract() && (!avoidProtectedHack() || !func->isProtected())) {
            QString methodCallArgs = getArgumentsFromMethodCall(code);
            if (!methodCallArgs.isEmpty()) {
                const QString pattern = u"%CPPSELF.%FUNCTION_NAME("_s + methodCallArgs + u')';
                QString replacement = u"(Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject *>("_s
                                      + pySelf + u")) ? "_s;
                if (func->name() == u"metaObject") {
                    QString wrapperClassName = wrapperName(func->ownerClass());
                    QString cppSelfVar = avoidProtectedHack()
                        ? u"%CPPSELF"_s
                        : u"reinterpret_cast<"_s + wrapperClassName + u" *>(%CPPSELF)"_s;
                    replacement += cppSelfVar + u"->::"_s + wrapperClassName
                                   + u"::%FUNCTION_NAME("_s + methodCallArgs
                                   + u") : %CPPSELF.%FUNCTION_NAME("_s + methodCallArgs + u"))"_s;
                } else {
                    replacement += u"%CPPSELF->::%TYPE::%FUNCTION_NAME("_s + methodCallArgs
                                   + u") : %CPPSELF.%FUNCTION_NAME("_s + methodCallArgs + u"))"_s;
                }
                code.replace(pattern, replacement);
            }
        }

        code.replace(u"%CPPSELF."_s, replacement.arg(cppSelf));
        code.replace(u"%CPPSELF"_s, cppSelf);

        if (code.indexOf(u"%BEGIN_ALLOW_THREADS") > -1) {
            if (code.count(u"%BEGIN_ALLOW_THREADS"_s) == code.count(u"%END_ALLOW_THREADS"_s)) {
                code.replace(u"%BEGIN_ALLOW_THREADS"_s, BEGIN_ALLOW_THREADS);
                code.replace(u"%END_ALLOW_THREADS"_s, END_ALLOW_THREADS);
            } else {
                qCWarning(lcShiboken) << "%BEGIN_ALLOW_THREADS and %END_ALLOW_THREADS mismatch";
            }
        }

        // replace template variable for the Python Type object for the
        // class implementing the method in which the code snip is written
        if (func->isStatic()) {
            code.replace(u"%PYTHONTYPEOBJECT"_s,
                         u"(*"_s + cpythonTypeName(func->implementingClass()) + u')');
        } else {
            code.replace(u"%PYTHONTYPEOBJECT."_s, pySelf + u"->ob_type->"_s);
            code.replace(u"%PYTHONTYPEOBJECT"_s, pySelf + u"->ob_type"_s);
        }
    }

    // Replaces template %ARGUMENT_NAMES and %# variables by argument variables and values.
    // Replaces template variables %# for individual arguments.
    const ArgumentVarReplacementList &argReplacements = getArgumentReplacement(func, usePyArgs, language, lastArg);

    QStringList args;
    for (const ArgumentVarReplacementPair &pair : argReplacements) {
        if (pair.second.startsWith(CPP_ARG_REMOVED))
            continue;
        args << pair.second;
    }
    code.replace(u"%ARGUMENT_NAMES"_s, args.join(u", "_s));

    for (const ArgumentVarReplacementPair &pair : argReplacements) {
        const AbstractMetaArgument &arg = pair.first;
        int idx = arg.argumentIndex() + 1;
        AbstractMetaType type = arg.modifiedType();
        if (type.isWrapperType()) {
            QString replacement = pair.second;
            const auto generatorArg = GeneratorArgument::fromMetaType(type);
            if (generatorArg.indirections  > 0)
                AbstractMetaType::stripDereference(&replacement);
            if (type.referenceType() == LValueReference || type.isPointer())
                code.replace(u'%' + QString::number(idx) + u'.', replacement + u"->"_s);
        }
        code.replace(CodeSnipAbstract::placeHolderRegex(idx), pair.second);
    }

    if (language == TypeSystem::NativeCode) {
        // Replaces template %PYTHON_ARGUMENTS variable with a pointer to the Python tuple
        // containing the converted virtual method arguments received from C++ to be passed
        // to the Python override.
        code.replace(u"%PYTHON_ARGUMENTS"_s, PYTHON_ARGS);

        // replace variable %PYTHON_METHOD_OVERRIDE for a pointer to the Python method
        // override for the C++ virtual method in which this piece of code was inserted
        code.replace(u"%PYTHON_METHOD_OVERRIDE"_s, PYTHON_OVERRIDE_VAR);
    }

    if (avoidProtectedHack()) {
        // If the function being processed was added by the user via type system,
        // Shiboken needs to find out if there are other overloads for the same method
        // name and if any of them is of the protected visibility. This is used to replace
        // calls to %FUNCTION_NAME on user written custom code for calls to the protected
        // dispatcher.
        bool isProtected = func->isProtected();
        auto owner = func->ownerClass();
        if (!isProtected && func->isUserAdded() && owner != nullptr) {
            const auto &funcs = getFunctionGroups(owner).value(func->name());
            isProtected = std::any_of(funcs.cbegin(), funcs.cend(),
                                      [](const AbstractMetaFunctionCPtr &f) {
                                          return f->isProtected();
                                      });
        }

        if (isProtected) {
            code.replace(u"%TYPE::%FUNCTION_NAME"_s,
                         QStringLiteral("%1::%2_protected")
                         .arg(wrapperName(func->ownerClass()), func->originalName()));
            code.replace(u"%FUNCTION_NAME"_s,
                         func->originalName() + u"_protected"_s);
        }
    }

    if (func->isConstructor() && shouldGenerateCppWrapper(func->ownerClass()))
        code.replace(u"%TYPE"_s, wrapperName(func->ownerClass()));

    if (func->ownerClass())
        code.replace(u"%CPPTYPE"_s, func->ownerClass()->name());

    replaceTemplateVariables(code, func);

    processCodeSnip(code);
    s << "// Begin code injection\n" << code << "// End of code injection\n\n";
}

// Returns true if the string is an expression,
// and false if it is a variable.
static bool isVariable(const QString &code)
{
    static const QRegularExpression expr(QStringLiteral("^\\s*\\*?\\s*[A-Za-z_][A-Za-z_0-9.]*\\s*(?:\\[[^\\[]+\\])*$"));
    Q_ASSERT(expr.isValid());
    return expr.match(code.trimmed()).hasMatch();
}

// A miniature normalizer that puts a type string into a format
// suitable for comparison with AbstractMetaType::cppSignature()
// result.
static QString miniNormalizer(const QString &varType)
{
    QString normalized = varType.trimmed();
    if (normalized.isEmpty())
        return normalized;
    if (normalized.startsWith(u"::"))
        normalized.remove(0, 2);
    QString suffix;
    while (normalized.endsWith(u'*') || normalized.endsWith(u'&')) {
        suffix.prepend(normalized.at(normalized.size() - 1));
        normalized.chop(1);
        normalized = normalized.trimmed();
    }
    const QString result = normalized + u' ' + suffix;
    return result.trimmed();
}
// The position must indicate the first character after the opening '('.
// ATTENTION: do not modify this function to trim any resulting string!
// This must be done elsewhere.
static QString getConverterTypeSystemVariableArgument(const QString &code, int pos)
{
    QString arg;
    int parenthesisDepth = 0;
    int count = 0;
    while (pos + count < code.size()) {
        char c = code.at(pos+count).toLatin1(); // toAscii is gone
        if (c == '(') {
            ++parenthesisDepth;
        } else if (c == ')') {
            if (parenthesisDepth == 0) {
                arg = code.mid(pos, count).trimmed();
                break;
            }
            --parenthesisDepth;
        }
        ++count;
    }
    if (parenthesisDepth != 0)
        throw Exception("Unbalanced parenthesis on type system converter variable call.");
    return arg;
}

const QHash<int, QString> &ShibokenGenerator::typeSystemConvName()
{
    static const  QHash<int, QString> result = {
        {TypeSystemCheckFunction, u"checkType"_s},
        {TypeSystemIsConvertibleFunction, u"isConvertible"_s},
        {TypeSystemToCppFunction, u"toCpp"_s},
        {TypeSystemToPythonFunction, u"toPython"_s}
    };
    return result;
}

using StringPair = QPair<QString, QString>;

void ShibokenGenerator::replaceConverterTypeSystemVariable(TypeSystemConverterVariable converterVariable,
                                                           QString &code) const
{
    QList<StringPair> replacements;
    QRegularExpressionMatchIterator rit = typeSystemConvRegExps()[converterVariable].globalMatch(code);
    while (rit.hasNext()) {
        const QRegularExpressionMatch match = rit.next();
        const QStringList list = match.capturedTexts();
        QString conversionString = list.constFirst();
        const QString &conversionTypeName = list.constLast();
        QString message;
        const auto conversionTypeO = AbstractMetaType::fromString(conversionTypeName, &message);
        if (!conversionTypeO.has_value()) {
            throw Exception(msgCannotFindType(conversionTypeName,
                                              typeSystemConvName().value(converterVariable),
                                              message));
        }
        const auto conversionType = conversionTypeO.value();
        QString conversion;
        switch (converterVariable) {
            case TypeSystemToCppFunction: {
                StringStream c(TextStream::Language::Cpp);
                int end = match.capturedStart();
                int start = end;
                while (start > 0 && code.at(start) != u'\n')
                    --start;
                while (code.at(start).isSpace())
                    ++start;
                QString varType = code.mid(start, end - start);
                conversionString = varType + list.constFirst();
                varType = miniNormalizer(varType);
                QString varName = list.at(1).trimmed();
                if (!varType.isEmpty()) {
                    c << getFullTypeName(conversionType) << ' ' << varName
                        << minimalConstructorExpression(api(), conversionType) << ";\n";
                }
                c << cpythonToCppConversionFunction(conversionType);
                QString prefix;
                if (!AbstractMetaType::stripDereference(&varName))
                    prefix = u'&';
                QString arg = getConverterTypeSystemVariableArgument(code, match.capturedEnd());
                conversionString += arg;
                c << arg << ", " << prefix << '(' << varName << ')';
                conversion = c.toString();
                break;
            }
            case TypeSystemCheckFunction:
                conversion = cpythonCheckFunction(conversionType);
                if (conversionType.typeEntry()->isPrimitive()
                    && (conversionType.typeEntry()->name() == cPyObjectT()
                        || !conversion.endsWith(u' '))) {
                    conversion += u'(';
                    break;
                }
            Q_FALLTHROUGH();
            case TypeSystemIsConvertibleFunction:
                if (conversion.isEmpty())
                    conversion = cpythonIsConvertibleFunction(conversionType);
            Q_FALLTHROUGH();
            case TypeSystemToPythonFunction:
                if (conversion.isEmpty())
                    conversion = cpythonToPythonConversionFunction(conversionType);
            Q_FALLTHROUGH();
            default: {
                QString arg = getConverterTypeSystemVariableArgument(code, match.capturedEnd());
                conversionString += arg;
                if (converterVariable == TypeSystemToPythonFunction && !isVariable(arg)) {
                    QString m;
                    QTextStream(&m) << "Only variables are acceptable as argument to %%CONVERTTOPYTHON type system variable on code snippet: '"
                        << code << '\'';
                    throw Exception(m);
                }
                if (conversion.contains(u"%in")) {
                    conversion.prepend(u'(');
                    conversion.replace(u"%in"_s, arg);
                } else {
                    conversion += arg;
                }
            }
        }
        replacements.append(qMakePair(conversionString, conversion));
    }
    for (const StringPair &rep : std::as_const(replacements))
        code.replace(rep.first, rep.second);
}

bool ShibokenGenerator::injectedCodeCallsCppFunction(const GeneratorContext &context,
                                                     const AbstractMetaFunctionCPtr &func)
{
    if (func->injectedCodeContains(u"%FUNCTION_NAME("))
        return true;
    QString funcCall = func->originalName() + u'(';
    if (func->isConstructor())
        funcCall.prepend(u"new "_s);
    if (func->injectedCodeContains(funcCall))
        return true;
    if (!func->isConstructor())
        return false;
    if (func->injectedCodeContains(u"new %TYPE("))
        return true;
     const auto owner = func->ownerClass();
     if (!owner->isPolymorphic())
         return false;
    const QString wrappedCtorCall = u"new "_s + context.effectiveClassName() + u'(';
    return func->injectedCodeContains(wrappedCtorCall);
}

bool ShibokenGenerator::useOverrideCaching(const AbstractMetaClassCPtr &metaClass)
{
    return metaClass->isPolymorphic();
}

ShibokenGenerator::AttroCheck
    ShibokenGenerator::checkAttroFunctionNeeds(const AbstractMetaClassCPtr &metaClass) const
{
    AttroCheck result;
    if (metaClass->typeEntry()->isSmartPointer()) {
        result |= AttroCheckFlag::GetattroSmartPointer | AttroCheckFlag::SetattroSmartPointer;
    } else {
        if (getGeneratorClassInfo(metaClass).needsGetattroFunction)
            result |= AttroCheckFlag::GetattroOverloads;
        if (metaClass->queryFirstFunction(metaClass->functions(),
                                          FunctionQueryOption::GetAttroFunction)) {
            result |= AttroCheckFlag::GetattroUser;
        }
        if (usePySideExtensions() && metaClass->qualifiedCppName() == qObjectT())
            result |= AttroCheckFlag::SetattroQObject;
        if (useOverrideCaching(metaClass))
            result |= AttroCheckFlag::SetattroMethodOverride;
        if (metaClass->queryFirstFunction(metaClass->functions(),
                                          FunctionQueryOption::SetAttroFunction)) {
            result |= AttroCheckFlag::SetattroUser;
        }
        // PYSIDE-1255: If setattro is generated for a class inheriting
        // QObject, the property code needs to be generated, too.
        if ((result & AttroCheckFlag::SetattroMask) != 0
            && !result.testFlag(AttroCheckFlag::SetattroQObject)
            && isQObject(metaClass)) {
            result |= AttroCheckFlag::SetattroQObject;
        }
    }
    return result;
}

bool ShibokenGenerator::classNeedsGetattroFunctionImpl(const AbstractMetaClassCPtr &metaClass)
{
    if (!metaClass)
        return false;
    if (metaClass->typeEntry()->isSmartPointer())
        return true;
    const auto &functionGroup = getFunctionGroups(metaClass);
    for (auto it = functionGroup.cbegin(), end = functionGroup.cend(); it != end; ++it) {
        AbstractMetaFunctionCList overloads;
        for (const auto &func : std::as_const(it.value())) {
            if (func->isAssignmentOperator() || func->isConversionOperator()
                || func->isModifiedRemoved()
                || func->isPrivate() || func->ownerClass() != func->implementingClass()
                || func->isConstructor() || func->isOperatorOverload())
                continue;
            overloads.append(func);
        }
        if (overloads.isEmpty())
            continue;
        if (OverloadData::hasStaticAndInstanceFunctions(overloads))
            return true;
    }
    return false;
}

AbstractMetaFunctionCList
    ShibokenGenerator::getMethodsWithBothStaticAndNonStaticMethods(const AbstractMetaClassCPtr &metaClass)
{
    AbstractMetaFunctionCList methods;
    if (metaClass) {
        const auto &functionGroups = getFunctionGroups(metaClass);
        for (auto it = functionGroups.cbegin(), end = functionGroups.cend(); it != end; ++it) {
            AbstractMetaFunctionCList overloads;
            for (const auto &func : std::as_const(it.value())) {
                if (func->isAssignmentOperator() || func->isConversionOperator()
                    || func->isModifiedRemoved()
                    || func->isPrivate() || func->ownerClass() != func->implementingClass()
                    || func->isConstructor() || func->isOperatorOverload())
                    continue;
                overloads.append(func);
            }
            if (overloads.isEmpty())
                continue;
            if (OverloadData::hasStaticAndInstanceFunctions(overloads))
                methods.append(overloads.constFirst());
        }
    }
    return methods;
}

AbstractMetaClassCPtr
    ShibokenGenerator::getMultipleInheritingClass(const AbstractMetaClassCPtr &metaClass)
{
    if (!metaClass || metaClass->baseClassNames().isEmpty())
        return nullptr;
    if (metaClass->baseClassNames().size() > 1)
        return metaClass;
    return getMultipleInheritingClass(metaClass->baseClass());
}

QString ShibokenGenerator::getModuleHeaderFileBaseName(const QString &moduleName)
{
    return moduleCppPrefix(moduleName).toLower() + QStringLiteral("_python");
}

QString ShibokenGenerator::getModuleHeaderFileName(const QString &moduleName)
{
    return getModuleHeaderFileBaseName(moduleName) + QStringLiteral(".h");
}

QString ShibokenGenerator::getPrivateModuleHeaderFileName(const QString &moduleName)
{
    return getModuleHeaderFileBaseName(moduleName) + QStringLiteral("_p.h");
}

IncludeGroupList ShibokenGenerator::classIncludes(const AbstractMetaClassCPtr &metaClass) const
{
    IncludeGroupList result;
    const auto typeEntry = metaClass->typeEntry();
    //Extra includes
    result.append(IncludeGroup{u"Extra includes"_s,
                               typeEntry->extraIncludes()});

    result.append({u"Enum includes"_s, {}});
    for (const auto &cppEnum : metaClass->enums())
        result.back().includes.append(cppEnum.typeEntry()->extraIncludes());

    result.append({u"Argument includes"_s, typeEntry->argumentIncludes()});
    const auto implicitConvs = implicitConversions(typeEntry);
    for (auto &f : implicitConvs) {
        if (f->isConversionOperator()) {
            const auto source = f->ownerClass();
            Q_ASSERT(source);
            result.back().append(source->typeEntry()->include());
        }
    }
    return result;
}

/*
static void dumpFunction(AbstractMetaFunctionList lst)
{
    qDebug() << "DUMP FUNCTIONS: ";
    for (AbstractMetaFunction *func : std::as_const(lst))
        qDebug() << "*" << func->ownerClass()->name()
                        << func->signature()
                        << "Private: " << func->isPrivate()
                        << "Empty: " << func->isEmptyFunction()
                        << "Static:" << func->isStatic()
                        << "Signal:" << func->isSignal()
                        << "ClassImplements: " <<  (func->ownerClass() != func->implementingClass())
                        << "is operator:" << func->isOperatorOverload()
                        << "is global:" << func->isInGlobalScope();
}
*/

static bool isGroupable(const AbstractMetaFunctionCPtr &func)
{
    switch (func->functionType()) {
    case AbstractMetaFunction::DestructorFunction:
    case AbstractMetaFunction::SignalFunction:
    case AbstractMetaFunction::GetAttroFunction:
    case AbstractMetaFunction::SetAttroFunction:
    case AbstractMetaFunction::ArrowOperator: // weird operator overloads
    case AbstractMetaFunction::SubscriptOperator:
        return false;
    default:
        break;
    }
    if (func->isModifiedRemoved() && !func->isAbstract())
        return false;
    return true;
}

static void insertIntoFunctionGroups(const AbstractMetaFunctionCList &lst,
                                     ShibokenGenerator::FunctionGroups *results)
{
    for (const auto &func : lst) {
        if (isGroupable(func))
            (*results)[func->name()].append(func);
    }
}

ShibokenGenerator::FunctionGroups ShibokenGenerator::getGlobalFunctionGroups() const
{
    FunctionGroups results;
    insertIntoFunctionGroups(api().globalFunctions(), &results);
    for (const auto &nsp : invisibleTopNamespaces())
        insertIntoFunctionGroups(nsp->functions(), &results);
    return results;
}

const GeneratorClassInfoCacheEntry &
    ShibokenGenerator::getGeneratorClassInfo(const AbstractMetaClassCPtr &scope)
{
    auto cache = generatorClassInfoCache();
    auto it = cache->find(scope);
    if (it == cache->end()) {
        it = cache->insert(scope, {});
        it.value().functionGroups = getFunctionGroupsImpl(scope);
        it.value().needsGetattroFunction = classNeedsGetattroFunctionImpl(scope);
    }
    return it.value();
}

ShibokenGenerator::FunctionGroups
    ShibokenGenerator::getFunctionGroups(const AbstractMetaClassCPtr &scope)
{
    Q_ASSERT(scope);
    return getGeneratorClassInfo(scope).functionGroups;
}

// Use non-const overloads only, for example, "foo()" and "foo()const"
// the second is removed.
static void removeConstOverloads(AbstractMetaFunctionCList *overloads)
{
    for (qsizetype i = overloads->size() - 1; i >= 0; --i) {
        const auto &f = overloads->at(i);
        if (f->isConstant()) {
            for (qsizetype c = 0, size = overloads->size(); c < size; ++c) {
                if (f->isConstOverloadOf(overloads->at(c).get())) {
                    overloads->removeAt(i);
                    break;
                }
            }
        }
    }
}

ShibokenGenerator::FunctionGroups
    ShibokenGenerator::getFunctionGroupsImpl(const AbstractMetaClassCPtr &scope)
{
    AbstractMetaFunctionCList lst = scope->functions();
    scope->getFunctionsFromInvisibleNamespacesToBeGenerated(&lst);

    FunctionGroups results;
    for (const auto &func : lst) {
        if (isGroupable(func)
            && func->ownerClass() == func->implementingClass()
            && func->generateBinding()) {
            auto it = results.find(func->name());
            if (it == results.end()) {
                it = results.insert(func->name(), AbstractMetaFunctionCList(1, func));
            } else {
                // If there are virtuals methods in the mix (PYSIDE-570,
                // QFileSystemModel::index(QString,int) and
                // QFileSystemModel::index(int,int,QModelIndex)) override, make sure
                // the overriding method of the most-derived class is seen first
                // and inserted into the "seenSignatures" set.
                if (func->isVirtual())
                    it.value().prepend(func);
                else
                    it.value().append(func);
            }
            getInheritedOverloads(scope, &it.value());
            removeConstOverloads(&it.value());
        }
    }
    return results;
}

static bool hidesBaseClassFunctions(const AbstractMetaFunctionCPtr &f)
{
    return 0 == (f->attributes()
                 & (AbstractMetaFunction::OverriddenCppMethod | AbstractMetaFunction::FinalCppMethod));
}

void ShibokenGenerator::getInheritedOverloads(const AbstractMetaClassCPtr &scope,
                                             AbstractMetaFunctionCList *overloads)
{
    if (overloads->isEmpty() || scope->isNamespace() || scope->baseClasses().isEmpty())
        return;

    // PYSIDE-331: look also into base classes. Check for any non-overriding
    // function hiding the base class functions.
    const bool hideBaseClassFunctions =
        std::any_of(overloads->cbegin(), overloads->cend(), hidesBaseClassFunctions);

    const QString &functionName = overloads->constFirst()->name();
    const bool hasUsingDeclarations = scope->hasUsingMemberFor(functionName);
    if (hideBaseClassFunctions && !hasUsingDeclarations)
        return; // No base function is visible

    // Collect base candidates by name and signature
    bool staticEncountered = false;
    QSet<QString> seenSignatures;
    for (const auto &func : *overloads) {
        seenSignatures.insert(func->minimalSignature());
        staticEncountered |= func->isStatic();
    }

    AbstractMetaFunctionCList baseCandidates;

    auto basePredicate = [&functionName, &seenSignatures, &baseCandidates]
                         (const AbstractMetaClassCPtr &b) {
        for (const auto &f : b->functions()) {
            if (f->generateBinding() && f->name() == functionName) {
                const QString signature = f->minimalSignature();
                if (!seenSignatures.contains(signature)) {
                    seenSignatures.insert(signature);
                    baseCandidates.append(f);
                }
            }
        }
        return false; // Keep going
    };

    for (const auto &baseClass : scope->baseClasses())
        recurseClassHierarchy(baseClass, basePredicate);

    // Remove the ones that are not made visible with using declarations
    if (hideBaseClassFunctions && hasUsingDeclarations) {
        const auto pred =  [scope](const AbstractMetaFunctionCPtr &f) {
            return !scope->isUsingMember(f->ownerClass(), f->name(), f->access());
        };
        auto end = std::remove_if(baseCandidates.begin(), baseCandidates.end(), pred);
        baseCandidates.erase(end, baseCandidates.end());
    }

    // PYSIDE-886: If the method does not have any static overloads declared
    // in the class in question, remove all inherited static methods as setting
    // METH_STATIC in that case can cause crashes for the instance methods.
    // Manifested as crash when calling QPlainTextEdit::find() (clash with
    // static QWidget::find(WId)).
    if (!staticEncountered) {
        auto end =  std::remove_if(baseCandidates.begin(), baseCandidates.end(),
                                   [](const AbstractMetaFunctionCPtr &f) { return f->isStatic(); });
         baseCandidates.erase(end, baseCandidates.end());
    }

    for (const auto &baseCandidate : baseCandidates) {
        AbstractMetaFunction *newFunc = baseCandidate->copy();
        newFunc->setImplementingClass(scope);
        overloads->append(AbstractMetaFunctionCPtr(newFunc));
    }
}

Generator::OptionDescriptions ShibokenGenerator::options() const
{
    auto result = Generator::options();
    result.append({
        {QLatin1StringView(DISABLE_VERBOSE_ERROR_MESSAGES),
         u"Disable verbose error messages. Turn the python code hard to debug\n"
          "but safe few kB on the generated bindings."_s},
        {QLatin1StringView(PARENT_CTOR_HEURISTIC),
         u"Enable heuristics to detect parent relationship on constructors."_s},
        {QLatin1StringView(RETURN_VALUE_HEURISTIC),
         u"Enable heuristics to detect parent relationship on return values\n"
          "(USE WITH CAUTION!)"_s},
        {QLatin1StringView(USE_ISNULL_AS_NB_NONZERO),
         u"If a class have an isNull() const method, it will be used to compute\n"
          "the value of boolean casts"_s},
        {QLatin1StringView(LEAN_HEADERS),
         u"Forward declare classes in module headers"_s},
        {QLatin1StringView(USE_OPERATOR_BOOL_AS_NB_NONZERO),
         u"If a class has an operator bool, it will be used to compute\n"
          "the value of boolean casts"_s},
        {QLatin1StringView(NO_IMPLICIT_CONVERSIONS),
         u"Do not generate implicit_conversions for function arguments."_s},
        {QLatin1StringView(WRAPPER_DIAGNOSTICS),
         u"Generate diagnostic code around wrappers"_s}
    });
    return result;
}

bool ShibokenGenerator::handleOption(const QString &key, const QString &value)
{
    if (Generator::handleOption(key, value))
        return true;
    if (key == QLatin1StringView(PARENT_CTOR_HEURISTIC))
        return (m_useCtorHeuristic = true);
    if (key == QLatin1StringView(RETURN_VALUE_HEURISTIC))
        return (m_userReturnValueHeuristic = true);
    if (key == QLatin1StringView(DISABLE_VERBOSE_ERROR_MESSAGES))
        return (m_verboseErrorMessagesDisabled = true);
    if (key == QLatin1StringView(USE_ISNULL_AS_NB_NONZERO))
        return (m_useIsNullAsNbNonZero = true);
    if (key == QLatin1StringView(LEAN_HEADERS))
        return (m_leanHeaders= true);
    if (key == QLatin1StringView(USE_OPERATOR_BOOL_AS_NB_NONZERO))
        return (m_useOperatorBoolAsNbNonZero = true);
    if (key == QLatin1StringView(NO_IMPLICIT_CONVERSIONS)) {
        m_generateImplicitConversions = false;
        return true;
    }
    if (key == QLatin1StringView(WRAPPER_DIAGNOSTICS))
        return (m_wrapperDiagnostics = true);
    return false;
}

bool ShibokenGenerator::doSetup()
{
    return true;
}

bool ShibokenGenerator::useCtorHeuristic() const
{
    return m_useCtorHeuristic;
}

bool ShibokenGenerator::useReturnValueHeuristic() const
{
    return m_userReturnValueHeuristic;
}

bool ShibokenGenerator::useIsNullAsNbNonZero() const
{
    return m_useIsNullAsNbNonZero;
}

bool ShibokenGenerator::leanHeaders() const
{
    return m_leanHeaders;
}

bool ShibokenGenerator::useOperatorBoolAsNbNonZero() const
{
    return m_useOperatorBoolAsNbNonZero;
}

bool ShibokenGenerator::generateImplicitConversions() const
{
    return m_generateImplicitConversions;
}

QString ShibokenGenerator::moduleCppPrefix(const QString &moduleName)
 {
    QString result = moduleName.isEmpty() ? packageName() : moduleName;
    result.replace(u'.', u'_');
    return result;
}

QString ShibokenGenerator::cppApiVariableName(const QString &moduleName)
{
    return u"Sbk"_s + moduleCppPrefix(moduleName) + u"Types"_s;
}

QString ShibokenGenerator::pythonModuleObjectName(const QString &moduleName)
{
    return u"Sbk"_s + moduleCppPrefix(moduleName) + u"ModuleObject"_s;
}

QString ShibokenGenerator::convertersVariableName(const QString &moduleName)
{
    QString result = cppApiVariableName(moduleName);
    result.chop(1);
    result.append(u"Converters"_s);
    return result;
}

static QString processInstantiationsVariableName(const AbstractMetaType &type)
{
    QString res = u'_' + _fixedCppTypeName(type.typeEntry()->qualifiedCppName()).toUpper();
    for (const auto &instantiation : type.instantiations()) {
        res += instantiation.isContainer()
               ? processInstantiationsVariableName(instantiation)
               : u'_' + _fixedCppTypeName(instantiation.cppSignature()).toUpper();
    }
    return res;
}

static void appendIndexSuffix(QString *s)
{
    if (!s->endsWith(u'_'))
        s->append(u'_');
    s->append(QStringLiteral("IDX"));
}

QString
    ShibokenGenerator::getTypeAlternateTemplateIndexVariableName(const AbstractMetaClassCPtr &metaClass)
{
    const auto templateBaseClass = metaClass->templateBaseClass();
    Q_ASSERT(templateBaseClass);
    QString result = u"SBK_"_s
        + _fixedCppTypeName(templateBaseClass->typeEntry()->qualifiedCppName()).toUpper();
    for (const auto &instantiation : metaClass->templateBaseClassInstantiations())
        result += processInstantiationsVariableName(instantiation);
    appendIndexSuffix(&result);
    return result;
}

QString ShibokenGenerator::getTypeIndexVariableName(const AbstractMetaClassCPtr &metaClass)
{
    return getTypeIndexVariableName(metaClass->typeEntry());
}
QString ShibokenGenerator::getTypeIndexVariableName(TypeEntryCPtr type)
{
    if (isCppPrimitive(type))
        type = basicReferencedTypeEntry(type);
    QString result = u"SBK_"_s;
    // Disambiguate namespaces per module to allow for extending them.
    if (type->isNamespace()) {
        QString package = type->targetLangPackage();
        const int dot = package.lastIndexOf(u'.');
        result += QStringView{package}.right(package.size() - (dot + 1));
    }
    result += _fixedCppTypeName(type->qualifiedCppName()).toUpper();
    appendIndexSuffix(&result);
    return result;
}
QString ShibokenGenerator::getTypeIndexVariableName(const AbstractMetaType &type)
{
    QString result = u"SBK"_s;
    if (type.typeEntry()->isContainer())
        result += u'_' + moduleName().toUpper();
    result += processInstantiationsVariableName(type);
    appendIndexSuffix(&result);
    return result;
}

bool ShibokenGenerator::verboseErrorMessagesDisabled() const
{
    return m_verboseErrorMessagesDisabled;
}

bool ShibokenGenerator::pythonFunctionWrapperUsesListOfArguments(const AbstractMetaFunctionCPtr &func) const
{
    const auto &groups = func->implementingClass()
       ? getFunctionGroups(func->implementingClass())
       : getGlobalFunctionGroups();
    OverloadData od(groups.value(func->name()), api());
    return od.pythonFunctionWrapperUsesListOfArguments();
}

QString ShibokenGenerator::minimalConstructorExpression(const ApiExtractorResult &api,
                                                          const AbstractMetaType &type)
{
    if (type.isExtendedCppPrimitive() || type.isSmartPointer())
        return {};
    QString errorMessage;
    const auto ctor = minimalConstructor(api, type, &errorMessage);
    if (ctor.has_value())
        return ctor->initialization();

    const QString message =
        msgCouldNotFindMinimalConstructor(QLatin1StringView(__FUNCTION__),
                                          type.cppSignature(), errorMessage);
    qCWarning(lcShiboken()).noquote() << message;
    return u";\n#error "_s + message + u'\n';
}

QString ShibokenGenerator::minimalConstructorExpression(const ApiExtractorResult &api,
                                                        const TypeEntryCPtr &type)
{
    if (isExtendedCppPrimitive(type))
        return {};
    const auto ctor = minimalConstructor(api, type);
    if (ctor.has_value())
        return ctor->initialization();

    const QString message =
        msgCouldNotFindMinimalConstructor(QLatin1StringView(__FUNCTION__),
                                          type->qualifiedCppName());
    qCWarning(lcShiboken()).noquote() << message;
    return u";\n#error "_s + message + u'\n';
}

QString ShibokenGenerator::pythonArgsAt(int i)
{
    return PYTHON_ARGS + u'[' + QString::number(i) + u']';
}

void ShibokenGenerator::replaceTemplateVariables(QString &code,
                                                 const AbstractMetaFunctionCPtr &func) const
{
    const auto cpp_class = func->ownerClass();
    if (cpp_class)
        code.replace(u"%TYPE"_s, cpp_class->name());

    const AbstractMetaArgumentList &argument = func->arguments();
    for (const AbstractMetaArgument &arg : argument)
        code.replace(u'%' + QString::number(arg.argumentIndex() + 1), arg.name());

    //template values
    code.replace(u"%RETURN_TYPE"_s, translateType(func->type(), cpp_class));
    code.replace(u"%FUNCTION_NAME"_s, func->originalName());

    if (code.contains(u"%ARGUMENT_NAMES")) {
        StringStream aux_stream;
        writeArgumentNames(aux_stream, func, Generator::SkipRemovedArguments);
        code.replace(u"%ARGUMENT_NAMES"_s, aux_stream);
    }

    if (code.contains(u"%ARGUMENTS")) {
        StringStream aux_stream;
        writeFunctionArguments(aux_stream, func, Options(SkipDefaultValues) | SkipRemovedArguments);
        code.replace(u"%ARGUMENTS"_s, aux_stream);
    }
}

QString ShibokenGenerator::stdMove(const QString &c)
{
    return u"std::move("_s + c + u')';
}

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "pysideqmlregistertype.h"
#include "pysideqmlregistertype_p.h"
#include "pysideqmltypeinfo_p.h"
#include "pysideqmlattached_p.h"
#include "pysideqmlextended_p.h"

#include <limits>

// shiboken
#include <shiboken.h>
#include <sbkstring.h>

// pyside
#include <pyside.h>
#include <pysideqobject.h>
#include <pyside_p.h>

#include <QtCore/QMutex>
#include <QtCore/QTypeRevision>

#include <QtQml/qqml.h>
#include <QtQml/QJSValue>
#include <QtQml/QQmlListProperty>
#include <private/qqmlmetatype_p.h>

static PySide::Qml::QuickRegisterItemFunction quickRegisterItemFunction = nullptr;

static void createInto(void *memory, void *type)
{
    QMutexLocker locker(&PySide::nextQObjectMemoryAddrMutex());
    PySide::setNextQObjectMemoryAddr(memory);
    Shiboken::GilState state;
    PyObject *obj = PyObject_CallObject(reinterpret_cast<PyObject *>(type), 0);
    if (!obj || PyErr_Occurred())
        PyErr_Print();
    PySide::setNextQObjectMemoryAddr(nullptr);
}

PyTypeObject *qObjectType()
{
    static PyTypeObject *const result =
        Shiboken::Conversions::getPythonTypeObject("QObject*");
    assert(result);
    return result;
}

static PyTypeObject *qQmlEngineType()
{
    static PyTypeObject *const result =
        Shiboken::Conversions::getPythonTypeObject("QQmlEngine*");
    assert(result);
    return result;
}

static PyTypeObject *qQJSValueType()
{
    static PyTypeObject *const result =
        Shiboken::Conversions::getPythonTypeObject("QJSValue*");
    assert(result);
    return result;
}

// Check if o inherits from baseClass
static bool inheritsFrom(const QMetaObject *o, const char *baseClass)
{
    for (auto *base = o->superClass(); base ; base = base->superClass()) {
        if (qstrcmp(base->className(), baseClass) == 0)
            return true;
    }
    return false;
}

// Check if o inherits from QPyQmlPropertyValueSource.
static inline bool isQmlPropertyValueSource(const QMetaObject *o)
{
    return inheritsFrom(o, "QPyQmlPropertyValueSource");
}

// Check if o inherits from QQmlParserStatus.
static inline bool isQmlParserStatus(const QMetaObject *o)
{
    return inheritsFrom(o, "QPyQmlParserStatus");
}

namespace PySide::Qml {

int qmlRegisterType(PyObject *pyObj, const char *uri, int versionMajor,
                    int versionMinor, const char *qmlName, const char *noCreationReason,
                    bool creatable)
{
    using namespace Shiboken;

    PyTypeObject *qobjectType = qObjectType();

    PyTypeObject *pyObjType = reinterpret_cast<PyTypeObject *>(pyObj);
    if (!PySequence_Contains(pyObjType->tp_mro, reinterpret_cast<PyObject *>(qobjectType))) {
        PyErr_Format(PyExc_TypeError, "A type inherited from %s expected, got %s.",
                     qobjectType->tp_name, pyObjType->tp_name);
        return -1;
    }

    const QMetaObject *metaObject = PySide::retrieveMetaObject(pyObjType);
    Q_ASSERT(metaObject);

    QQmlPrivate::RegisterType type;

    // Allow registering Qt Quick items.
    const bool isQuickType = quickRegisterItemFunction && quickRegisterItemFunction(pyObj, &type);

    // Register as simple QObject rather than Qt Quick item.
    // Incref the type object, don't worry about decref'ing it because
    // there's no way to unregister a QML type.
    Py_INCREF(pyObj);

    type.structVersion = 0;

    const QByteArray typeName(pyObjType->tp_name);
    QByteArray ptrType = typeName + '*';
    QByteArray listType = QByteArrayLiteral("QQmlListProperty<") + typeName + '>';

    type.typeId = QMetaType(new QQmlMetaTypeInterface(ptrType));
    type.listId = QMetaType(new QQmlListMetaTypeInterface(listType,
                                                          type.typeId.iface()));
    const auto typeInfo = qmlTypeInfo(pyObj);
    auto info = qmlAttachedInfo(pyObjType, typeInfo);
    type.attachedPropertiesFunction = info.factory;
    type.attachedPropertiesMetaObject = info.metaObject;

    if (!isQuickType) { // values filled by the Quick registration
        // QPyQmlParserStatus inherits QObject, QQmlParserStatus, so,
        // it is found behind the QObject.
        type.parserStatusCast = isQmlParserStatus(metaObject)
            ? int(sizeof(QObject))
            : QQmlPrivate::StaticCastSelector<QObject, QQmlParserStatus>::cast();
        // Similar for QPyQmlPropertyValueSource
        type.valueSourceCast = isQmlPropertyValueSource(metaObject)
            ? int(sizeof(QObject))
            : QQmlPrivate::StaticCastSelector<QObject, QQmlPropertyValueSource>::cast();
        type.valueInterceptorCast =
                QQmlPrivate::StaticCastSelector<QObject, QQmlPropertyValueInterceptor>::cast();
    }

    int objectSize = static_cast<int>(PySide::getSizeOfQObject(
                                      reinterpret_cast<PyTypeObject *>(pyObj)));
    type.objectSize = objectSize;
    type.create = creatable ? createInto : nullptr;
    type.noCreationReason = QString::fromUtf8(noCreationReason);
    type.userdata = pyObj;
    type.uri = uri;
    type.version = QTypeRevision::fromVersion(versionMajor, versionMinor);
    type.elementName = qmlName;

    info = qmlExtendedInfo(pyObj, typeInfo);
    type.extensionObjectCreate = info.factory;
    type.extensionMetaObject = info.metaObject;
    type.customParser = 0;
    type.metaObject = metaObject; // Snapshot may have changed.

    int qmlTypeId = QQmlPrivate::qmlregister(QQmlPrivate::TypeRegistration, &type);
    if (qmlTypeId == -1) {
        PyErr_Format(PyExc_TypeError, "QML meta type registration of \"%s\" failed.",
                     qmlName);
    }
    return qmlTypeId;
}

int qmlRegisterSingletonType(PyObject *pyObj, const char *uri, int versionMajor,
                             int versionMinor, const char *qmlName, PyObject *callback,
                             bool isQObject, bool hasCallback)
{
    using namespace Shiboken;

    if (hasCallback) {
        if (!PyCallable_Check(callback)) {
            PyErr_Format(PyExc_TypeError, "Invalid callback specified.");
            return -1;
        }

        AutoDecRef funcCode(PyObject_GetAttrString(callback, "__code__"));
        AutoDecRef argCount(PyObject_GetAttrString(funcCode, "co_argcount"));

        int count = PyLong_AsLong(argCount);

        if (count != 1) {
            PyErr_Format(PyExc_TypeError, "Callback has a bad parameter count.");
            return -1;
        }

        // Make sure the callback never gets deallocated
        Py_INCREF(callback);
    }

    const QMetaObject *metaObject = nullptr;

    if (isQObject) {
        PyTypeObject *pyObjType = reinterpret_cast<PyTypeObject *>(pyObj);

        if (!isQObjectDerived(pyObjType, true))
            return -1;

        // If we don't have a callback we'll need the pyObj to stay allocated indefinitely
        if (!hasCallback)
            Py_INCREF(pyObj);

        metaObject = PySide::retrieveMetaObject(pyObjType);
        Q_ASSERT(metaObject);
    }

    QQmlPrivate::RegisterSingletonType type;
    type.structVersion = 0;

    type.uri = uri;
    type.version = QTypeRevision::fromVersion(versionMajor, versionMinor);
    type.typeName = qmlName;
    type.instanceMetaObject = metaObject;

    if (isQObject) {
        // FIXME: Fix this to assign new type ids each time.
        type.typeId = QMetaType(QMetaType::QObjectStar);

        type.qObjectApi =
            [callback, pyObj, hasCallback](QQmlEngine *engine, QJSEngine *) -> QObject * {
                Shiboken::GilState gil;
                AutoDecRef args(PyTuple_New(hasCallback ? 1 : 0));

                if (hasCallback) {
                    PyTuple_SET_ITEM(args, 0, Conversions::pointerToPython(
                                     qQmlEngineType(), engine));
                }

                AutoDecRef retVal(PyObject_CallObject(hasCallback ? callback : pyObj, args));

                // Make sure the callback returns something we can convert, else the entire application will crash.
                if (retVal.isNull() ||
                    Conversions::isPythonToCppPointerConvertible(qObjectType(), retVal) == nullptr) {
                    PyErr_Format(PyExc_TypeError, "Callback returns invalid value.");
                    return nullptr;
                }

                QObject *obj = nullptr;
                Conversions::pythonToCppPointer(qObjectType(), retVal, &obj);

                if (obj != nullptr)
                    Py_INCREF(retVal);

                return obj;
            };
    } else {
        type.scriptApi =
            [callback](QQmlEngine *engine, QJSEngine *) -> QJSValue {
                Shiboken::GilState gil;
                AutoDecRef args(PyTuple_New(1));

                PyTuple_SET_ITEM(args, 0, Conversions::pointerToPython(
                                 qQmlEngineType(), engine));

                AutoDecRef retVal(PyObject_CallObject(callback, args));

                PyTypeObject *qjsvalueType = qQJSValueType();

                // Make sure the callback returns something we can convert, else the entire application will crash.
                if (retVal.isNull() ||
                    Conversions::isPythonToCppPointerConvertible(qjsvalueType, retVal) == nullptr) {
                    PyErr_Format(PyExc_TypeError, "Callback returns invalid value.");
                    return QJSValue(QJSValue::UndefinedValue);
                }

                QJSValue *val = nullptr;
                Conversions::pythonToCppPointer(qjsvalueType, retVal, &val);

                Py_INCREF(retVal);

                return *val;
            };
    }

    return QQmlPrivate::qmlregister(QQmlPrivate::SingletonRegistration, &type);
}

int qmlRegisterSingletonInstance(PyObject *pyObj, const char *uri, int versionMajor,
                                 int versionMinor, const char *qmlName,
                                 PyObject *instanceObject)
{
    using namespace Shiboken;

    // Check if the Python Type inherit from QObject
    PyTypeObject *pyObjType = reinterpret_cast<PyTypeObject *>(pyObj);

    if (!isQObjectDerived(pyObjType, true))
        return -1;

    // Convert the instanceObject (PyObject) into a QObject
    QObject *instanceQObject = PySide::convertToQObject(instanceObject, true);
    if (instanceQObject == nullptr)
        return -1;

    // Create Singleton Functor to pass the QObject to the Type registration step
    // similarly to the case when we have a callback
    QQmlPrivate::SingletonInstanceFunctor registrationFunctor;
    registrationFunctor.m_object = instanceQObject;

    const QMetaObject *metaObject = PySide::retrieveMetaObject(pyObjType);
    Q_ASSERT(metaObject);

    QQmlPrivate::RegisterSingletonType type;
    type.structVersion = 0;

    type.uri = uri;
    type.version = QTypeRevision::fromVersion(versionMajor, versionMinor);
    type.typeName = qmlName;
    type.instanceMetaObject = metaObject;

    // FIXME: Fix this to assign new type ids each time.
    type.typeId = QMetaType(QMetaType::QObjectStar);
    type.qObjectApi = registrationFunctor;


    return QQmlPrivate::qmlregister(QQmlPrivate::SingletonRegistration, &type);
}

} // namespace PySide::Qml

static std::string getGlobalString(const char *name)
{
    using Shiboken::AutoDecRef;

    PyObject *globals = PyEval_GetGlobals();

    AutoDecRef pyName(Py_BuildValue("s", name));

    PyObject *globalVar = PyDict_GetItem(globals, pyName);

    if (globalVar == nullptr || !PyUnicode_Check(globalVar))
        return "";

    const char *stringValue = _PepUnicode_AsString(globalVar);
    return stringValue != nullptr ? stringValue : "";
}

static int getGlobalInt(const char *name)
{
    using Shiboken::AutoDecRef;

    PyObject *globals = PyEval_GetGlobals();

    AutoDecRef pyName(Py_BuildValue("s", name));

    PyObject *globalVar = PyDict_GetItem(globals, pyName);

    if (globalVar == nullptr || !PyLong_Check(globalVar))
        return -1;

    long value = PyLong_AsLong(globalVar);

    if (value > std::numeric_limits<int>::max() || value < std::numeric_limits<int>::min())
        return -1;

    return value;
}

enum class RegisterMode {
    Normal,
    Anonymous,
    Uncreatable,
    Singleton
};

static PyObject *qmlElementMacroHelper(PyObject *pyObj,
                                       const char *decoratorName,
                                       const char *typeName = nullptr,
                                       RegisterMode mode = RegisterMode::Normal,
                                       const char *noCreationReason = nullptr)
{
    if (!PyType_Check(pyObj)) {
        PyErr_Format(PyExc_TypeError, "This decorator can only be used on classes.");
        return nullptr;
    }

    PyTypeObject *pyObjType = reinterpret_cast<PyTypeObject *>(pyObj);
    if (typeName == nullptr)
        typeName = pyObjType->tp_name;
    if (!PySequence_Contains(pyObjType->tp_mro, reinterpret_cast<PyObject *>(qObjectType()))) {
        PyErr_Format(PyExc_TypeError, "This decorator can only be used with classes inherited from QObject, got %s.",
                     typeName);
        return nullptr;
    }

    std::string importName = getGlobalString("QML_IMPORT_NAME");
    int majorVersion = getGlobalInt("QML_IMPORT_MAJOR_VERSION");
    int minorVersion = getGlobalInt("QML_IMPORT_MINOR_VERSION");

    if (importName.empty()) {
        PyErr_Format(PyExc_TypeError, "You need specify QML_IMPORT_NAME in order to use %s.",
                     decoratorName);
        return nullptr;
    }

    if (majorVersion == -1) {
       PyErr_Format(PyExc_TypeError, "You need specify QML_IMPORT_MAJOR_VERSION in order to use %s.",
                    decoratorName);
       return nullptr;
    }

    // Specifying a minor version is optional
    if (minorVersion == -1)
        minorVersion = 0;

    const char *uri = importName.c_str();
    const int result = mode == RegisterMode::Singleton
        ? PySide::Qml::qmlRegisterSingletonType(pyObj, uri, majorVersion, minorVersion,
                                                typeName, nullptr,
                                                PySide::isQObjectDerived(pyObjType, false),
                                                false)
        : PySide::Qml::qmlRegisterType(pyObj, uri, majorVersion, minorVersion,
                                       mode != RegisterMode::Anonymous ? typeName : nullptr,
                                       noCreationReason,
                                       mode == RegisterMode::Normal);

    if (result == -1) {
        PyErr_Format(PyExc_TypeError, "%s: Failed to register type %s.",
                     decoratorName, typeName);
    }

    return pyObj;
}

namespace PySide::Qml {

PyObject *qmlElementMacro(PyObject *pyObj, const char *decoratorName,
                          const char *typeName = nullptr)
{
    RegisterMode mode = RegisterMode::Normal;
    const char *noCreationReason = nullptr;
    const auto info = PySide::Qml::qmlTypeInfo(pyObj);
    auto *registerObject = pyObj;
    if (info) {
        if (info->flags.testFlag(PySide::Qml::QmlTypeFlag::Singleton))
            mode = RegisterMode::Singleton;
        else if (info->flags.testFlag(PySide::Qml::QmlTypeFlag::Uncreatable))
            mode = RegisterMode::Uncreatable;
        noCreationReason = info->noCreationReason.c_str();
        if (info->foreignType)
            registerObject = reinterpret_cast<PyObject *>(info->foreignType);
    }
    if (!qmlElementMacroHelper(registerObject, decoratorName, typeName, mode, noCreationReason))
        return nullptr;
    return pyObj;
}

PyObject *qmlElementMacro(PyObject *pyObj)
{
    return qmlElementMacro(pyObj, "QmlElement");
}

PyObject *qmlNamedElementMacro(PyObject *pyObj, const char *typeName)
{
    return qmlElementMacro(pyObj, "QmlNamedElement", qstrdup(typeName));
}

PyObject *qmlAnonymousMacro(PyObject *pyObj)
{
    return qmlElementMacroHelper(pyObj, "QmlAnonymous", nullptr,
                                 RegisterMode::Anonymous);
}

PyObject *qmlSingletonMacro(PyObject *pyObj)
{
    PySide::Qml::ensureQmlTypeInfo(pyObj)->flags.setFlag(PySide::Qml::QmlTypeFlag::Singleton);
    Py_INCREF(pyObj);
    return pyObj;
}

QuickRegisterItemFunction getQuickRegisterItemFunction()
{
    return quickRegisterItemFunction;
}

void setQuickRegisterItemFunction(QuickRegisterItemFunction function)
{
    quickRegisterItemFunction = function;
}

} // namespace PySide::Qml

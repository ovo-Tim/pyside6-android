// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qobjectconnect.h"
#include "pysideqobject.h"
#include "pysidesignal.h"
#include "pysideutils.h"
#include "signalmanager.h"

#include "shiboken.h"
#include "basewrapper.h"
#include "autodecref.h"

#include <QtCore/QDebug>
#include <QtCore/QMetaMethod>
#include <QtCore/QObject>

static bool isMethodDecorator(PyObject *method, bool is_pymethod, PyObject *self)
{
    Shiboken::AutoDecRef methodName(PyObject_GetAttr(method, Shiboken::PyMagicName::name()));
    if (!PyObject_HasAttr(self, methodName))
        return true;
    Shiboken::AutoDecRef otherMethod(PyObject_GetAttr(self, methodName));

    // PYSIDE-1523: Each could be a compiled method or a normal method here, for the
    // compiled ones we can use the attributes.
    PyObject *function1;
    if (PyMethod_Check(otherMethod.object())) {
        function1 = PyMethod_GET_FUNCTION(otherMethod.object());
    } else {
        function1 = PyObject_GetAttr(otherMethod.object(), Shiboken::PyName::im_func());
        if (function1 == nullptr)
            return false;
        Py_DECREF(function1);
        // Not retaining a reference in line with what PyMethod_GET_FUNCTION does.
    }

    PyObject *function2;
    if (is_pymethod) {
        function2 = PyMethod_GET_FUNCTION(method);
    } else {
        function2 = PyObject_GetAttr(method, Shiboken::PyName::im_func());
        Py_DECREF(function2);
        // Not retaining a reference in line with what PyMethod_GET_FUNCTION does.
    }

    return function1 != function2;
}

struct GetReceiverResult
{
    QObject *receiver = nullptr;
    PyObject *self = nullptr;
    QByteArray callbackSig;
    bool usingGlobalReceiver = false;
    int slotIndex = -1;
};

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const GetReceiverResult &r)
{
    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "GetReceiverResult(receiver=" << r.receiver << ", self=" << r.self
      << ", sig=" << r.callbackSig << "slotIndex=" << r.slotIndex
      << ", usingGlobalReceiver=" << r.usingGlobalReceiver << ')';
    return d;
}
#endif // QT_NO_DEBUG_STREAM

static GetReceiverResult getReceiver(QObject *source, const char *signal,
                                     PyObject *callback)
{
    GetReceiverResult result;

    bool forceGlobalReceiver = false;
    if (PyMethod_Check(callback)) {
        result.self = PyMethod_GET_SELF(callback);
        result.receiver = PySide::convertToQObject(result.self, false);
        forceGlobalReceiver = isMethodDecorator(callback, true, result.self);
#ifdef PYPY_VERSION
    } else if (Py_TYPE(callback) == PepBuiltinMethod_TypePtr) {
        result.self = PyObject_GetAttrString(callback, "__self__");
        Py_DECREF(result.self);
        result.receiver = PySide::convertToQObject(result.self, false);
#endif
    } else if (PyCFunction_Check(callback)) {
        result.self = PyCFunction_GET_SELF(callback);
        result.receiver = PySide::convertToQObject(result.self, false);
    } else if (PySide::isCompiledMethod(callback)) {
        result.self = PyObject_GetAttr(callback, Shiboken::PyName::im_self());
        Py_DECREF(result.self);
        result.receiver = PySide::convertToQObject(result.self, false);
        forceGlobalReceiver = isMethodDecorator(callback, false, result.self);
    } else if (PyCallable_Check(callback)) {
        // Ok, just a callable object
        result.receiver = nullptr;
        result.self = nullptr;
    }

    result.usingGlobalReceiver = !result.receiver || forceGlobalReceiver;

    // Check if this callback is a overwrite of a non-virtual Qt slot.
    if (!result.usingGlobalReceiver && result.receiver && result.self) {
        result.callbackSig =
            PySide::Signal::getCallbackSignature(signal, result.receiver, callback,
                                                 result.usingGlobalReceiver).toLatin1();
        const QMetaObject *metaObject = result.receiver->metaObject();
        result.slotIndex = metaObject->indexOfSlot(result.callbackSig.constData());
        if (result.slotIndex != -1 && result.slotIndex < metaObject->methodOffset()
            && PyMethod_Check(callback)) {
            result.usingGlobalReceiver = true;
        }
    }

    const auto receiverThread = result.receiver ? result.receiver->thread() : nullptr;

    if (result.usingGlobalReceiver) {
        PySide::SignalManager &signalManager = PySide::SignalManager::instance();
        result.receiver = signalManager.globalReceiver(source, callback, result.receiver);
        // PYSIDE-1354: Move the global receiver to the original receivers's thread
        // so that autoconnections work correctly.
        if (receiverThread && receiverThread != result.receiver->thread())
            result.receiver->moveToThread(receiverThread);
        result.callbackSig =
            PySide::Signal::getCallbackSignature(signal, result.receiver, callback,
                                                 result.usingGlobalReceiver).toLatin1();
        const QMetaObject *metaObject = result.receiver->metaObject();
        result.slotIndex = metaObject->indexOfSlot(result.callbackSig.constData());
    }

    return result;
}

namespace PySide
{
class FriendlyQObject : public QObject // Make protected connectNotify() accessible.
{
public:
    using QObject::connectNotify;
    using QObject::disconnectNotify;
};

QMetaObject::Connection qobjectConnect(QObject *source, const char *signal,
                                       QObject *receiver, const char *slot,
                                       Qt::ConnectionType type)
{
    if (!signal || !slot || !PySide::Signal::checkQtSignal(signal))
        return {};

    if (!PySide::SignalManager::registerMetaMethod(source, signal + 1, QMetaMethod::Signal))
        return {};

    const auto methodType = PySide::Signal::isQtSignal(slot)
                            ? QMetaMethod::Signal : QMetaMethod::Slot;
    PySide::SignalManager::registerMetaMethod(receiver, slot + 1, methodType);
    return QObject::connect(source, signal, receiver, slot, type);
}

QMetaObject::Connection qobjectConnect(QObject *source, QMetaMethod signal,
                                       QObject *receiver, QMetaMethod slot,
                                       Qt::ConnectionType type)
{
    return qobjectConnect(source, signal.methodSignature().constData(),
                          receiver, slot.methodSignature().constData(), type);
}

QMetaObject::Connection qobjectConnectCallback(QObject *source, const char *signal,
                                               PyObject *callback, Qt::ConnectionType type)
{
    if (!signal || !PySide::Signal::checkQtSignal(signal))
        return {};

    const int signalIndex =
        PySide::SignalManager::registerMetaMethodGetIndex(source, signal + 1,
                                                          QMetaMethod::Signal);
    if (signalIndex == -1)
        return {};

    // Extract receiver from callback
    const GetReceiverResult receiver = getReceiver(source, signal + 1, callback);
    if (receiver.receiver == nullptr && receiver.self == nullptr)
        return {};

    int slotIndex = receiver.slotIndex;

    PySide::SignalManager &signalManager = PySide::SignalManager::instance();
    if (slotIndex == -1) {
        if (!receiver.usingGlobalReceiver && receiver.self
            && !Shiboken::Object::hasCppWrapper(reinterpret_cast<SbkObject *>(receiver.self))) {
            qWarning("You can't add dynamic slots on an object originated from C++.");
            if (receiver.usingGlobalReceiver)
                signalManager.releaseGlobalReceiver(source, receiver.receiver);

            return {};
        }

        const char *slotSignature = receiver.callbackSig.constData();
        slotIndex = receiver.usingGlobalReceiver
            ? signalManager.globalReceiverSlotIndex(receiver.receiver, slotSignature)
            : PySide::SignalManager::registerMetaMethodGetIndex(receiver.receiver, slotSignature,
                                                                QMetaMethod::Slot);

        if (slotIndex == -1) {
            if (receiver.usingGlobalReceiver)
                signalManager.releaseGlobalReceiver(source, receiver.receiver);

            return {};
        }
    }

    auto connection = QMetaObject::connect(source, signalIndex, receiver.receiver, slotIndex, type);
    if (!connection) {
        if (receiver.usingGlobalReceiver)
            signalManager.releaseGlobalReceiver(source, receiver.receiver);
        return {};
    }

    Q_ASSERT(receiver.receiver);
    if (receiver.usingGlobalReceiver)
        signalManager.notifyGlobalReceiver(receiver.receiver);

    const QMetaMethod signalMethod = receiver.receiver->metaObject()->method(signalIndex);
    static_cast<FriendlyQObject *>(source)->connectNotify(signalMethod);
    return connection;
}

bool qobjectDisconnectCallback(QObject *source, const char *signal, PyObject *callback)
{
    if (!PySide::Signal::checkQtSignal(signal))
        return false;

    // Extract receiver from callback
    const GetReceiverResult receiver = getReceiver(nullptr, signal, callback);
    if (receiver.receiver == nullptr && receiver.self == nullptr)
        return false;

    const int signalIndex = source->metaObject()->indexOfSignal(signal + 1);
    const int slotIndex = receiver.slotIndex;

    if (!QMetaObject::disconnectOne(source, signalIndex, receiver.receiver, slotIndex))
        return false;

    Q_ASSERT(receiver.receiver);
    const QMetaMethod slotMethod = receiver.receiver->metaObject()->method(slotIndex);
    static_cast<FriendlyQObject *>(source)->disconnectNotify(slotMethod);

    if (receiver.usingGlobalReceiver) { // might delete the receiver
        PySide::SignalManager &signalManager = PySide::SignalManager::instance();
        signalManager.releaseGlobalReceiver(source, receiver.receiver);
    }
    return true;
}

} // namespace PySide

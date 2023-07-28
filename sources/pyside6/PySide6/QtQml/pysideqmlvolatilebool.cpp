// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "pysideqmlvolatilebool.h"

#include <signature.h>

#include <QtCore/QDebug>

// Volatile Bool used for QQmlIncubationController::incubateWhile(std::atomic<bool> *, int)

// Generated headers containing the definition of struct
// QtQml_VolatileBoolObject. It is injected to avoid "pyside6_qtqml_python.h"
// depending on other headers.
#include "pyside6_qtcore_python.h"
#include "pyside6_qtqml_python.h"

// VolatileBool (volatile bool) type definition.

static PyObject *
QtQml_VolatileBoolObject_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static const char *kwlist[] = {"x", 0};
    PyObject *x = Py_False;
    long ok;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:bool", const_cast<char **>(kwlist), &x))
        return nullptr;
    ok = PyObject_IsTrue(x);
    if (ok < 0)
        return nullptr;

    QtQml_VolatileBoolObject *self
            = reinterpret_cast<QtQml_VolatileBoolObject *>(type->tp_alloc(type, 0));

    if (self != nullptr)
        self->flag = new AtomicBool(ok);

    return reinterpret_cast<PyObject *>(self);
}

static void QtQml_VolatileBoolObject_dealloc(PyObject *self)
{
    auto volatileBool = reinterpret_cast<QtQml_VolatileBoolObject *>(self);
    delete volatileBool->flag;
    Sbk_object_dealloc(self);
}

static PyObject *
QtQml_VolatileBoolObject_get(QtQml_VolatileBoolObject *self)
{
    return *self->flag ? Py_True : Py_False;
}

static PyObject *
QtQml_VolatileBoolObject_set(QtQml_VolatileBoolObject *self, PyObject *args)
{
    PyObject *value = Py_False;
    long ok;

    if (!PyArg_ParseTuple(args, "O:bool", &value)) {
        return nullptr;
    }

    ok = PyObject_IsTrue(value);
    if (ok < 0) {
        PyErr_SetString(PyExc_TypeError, "Not a boolean value.");
        return nullptr;
    }

    *self->flag = ok > 0;

    Py_RETURN_NONE;
}

static PyMethodDef QtQml_VolatileBoolObject_methods[] = {
    {"get", reinterpret_cast<PyCFunction>(QtQml_VolatileBoolObject_get), METH_NOARGS,
     "B.get() -> Bool. Returns the value of the volatile boolean"
    },
    {"set", reinterpret_cast<PyCFunction>(QtQml_VolatileBoolObject_set), METH_VARARGS,
     "B.set(a) -> None. Sets the value of the volatile boolean"
    },
    {nullptr, nullptr, 0, nullptr}  /* Sentinel */
};

static PyObject *
QtQml_VolatileBoolObject_repr(QtQml_VolatileBoolObject *self)
{
    PyObject *s;

    if (*self->flag)
        s = PyBytes_FromFormat("%s(True)",
                                Py_TYPE(self)->tp_name);
    else
        s = PyBytes_FromFormat("%s(False)",
                                Py_TYPE(self)->tp_name);
    Py_XINCREF(s);
    return s;
}

static PyObject *
QtQml_VolatileBoolObject_str(QtQml_VolatileBoolObject *self)
{
    PyObject *s;

    if (*self->flag)
        s = PyBytes_FromFormat("%s(True) -> %p",
                                Py_TYPE(self)->tp_name, self->flag);
    else
        s = PyBytes_FromFormat("%s(False) -> %p",
                                Py_TYPE(self)->tp_name, self->flag);
    Py_XINCREF(s);
    return s;
}

static PyType_Slot QtQml_VolatileBoolType_slots[] = {
    {Py_tp_repr, reinterpret_cast<void *>(QtQml_VolatileBoolObject_repr)},
    {Py_tp_str, reinterpret_cast<void *>(QtQml_VolatileBoolObject_str)},
    {Py_tp_methods, reinterpret_cast<void *>(QtQml_VolatileBoolObject_methods)},
    {Py_tp_new, reinterpret_cast<void *>(QtQml_VolatileBoolObject_new)},
    {Py_tp_dealloc, reinterpret_cast<void *>(QtQml_VolatileBoolObject_dealloc)},
    {0, 0}
};
static PyType_Spec QtQml_VolatileBoolType_spec = {
    "2:PySide6.QtQml.VolatileBool",
    sizeof(QtQml_VolatileBoolObject),
    0,
    Py_TPFLAGS_DEFAULT,
    QtQml_VolatileBoolType_slots,
};

PyTypeObject *QtQml_VolatileBool_TypeF(void)
{
    static auto *type = SbkType_FromSpec(&QtQml_VolatileBoolType_spec);
    return type;
}

static const char *VolatileBool_SignatureStrings[] = {
    "PySide6.QtQml.VolatileBool.get(self)->bool",
    "PySide6.QtQml.VolatileBool.set(self,a:object)",
    nullptr}; // Sentinel

void initQtQmlVolatileBool(PyObject *module)
{
    if (InitSignatureStrings(QtQml_VolatileBool_TypeF(), VolatileBool_SignatureStrings) < 0) {
        PyErr_Print();
        qWarning() << "Error initializing VolatileBool type.";
        return;
    }

    Py_INCREF(QtQml_VolatileBool_TypeF());
    PyModule_AddObject(module, PepType_GetNameStr(QtQml_VolatileBool_TypeF()),
                       reinterpret_cast<PyObject *>(QtQml_VolatileBool_TypeF()));
}

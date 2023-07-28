// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "basewrapper.h"
#include "basewrapper_p.h"
#include "bindingmanager.h"
#include "helper.h"
#include "sbkconverter.h"
#include "sbkenum.h"
#include "sbkerrors.h"
#include "sbkfeature_base.h"
#include "sbkstring.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"
#include "autodecref.h"
#include "gilstate.h"
#include <string>
#include <cstring>
#include <cstddef>
#include <set>
#include <sstream>
#include <algorithm>
#include "threadstatesaver.h"
#include "signature.h"
#include "signature_p.h"
#include "voidptr.h"

#include <iostream>

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace {
    void _destroyParentInfo(SbkObject *obj, bool keepReference);
}

static void callDestructor(const Shiboken::DtorAccumulatorVisitor::DestructorEntries &dts)
{
    for (const auto &e : dts) {
        Shiboken::ThreadStateSaver threadSaver;
        threadSaver.save();
        e.destructor(e.cppInstance);
    }
}

extern "C"
{

// PYSIDE-939: A general replacement for object_dealloc.
void Sbk_object_dealloc(PyObject *self)
{
    if (PepRuntime_38_flag) {
        // PYSIDE-939: Handling references correctly.
        // This was not needed before Python 3.8 (Python issue 35810)
        Py_DECREF(Py_TYPE(self));
    }
    Py_TYPE(self)->tp_free(self);
}

static void SbkObjectType_tp_dealloc(PyTypeObject *pyType);
static PyTypeObject *SbkObjectType_tp_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds);

static DestroyQAppHook DestroyQApplication = nullptr;

// PYSIDE-1470: Provide a hook to kill an Application from Shiboken.
void setDestroyQApplication(DestroyQAppHook func)
{
    DestroyQApplication = func;
}

// PYSIDE-535: Use the C API in PyPy instead of `op->ob_dict`, directly
LIBSHIBOKEN_API PyObject *SbkObject_GetDict_NoRef(PyObject *op)
{
    assert(Shiboken::Object::checkType(op));
#ifdef PYPY_VERSION
    Shiboken::GilState state;
    auto *ret = PyObject_GenericGetDict(op, nullptr);
    Py_DECREF(ret);
    return ret;
#else
    auto *sbkObj = reinterpret_cast<SbkObject *>(op);
    if (!sbkObj->ob_dict) {
        Shiboken::GilState state;
        sbkObj->ob_dict = PyDict_New();
    }
    return sbkObj->ob_dict;
#endif
}

static int
check_set_special_type_attr(PyTypeObject *type, PyObject *value, const char *name)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "can't set %s.%s", type->tp_name, name);
        return 0;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "can't delete %s.%s", type->tp_name, name);
        return 0;
    }
    return 1;
}

// PYSIDE-1177: Add a setter to allow setting type doc.
static int
type_set_doc(PyTypeObject *type, PyObject *value, void * /* context */)
{
    if (!check_set_special_type_attr(type, value, "__doc__"))
        return -1;
    PyType_Modified(type);
    return PyDict_SetItem(type->tp_dict, Shiboken::PyMagicName::doc(), value);
}

// PYSIDE-908: The function PyType_Modified does not work in PySide, so we need to
// explicitly pass __doc__.
static PyGetSetDef SbkObjectType_tp_getset[] = {
    {const_cast<char *>("__doc__"),       reinterpret_cast<getter>(Sbk_TypeGet___doc__),
                                          reinterpret_cast<setter>(type_set_doc), nullptr, nullptr},
    {const_cast<char *>("__dict__"),      reinterpret_cast<getter>(Sbk_TypeGet___dict__),
                                          nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}  // Sentinel
};

static PyType_Slot SbkObjectType_Type_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(SbkObjectType_tp_dealloc)},
    {Py_tp_getattro, reinterpret_cast<void *>(mangled_type_getattro)},
    {Py_tp_base, static_cast<void *>(&PyType_Type)},
    {Py_tp_alloc, reinterpret_cast<void *>(PyType_GenericAlloc)},
    {Py_tp_new, reinterpret_cast<void *>(SbkObjectType_tp_new)},
    {Py_tp_free, reinterpret_cast<void *>(PyObject_GC_Del)},
    {Py_tp_getset, reinterpret_cast<void *>(SbkObjectType_tp_getset)},
    {0, nullptr}
};

// PYSIDE-535: The tp_itemsize field is inherited and does not need to be set.
// In PyPy, it _must_ not be set, because it would have the meaning that a
// `__len__` field must be defined. Not doing so creates a hard-to-find crash.
static PyType_Spec SbkObjectType_Type_spec = {
    "1:Shiboken.ObjectType",
    0,
    0, // sizeof(PyMemberDef), not for PyPy without a __len__ defined
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    SbkObjectType_Type_slots,
};

PyTypeObject *SbkObjectType_TypeF(void)
{
    static auto *type = SbkType_FromSpec(&SbkObjectType_Type_spec);
    return type;
}

static PyObject *SbkObjectGetDict(PyObject *pObj, void *)
{
    auto ret = SbkObject_GetDict_NoRef(pObj);
    Py_XINCREF(ret);
    return ret;
}

static PyGetSetDef SbkObject_tp_getset[] = {
    {const_cast<char *>("__dict__"), SbkObjectGetDict, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr} // Sentinel
};

static int SbkObject_tp_traverse(PyObject *self, visitproc visit, void *arg)
{
    auto *sbkSelf = reinterpret_cast<SbkObject *>(self);

    //Visit children
    Shiboken::ParentInfo *pInfo = sbkSelf->d->parentInfo;
    if (pInfo) {
        for (SbkObject *c : pInfo->children)
             Py_VISIT(c);
    }

    //Visit refs
    Shiboken::RefCountMap *rInfo = sbkSelf->d->referredObjects;
    if (rInfo) {
        for (auto it = rInfo->begin(), end = rInfo->end(); it != end; ++it)
            Py_VISIT(it->second);
    }

    if (sbkSelf->ob_dict)
        Py_VISIT(sbkSelf->ob_dict);

#if PY_VERSION_HEX >= 0x03090000
    // This was not needed before Python 3.9 (Python issue 35810 and 40217)
    Py_VISIT(Py_TYPE(self));
#endif
    return 0;
}

static int SbkObject_tp_clear(PyObject *self)
{
    auto *sbkSelf = reinterpret_cast<SbkObject *>(self);

    Shiboken::Object::removeParent(sbkSelf);

    if (sbkSelf->d->parentInfo)
        _destroyParentInfo(sbkSelf, true);

    Shiboken::Object::clearReferences(sbkSelf);

    if (sbkSelf->ob_dict)
        Py_CLEAR(sbkSelf->ob_dict);
    return 0;
}

static PyType_Slot SbkObject_Type_slots[] = {
    {Py_tp_getattro, reinterpret_cast<void *>(SbkObject_GenericGetAttr)},
    {Py_tp_setattro, reinterpret_cast<void *>(SbkObject_GenericSetAttr)},
    {Py_tp_dealloc, reinterpret_cast<void *>(SbkDeallocWrapperWithPrivateDtor)},
    {Py_tp_traverse, reinterpret_cast<void *>(SbkObject_tp_traverse)},
    {Py_tp_clear, reinterpret_cast<void *>(SbkObject_tp_clear)},
    // unsupported: {Py_tp_weaklistoffset, (void *)offsetof(SbkObject, weakreflist)},
    {Py_tp_getset, reinterpret_cast<void *>(SbkObject_tp_getset)},
    // unsupported: {Py_tp_dictoffset, (void *)offsetof(SbkObject, ob_dict)},
    {0, nullptr}
};
static PyType_Spec SbkObject_Type_spec = {
    "1:Shiboken.Object",
    sizeof(SbkObject),
    0,
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    SbkObject_Type_slots,
};

static const char *SbkObject_SignatureStrings[] = {
    "Shiboken.Object(self)",
    nullptr}; // Sentinel

PyTypeObject *SbkObject_TypeF(void)
{
    static auto *type = SbkType_FromSpec_BMDWB(&SbkObject_Type_spec,
                                               nullptr,     // bases
                                               SbkObjectType_TypeF(),
                                               offsetof(SbkObject, ob_dict),
                                               offsetof(SbkObject, weakreflist),
                                               nullptr);    // bufferprocs
    return type;
}

static int mainThreadDeletionHandler(void *)
{
    if (Py_IsInitialized())
        Shiboken::BindingManager::instance().runDeletionInMainThread();
    return 0;
}

static void SbkDeallocWrapperCommon(PyObject *pyObj, bool canDelete)
{
    auto *sbkObj = reinterpret_cast<SbkObject *>(pyObj);
    PyTypeObject *pyType = Py_TYPE(pyObj);

    // Need to decref the type if this is the dealloc func; if type
    // is subclassed, that dealloc func will decref (see subtype_dealloc
    // in typeobject.c in the python sources)
    auto dealloc = PyType_GetSlot(pyType, Py_tp_dealloc);
    bool needTypeDecref = dealloc == SbkDeallocWrapper
        || dealloc == SbkDeallocWrapperWithPrivateDtor;
    if (PepRuntime_38_flag) {
        // PYSIDE-939: Additional rule: Also when a subtype is heap allocated,
        // then the subtype_dealloc deref will be suppressed, and we need again
        // to supply a decref.
        needTypeDecref |= (pyType->tp_base->tp_flags & Py_TPFLAGS_HEAPTYPE) != 0;
    }

#if defined(__APPLE__)
    // Just checking once that our assumptions are right.
    if (false) {
        void *p = PyType_GetSlot(pyType, Py_tp_dealloc);
        Dl_info dl_info;
        dladdr(p, &dl_info);
        fprintf(stderr, "tp_dealloc is %s\n", dl_info.dli_sname);
    }
    // Gives one of our functions
    //  "Sbk_object_dealloc"
    //  "SbkDeallocWrapperWithPrivateDtor"
    //  "SbkDeallocQAppWrapper"
    //  "SbkDeallocWrapper"
    // but for typedealloc_test.py we get
    //  "subtype_dealloc"
#endif

    // Ensure that the GC is no longer tracking this object to avoid a
    // possible reentrancy problem.  Since there are multiple steps involved
    // in deallocating a SbkObject it is possible for the garbage collector to
    // be invoked and it trying to delete this object while it is still in
    // progress from the first time around, resulting in a double delete and a
    // crash.
    PyObject_GC_UnTrack(pyObj);

    // Check that Python is still initialized as sometimes this is called by a static destructor
    // after Python interpeter is shutdown.
    if (sbkObj->weakreflist && Py_IsInitialized())
        PyObject_ClearWeakRefs(pyObj);

    // If I have ownership and is valid delete C++ pointer
    auto *sotp = PepType_SOTP(pyType);
    canDelete &= sbkObj->d->hasOwnership && sbkObj->d->validCppObject;
    if (canDelete) {
        if (sotp->delete_in_main_thread && Shiboken::currentThreadId() != Shiboken::mainThreadId()) {
            auto &bindingManager = Shiboken::BindingManager::instance();
            if (sotp->is_multicpp) {
                 Shiboken::DtorAccumulatorVisitor visitor(sbkObj);
                 Shiboken::walkThroughClassHierarchy(Py_TYPE(pyObj), &visitor);
                 for (const auto &e : visitor.entries())
                     bindingManager.addToDeletionInMainThread(e);
            } else {
                Shiboken::DestructorEntry e{sotp->cpp_dtor, sbkObj->d->cptr[0]};
                bindingManager.addToDeletionInMainThread(e);
            }
            Py_AddPendingCall(mainThreadDeletionHandler, nullptr);
            canDelete = false;
        }
    }

    PyObject *error_type, *error_value, *error_traceback;

    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    if (canDelete) {
        if (sotp->is_multicpp) {
            Shiboken::DtorAccumulatorVisitor visitor(sbkObj);
            Shiboken::walkThroughClassHierarchy(Py_TYPE(pyObj), &visitor);
            Shiboken::Object::deallocData(sbkObj, true);
            callDestructor(visitor.entries());
        } else {
            void *cptr = sbkObj->d->cptr[0];
            Shiboken::Object::deallocData(sbkObj, true);

            Shiboken::ThreadStateSaver threadSaver;
            if (Py_IsInitialized())
                threadSaver.save();
            sotp->cpp_dtor(cptr);
        }
    } else {
        Shiboken::Object::deallocData(sbkObj, true);
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);

    if (needTypeDecref)
        Py_DECREF(pyType);
    if (PepRuntime_38_flag) {
        // PYSIDE-939: Handling references correctly.
        // This was not needed before Python 3.8 (Python issue 35810)
        Py_DECREF(pyType);
    }
}

static inline PyObject *_Sbk_NewVarObject(PyTypeObject *type)
{
    // PYSIDE-1970: Support __slots__, implemented by PyVarObject
    auto const baseSize = sizeof(SbkObject);
    auto varCount = Py_SIZE(type);
    auto *self = PyObject_GC_NewVar(PyObject, type, varCount);
    if (varCount)
        std::memset(reinterpret_cast<char *>(self) + baseSize, 0, varCount * sizeof(void *));
    return self;
}

void SbkDeallocWrapper(PyObject *pyObj)
{
    SbkDeallocWrapperCommon(pyObj, true);
}

void SbkDeallocQAppWrapper(PyObject *pyObj)
{
    SbkDeallocWrapper(pyObj);
    // PYSIDE-571: make sure to create a singleton deleted qApp.
    Py_DECREF(MakeQAppWrapper(nullptr));
}

void SbkDeallocWrapperWithPrivateDtor(PyObject *self)
{
    SbkDeallocWrapperCommon(self, false);
}

void SbkObjectType_tp_dealloc(PyTypeObject *sbkType)
{
    SbkObjectTypePrivate *sotp = PepType_SOTP(sbkType);
    auto pyObj = reinterpret_cast<PyObject *>(sbkType);

    PyObject_GC_UnTrack(pyObj);
#ifndef Py_LIMITED_API
#  if PY_VERSION_HEX >= 0x030A0000
    Py_TRASHCAN_BEGIN(pyObj, 1);
#  else
    Py_TRASHCAN_SAFE_BEGIN(pyObj);
#  endif
#endif
    if (sotp) {
        if (sotp->user_data && sotp->d_func) {
            sotp->d_func(sotp->user_data);
            sotp->user_data = nullptr;
        }
        free(sotp->original_name);
        sotp->original_name = nullptr;
        if (!Shiboken::ObjectType::isUserType(sbkType))
            Shiboken::Conversions::deleteConverter(sotp->converter);
        PepType_SOTP_delete(sbkType);
    }
#ifndef Py_LIMITED_API
#  if PY_VERSION_HEX >= 0x030A0000
    Py_TRASHCAN_END;
#  else
    Py_TRASHCAN_SAFE_END(pyObj);
#  endif
#endif
    if (PepRuntime_38_flag) {
        // PYSIDE-939: Handling references correctly.
        // This was not needed before Python 3.8 (Python issue 35810)
        Py_DECREF(Py_TYPE(pyObj));
    }
}

////////////////////////////////////////////////////////////////////////////
//
// Support for the qApp macro.
//
// qApp is a macro in Qt5. In Python, we simulate that a little by a
// variable that monitors Q*Application.instance().
// This variable is also able to destroy the app by qApp.shutdown().
//

PyObject *MakeQAppWrapper(PyTypeObject *type)
{
    static PyObject *qApp_last = nullptr;

    // protecting from multiple application instances
    if (!(type == nullptr || qApp_last == Py_None)) {
        const char *res_name = qApp_last != nullptr
            ? PepType_GetNameStr(Py_TYPE(qApp_last)) : "<Unknown>";
        const char *type_name = PepType_GetNameStr(type);
        PyErr_Format(PyExc_RuntimeError, "Please destroy the %s singleton before"
            " creating a new %s instance.", res_name, type_name);
        return nullptr;
    }

    // monitoring the last application state
    PyObject *qApp_curr = type != nullptr ? _Sbk_NewVarObject(type) : Py_None;
    static PyObject *builtins = PyEval_GetBuiltins();
    if (PyDict_SetItem(builtins, Shiboken::PyName::qApp(), qApp_curr) < 0)
        return nullptr;
    qApp_last = qApp_curr;
    // Note: This Py_INCREF would normally be wrong because the qApp
    // object already has a reference from PyObject_GC_New. But this is
    // exactly the needed reference that keeps qApp alive from alone!
    Py_INCREF(qApp_curr);
    // PYSIDE-1470: As a side effect, the interactive "_" variable tends to
    //              create reference cycles. This is disturbing when trying
    //              to remove qApp with del.
    // PYSIDE-1758: Since we moved to an explicit qApp.shutdown() call, we
    //              no longer initialize "_" with Py_None.
    return qApp_curr;
}

static PyTypeObject *SbkObjectType_tp_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    // Check if all bases are new style before calling type.tp_new
    // Was causing gc assert errors in test_bug704.py when
    // this check happened after creating the type object.
    // Argument parsing take from type.tp_new code.

    // PYSIDE-595: Also check if all bases allow inheritance.
    // Before we changed to heap types, it was sufficient to remove the
    // Py_TPFLAGS_BASETYPE flag. That does not work, because PySide does
    // not respect this flag itself!
    PyObject *name;
    PyObject *pyBases;
    PyObject *dict;
    static const char *kwlist[] = { "name", "bases", "dict", nullptr};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO!O!:sbktype", const_cast<char **>(kwlist),
                                     &name,
                                     &PyTuple_Type, &pyBases,
                                     &PyDict_Type, &dict))
        return nullptr;

    for (int i=0, i_max=PyTuple_GET_SIZE(pyBases); i < i_max; i++) {
        PyObject *baseType = PyTuple_GET_ITEM(pyBases, i);
        if (reinterpret_cast<PyTypeObject *>(baseType)->tp_new == SbkDummyNew) {
            // PYSIDE-595: A base class does not allow inheritance.
            return reinterpret_cast<PyTypeObject *>(SbkDummyNew(metatype, args, kwds));
        }
    }

    // PYSIDE-939: This is still a temporary patch that circumvents the problem
    //             with Py_TPFLAGS_METHOD_DESCRIPTOR. The problem exists in Python 3.8
    //             until 3.9.12, only. We check the runtime and hope for this version valishing.
    //             https://github.com/python/cpython/issues/92112 will not be fixed for 3.8 :/
    PyTypeObject *newType{};
    static auto triplet = _PepRuntimeVersion();
    if (triplet >= (3 << 16 | 8 << 8 | 0) && triplet < (3 << 16 | 9 << 8 | 13)) {
        auto hold = PyMethodDescr_Type.tp_flags;
        PyMethodDescr_Type.tp_flags &= ~Py_TPFLAGS_METHOD_DESCRIPTOR;
        newType = PepType_Type_tp_new(metatype, args, kwds);
        PyMethodDescr_Type.tp_flags = hold;
    } else {
        newType = PepType_Type_tp_new(metatype, args, kwds);
    }

    if (!newType)
        return nullptr;

    SbkObjectTypePrivate *sotp = PepType_SOTP(newType);

    const auto bases = Shiboken::getCppBaseClasses(newType);
    if (bases.size() == 1) {
        SbkObjectTypePrivate *parentType = PepType_SOTP(bases.front());
        sotp->mi_offsets = parentType->mi_offsets;
        sotp->mi_init = parentType->mi_init;
        sotp->mi_specialcast = parentType->mi_specialcast;
        sotp->type_discovery = parentType->type_discovery;
        sotp->cpp_dtor = parentType->cpp_dtor;
        sotp->is_multicpp = 0;
        sotp->converter = parentType->converter;
    } else {
        sotp->mi_offsets = nullptr;
        sotp->mi_init = nullptr;
        sotp->mi_specialcast = nullptr;
        sotp->type_discovery = nullptr;
        sotp->cpp_dtor = nullptr;
        sotp->is_multicpp = 1;
        sotp->converter = nullptr;
    }
    if (bases.size() == 1) {
        const char *original_name = PepType_SOTP(bases.front())->original_name;
        if (original_name == nullptr)
            original_name = "object";
        sotp->original_name = strdup(original_name);
    }
    else
        sotp->original_name = strdup("object");
    sotp->user_data = nullptr;
    sotp->d_func = nullptr;
    sotp->is_user_type = 1;

    // PYSIDE-1463: Prevent feature switching while in the creation process
    auto saveFeature = initSelectableFeature(nullptr);
    for (PyTypeObject *base : bases) {
        sotp = PepType_SOTP(base);
        if (sotp->subtype_init)
            sotp->subtype_init(newType, args, kwds);
    }
    initSelectableFeature(saveFeature);
    return newType;
}

static PyObject *_setupNew(PyObject *obSelf, PyTypeObject *subtype)
{
    auto *obSubtype = reinterpret_cast<PyObject *>(subtype);
    auto *sbkSubtype = subtype;
    auto *self = reinterpret_cast<SbkObject *>(obSelf);

    Py_INCREF(obSubtype);
    auto d = new SbkObjectPrivate;

    auto *sotp = PepType_SOTP(sbkSubtype);
    int numBases = ((sotp && sotp->is_multicpp) ?
        Shiboken::getNumberOfCppBaseClasses(subtype) : 1);
    d->cptr = new void *[numBases];
    std::memset(d->cptr, 0, sizeof(void *) *size_t(numBases));
    d->hasOwnership = 1;
    d->containsCppWrapper = 0;
    d->validCppObject = 0;
    d->parentInfo = nullptr;
    d->referredObjects = nullptr;
    d->cppObjectCreated = 0;
    d->isQAppSingleton = 0;
    self->ob_dict = nullptr;
    self->weakreflist = nullptr;
    self->d = d;
    PyObject_GC_Track(obSelf);
    return obSelf;
}

PyObject *SbkObject_tp_new(PyTypeObject *subtype, PyObject * /* args */, PyObject * /* kwds */)
{
    PyObject *self = _Sbk_NewVarObject(subtype);
    return _setupNew(self, subtype);
}

PyObject *SbkQApp_tp_new(PyTypeObject *subtype, PyObject *, PyObject *)
{
    auto *obSelf = MakeQAppWrapper(subtype);
    auto *self = reinterpret_cast<SbkObject *>(obSelf);
    if (self == nullptr)
        return nullptr;
    auto ret = _setupNew(obSelf, subtype);
    auto priv = self->d;
    priv->isQAppSingleton = 1;
    return ret;
}

PyObject *SbkDummyNew(PyTypeObject *type, PyObject *, PyObject *)
{
    // PYSIDE-595: Give the same error as type_call does when tp_new is NULL.
    PyErr_Format(PyExc_TypeError,
                 "cannot create '%.100s' instances ¯\\_(ツ)_/¯",
                 type->tp_name);
    return nullptr;
}

// PYSIDE-74: Fallback used in all types now.
PyObject *FallbackRichCompare(PyObject *self, PyObject *other, int op)
{
    // This is a very simple implementation that supplies a simple identity.
    static const char * const opstrings[] = {"<", "<=", "==", "!=", ">", ">="};
    PyObject *res;

    switch (op) {

    case Py_EQ:
        res = (self == other) ? Py_True : Py_False;
        break;
    case Py_NE:
        res = (self != other) ? Py_True : Py_False;
        break;
    default:
        PyErr_Format(PyExc_TypeError,
                     "'%s' not supported between instances of '%.100s' and '%.100s'",
                     opstrings[op],
                     self->ob_type->tp_name,
                     other->ob_type->tp_name);
        return nullptr;
    }
    Py_INCREF(res);
    return res;
}

} //extern "C"


namespace
{

void _destroyParentInfo(SbkObject *obj, bool keepReference)
{
    Shiboken::ParentInfo *pInfo = obj->d->parentInfo;
    if (pInfo) {
        while(!pInfo->children.empty()) {
            SbkObject *first = *pInfo->children.begin();
            // Mark child as invalid
            Shiboken::Object::invalidate(first);
            Shiboken::Object::removeParent(first, false, keepReference);
        }
        Shiboken::Object::removeParent(obj, false);
    }
}

}

namespace Shiboken
{
bool walkThroughClassHierarchy(PyTypeObject *currentType, HierarchyVisitor *visitor)
{
    PyObject *bases = currentType->tp_bases;
    Py_ssize_t numBases = PyTuple_GET_SIZE(bases);
    bool result = false;
    for (int i = 0; !result && i < numBases; ++i) {
        auto type = reinterpret_cast<PyTypeObject *>(PyTuple_GET_ITEM(bases, i));
        if (PyType_IsSubtype(type, reinterpret_cast<PyTypeObject *>(SbkObject_TypeF()))) {
            result = PepType_SOTP(type)->is_user_type
                     ? walkThroughClassHierarchy(type, visitor) : visitor->visit(type);
        }
    }
    return result;
}

// Wrapper metatype and base type ----------------------------------------------------------

HierarchyVisitor::HierarchyVisitor() = default;
HierarchyVisitor::~HierarchyVisitor() = default;

bool BaseCountVisitor::visit(PyTypeObject *)
{
    m_count++;
    return false;
}

bool BaseAccumulatorVisitor::visit(PyTypeObject *node)
{
    m_bases.push_back(node);
    return false;
}

bool GetIndexVisitor::visit(PyTypeObject *node)
{
    m_index++;
    return PyType_IsSubtype(node, m_desiredType);
}

bool DtorAccumulatorVisitor::visit(PyTypeObject *node)
{
    auto *sotp = PepType_SOTP(node);
    m_entries.push_back(DestructorEntry{sotp->cpp_dtor,
                                        m_pyObject->d->cptr[m_entries.size()]});
    return false;
}

void _initMainThreadId(); // helper.cpp

namespace Conversions { void init(); }

void init()
{
    static bool shibokenAlreadInitialised = false;
    if (shibokenAlreadInitialised)
        return;

    _initMainThreadId();

    Conversions::init();

    //Init private data
    Pep384_Init();

    if (PyType_Ready(SbkEnumType_TypeF()) < 0)
        Py_FatalError("[libshiboken] Failed to initialize Shiboken.SbkEnumType metatype.");

    if (PyType_Ready(SbkObjectType_TypeF()) < 0)
        Py_FatalError("[libshiboken] Failed to initialize Shiboken.BaseWrapperType metatype.");

    if (PyType_Ready(SbkObject_TypeF()) < 0)
        Py_FatalError("[libshiboken] Failed to initialize Shiboken.BaseWrapper type.");

    VoidPtr::init();

    shibokenAlreadInitialised = true;
}

// PYSIDE-1415: Publish Shiboken objects.
// PYSIDE-1735: Initialize the whole Shiboken startup.
void initShibokenSupport(PyObject *module)
{
    Py_INCREF(SbkObject_TypeF());
    PyModule_AddObject(module, "Object", reinterpret_cast<PyObject *>(SbkObject_TypeF()));

    // PYSIDE-1735: When the initialization was moved into Shiboken import, this
    //              Py_INCREF became necessary. No idea why.
    Py_INCREF(module);
    init_shibokensupport_module();

    auto *type = SbkObject_TypeF();
    if (InitSignatureStrings(type, SbkObject_SignatureStrings) < 0)
        Py_FatalError("Error in initShibokenSupport");
}

// setErrorAboutWrongArguments now gets overload info from the signature module.
// Info can be nullptr and contains extra info.
void setErrorAboutWrongArguments(PyObject *args, const char *funcName, PyObject *info)
{
    SetError_Argument(args, funcName, info);
}

PyObject *returnWrongArguments(PyObject *args, const char *funcName, PyObject *info)
{
    setErrorAboutWrongArguments(args, funcName, info);
    return {};
}

int returnWrongArguments_Zero(PyObject *args, const char *funcName, PyObject *info)
{
    setErrorAboutWrongArguments(args, funcName, info);
    return 0;
}

int returnWrongArguments_MinusOne(PyObject *args, const char *funcName, PyObject *info)
{
    setErrorAboutWrongArguments(args, funcName, info);
    return -1;
}

PyObject *returnFromRichCompare(PyObject *result)
{
    if (result && !PyErr_Occurred())
        return result;
    Shiboken::Errors::setOperatorNotImplemented();
    return {};
}

PyObject *checkInvalidArgumentCount(Py_ssize_t numArgs, Py_ssize_t minArgs, Py_ssize_t maxArgs)
{
    PyObject *result = nullptr;
    // for seterror_argument(), signature/errorhandler.py
    if (numArgs > maxArgs) {
        static PyObject *const tooMany = Shiboken::String::createStaticString(">");
        result = tooMany;
        Py_INCREF(result);
    } else if (numArgs < minArgs) {
        static PyObject *const tooFew = Shiboken::String::createStaticString("<");
        static PyObject *const noArgs = Shiboken::String::createStaticString("0");
        result = numArgs > 0 ? tooFew : noArgs;
        Py_INCREF(result);
    }
    return result;
}

class FindBaseTypeVisitor : public HierarchyVisitor
{
public:
    explicit FindBaseTypeVisitor(PyTypeObject *typeToFind) : m_typeToFind(typeToFind) {}

    bool visit(PyTypeObject *node) override
    {
        return node == m_typeToFind;
    }

private:
    PyTypeObject *m_typeToFind;
};

std::vector<SbkObject *> splitPyObject(PyObject *pyObj)
{
    std::vector<SbkObject *> result;
    if (PySequence_Check(pyObj)) {
        AutoDecRef lst(PySequence_Fast(pyObj, "Invalid keep reference object."));
        if (!lst.isNull()) {
            for (Py_ssize_t i = 0, i_max = PySequence_Fast_GET_SIZE(lst.object()); i < i_max; ++i) {
                PyObject *item = PySequence_Fast_GET_ITEM(lst.object(), i);
                if (Object::checkType(item))
                    result.push_back(reinterpret_cast<SbkObject *>(item));
            }
        }
    } else {
        result.push_back(reinterpret_cast<SbkObject *>(pyObj));
    }
    return result;
}

template <class Iterator>
inline void decRefPyObjectList(Iterator i1, Iterator i2)
{
    for (; i1 != i2; ++i1)
        Py_DECREF(i1->second);
}

namespace ObjectType
{

bool checkType(PyTypeObject *type)
{
    return PyType_IsSubtype(type, SbkObject_TypeF()) != 0;
}

bool isUserType(PyTypeObject *type)
{
    return checkType(type) && PepType_SOTP(type)->is_user_type;
}

bool canCallConstructor(PyTypeObject *myType, PyTypeObject *ctorType)
{
    FindBaseTypeVisitor visitor(ctorType);
    if (!walkThroughClassHierarchy(myType, &visitor)) {
        PyErr_Format(PyExc_TypeError, "%s isn't a direct base class of %s", ctorType->tp_name, myType->tp_name);
        return false;
    }
    return true;
}

bool hasCast(PyTypeObject *type)
{
    return PepType_SOTP(type)->mi_specialcast != nullptr;
}

void *cast(PyTypeObject *sourceType, SbkObject *obj, PyTypeObject *pyTargetType)
{
    auto *sotp = PepType_SOTP(sourceType);
    return sotp->mi_specialcast(Object::cppPointer(obj, pyTargetType), pyTargetType);
}

void setCastFunction(PyTypeObject *type, SpecialCastFunction func)
{
    auto *sotp = PepType_SOTP(type);
    sotp->mi_specialcast = func;
}

void setOriginalName(PyTypeObject *type, const char *name)
{
    auto *sotp = PepType_SOTP(type);
    if (sotp->original_name)
        free(sotp->original_name);
    sotp->original_name = strdup(name);
}

const char *getOriginalName(PyTypeObject *type)
{
    return PepType_SOTP(type)->original_name;
}

void setTypeDiscoveryFunctionV2(PyTypeObject *type, TypeDiscoveryFuncV2 func)
{
    PepType_SOTP(type)->type_discovery = func;
}

void copyMultipleInheritance(PyTypeObject *type, PyTypeObject *other)
{
    auto *sotp_type = PepType_SOTP(type);
    auto *sotp_other = PepType_SOTP(other);
    sotp_type->mi_init = sotp_other->mi_init;
    sotp_type->mi_offsets = sotp_other->mi_offsets;
    sotp_type->mi_specialcast = sotp_other->mi_specialcast;
}

void setMultipleInheritanceFunction(PyTypeObject *type, MultipleInheritanceInitFunction function)
{
    PepType_SOTP(type)->mi_init = function;
}

MultipleInheritanceInitFunction getMultipleInheritanceFunction(PyTypeObject *type)
{
    return PepType_SOTP(type)->mi_init;
}

void setDestructorFunction(PyTypeObject *type, ObjectDestructor func)
{
    PepType_SOTP(type)->cpp_dtor = func;
}

PyTypeObject *
introduceWrapperType(PyObject *enclosingObject,
                     const char *typeName,
                     const char *originalName,
                     PyType_Spec *typeSpec,
                     ObjectDestructor cppObjDtor,
                     PyTypeObject *baseType,
                     PyObject *baseTypes,
                     unsigned wrapperFlags)
{
    auto *base = baseType ? baseType : SbkObject_TypeF();
    typeSpec->slots[0].pfunc = reinterpret_cast<void *>(base);
    auto *bases = baseTypes ? baseTypes : PyTuple_Pack(1, base);

    auto *type = SbkType_FromSpecBasesMeta(typeSpec, bases, SbkObjectType_TypeF());

    for (int i = 0; i < PySequence_Fast_GET_SIZE(bases); ++i) {
        auto *st = reinterpret_cast<PyTypeObject *>(PySequence_Fast_GET_ITEM(bases, i));
        BindingManager::instance().addClassInheritance(st, type);
    }

    auto sotp = PepType_SOTP(type);
    if (wrapperFlags & DeleteInMainThread)
        sotp->delete_in_main_thread = 1;

    setOriginalName(type, originalName);
    setDestructorFunction(type, cppObjDtor);
    auto *ob_type = reinterpret_cast<PyObject *>(type);

    if (wrapperFlags & InnerClass)
        return PyDict_SetItemString(enclosingObject, typeName, ob_type) == 0 ? type : nullptr;

    // PyModule_AddObject steals type's reference.
    Py_INCREF(ob_type);
    if (PyModule_AddObject(enclosingObject, typeName, ob_type) != 0) {
        std::cerr << "Warning: " << __FUNCTION__ << " returns nullptr for "
            << typeName << '/' << originalName << " due to PyModule_AddObject(enclosingObject="
            << enclosingObject << ", ob_type=" << ob_type << ") failing\n";
        return nullptr;
    }
    return type;
}

void setSubTypeInitHook(PyTypeObject *type, SubTypeInitHook func)
{
    PepType_SOTP(type)->subtype_init = func;
}

void *getTypeUserData(PyTypeObject *type)
{
    return PepType_SOTP(type)->user_data;
}

void setTypeUserData(PyTypeObject *type, void *userData, DeleteUserDataFunc d_func)
{
    auto *sotp = PepType_SOTP(type);
    sotp->user_data = userData;
    sotp->d_func = d_func;
}

// Try to find the exact type of cptr.
PyTypeObject *typeForTypeName(const char *typeName)
{
    PyTypeObject *result{};
    if (typeName) {
        if (PyTypeObject *pyType = Shiboken::Conversions::getPythonTypeObject(typeName))
            result = pyType;
    }
    return result;
}

bool hasSpecialCastFunction(PyTypeObject *sbkType)
{
    const auto *d = PepType_SOTP(sbkType);
    return d != nullptr && d->mi_specialcast != nullptr;
}

} // namespace ObjectType


namespace Object
{

static void recursive_invalidate(SbkObject *self, std::set<SbkObject *>& seen);

bool checkType(PyObject *pyObj)
{
    return ObjectType::checkType(Py_TYPE(pyObj));
}

bool isUserType(PyObject *pyObj)
{
    return ObjectType::isUserType(Py_TYPE(pyObj));
}

Py_hash_t hash(PyObject *pyObj)
{
    assert(Shiboken::Object::checkType(pyObj));
    return reinterpret_cast<Py_hash_t>(pyObj);
}

static void setSequenceOwnership(PyObject *pyObj, bool owner)
{

    bool has_length = true;

    if (!pyObj)
        return;

    if (PySequence_Size(pyObj) < 0) {
        PyErr_Clear();
        has_length = false;
    }

    if (PySequence_Check(pyObj) && has_length) {
        Py_ssize_t size = PySequence_Size(pyObj);
        if (size > 0) {
            const auto objs = splitPyObject(pyObj);
            if (owner) {
                for (SbkObject *o : objs)
                    getOwnership(o);
            } else {
                for (SbkObject *o : objs)
                    releaseOwnership(o);
            }
        }
    } else if (Object::checkType(pyObj)) {
        if (owner)
            getOwnership(reinterpret_cast<SbkObject *>(pyObj));
        else
            releaseOwnership(reinterpret_cast<SbkObject *>(pyObj));
    }
}

void setValidCpp(SbkObject *pyObj, bool value)
{
    pyObj->d->validCppObject = value;
}

void setHasCppWrapper(SbkObject *pyObj, bool value)
{
    pyObj->d->containsCppWrapper = value;
}

bool hasCppWrapper(SbkObject *pyObj)
{
    return pyObj->d->containsCppWrapper;
}

bool wasCreatedByPython(SbkObject *pyObj)
{
    return pyObj->d->cppObjectCreated;
}

void callCppDestructors(SbkObject *pyObj)
{
    auto priv = pyObj->d;
    if (priv->isQAppSingleton && DestroyQApplication) {
        // PYSIDE-1470: Allow to destroy the application from Shiboken.
        DestroyQApplication();
        return;
    }
    PyTypeObject *type = Py_TYPE(pyObj);
    auto *sotp = PepType_SOTP(type);
    if (sotp->is_multicpp) {
        Shiboken::DtorAccumulatorVisitor visitor(pyObj);
        Shiboken::walkThroughClassHierarchy(type, &visitor);
        callDestructor(visitor.entries());
    } else {
        Shiboken::ThreadStateSaver threadSaver;
        threadSaver.save();
        sotp->cpp_dtor(pyObj->d->cptr[0]);
    }

    if (priv->validCppObject && priv->containsCppWrapper) {
        BindingManager::instance().releaseWrapper(pyObj);
    }

    /* invalidate needs to be called before deleting pointer array because
       it needs to delete entries for them from the BindingManager hash table;
       also release wrapper explicitly if object contains C++ wrapper because
       invalidate doesn't */
    invalidate(pyObj);

    delete[] priv->cptr;
    priv->cptr = nullptr;
    priv->validCppObject = false;
}

bool hasOwnership(SbkObject *pyObj)
{
    return pyObj->d->hasOwnership;
}

void getOwnership(SbkObject *self)
{
    // skip if already have the ownership
    if (self->d->hasOwnership)
        return;

    // skip if this object has parent
    if (self->d->parentInfo && self->d->parentInfo->parent)
        return;

    // Get back the ownership
    self->d->hasOwnership = true;

    if (self->d->containsCppWrapper)
        Py_DECREF(reinterpret_cast<PyObject *>(self)); // Remove extra ref
    else
        makeValid(self); // Make the object valid again
}

void getOwnership(PyObject *pyObj)
{
    if (pyObj)
        setSequenceOwnership(pyObj, true);
}

void releaseOwnership(SbkObject *self)
{
    // skip if the ownership have already moved to c++
    auto *selfType = Py_TYPE(self);
    if (!self->d->hasOwnership || Shiboken::Conversions::pythonTypeIsValueType(PepType_SOTP(selfType)->converter))
        return;

    // remove object ownership
    self->d->hasOwnership = false;

    // If We have control over object life
    if (self->d->containsCppWrapper)
        Py_INCREF(reinterpret_cast<PyObject *>(self)); // keep the python object alive until the wrapper destructor call
    else
        invalidate(self); // If I do not know when this object will die We need to invalidate this to avoid use after
}

void releaseOwnership(PyObject *self)
{
    setSequenceOwnership(self, false);
}

/* Needed forward declarations */
static void recursive_invalidate(PyObject *pyobj, std::set<SbkObject *>& seen);
static void recursive_invalidate(SbkObject *self, std::set<SbkObject *> &seen);

void invalidate(PyObject *pyobj)
{
    std::set<SbkObject *> seen;
    recursive_invalidate(pyobj, seen);
}

void invalidate(SbkObject *self)
{
    std::set<SbkObject *> seen;
    recursive_invalidate(self, seen);
}

static void recursive_invalidate(PyObject *pyobj, std::set<SbkObject *> &seen)
{
    const auto objs = splitPyObject(pyobj);
    for (SbkObject *o : objs)
        recursive_invalidate(o, seen);
}

static void recursive_invalidate(SbkObject *self, std::set<SbkObject *> &seen)
{
    // Skip if this object not is a valid object or if it's already been seen
    if (!self || reinterpret_cast<PyObject *>(self) == Py_None || seen.find(self) != seen.end())
        return;
    seen.insert(self);

    if (!self->d->containsCppWrapper) {
        self->d->validCppObject = false; // Mark object as invalid only if this is not a wrapper class
        BindingManager::instance().releaseWrapper(self);
    }

    // If it is a parent invalidate all children.
    if (self->d->parentInfo) {
        // Create a copy because this list can be changed during the process
        ChildrenList copy = self->d->parentInfo->children;

        for (SbkObject *child : copy) {
            // invalidate the child
            recursive_invalidate(child, seen);

            // if the parent not is a wrapper class, then remove children from him, because We do not know when this object will be destroyed
            if (!self->d->validCppObject)
                removeParent(child, true, true);
        }
    }

    // If has ref to other objects invalidate all
    if (self->d->referredObjects) {
        RefCountMap &refCountMap = *(self->d->referredObjects);
        for (auto it = refCountMap.begin(), end = refCountMap.end(); it != end; ++it)
            recursive_invalidate(it->second, seen);
    }
}

void makeValid(SbkObject *self)
{
    // Skip if this object not is a valid object
    if (!self || reinterpret_cast<PyObject *>(self) == Py_None || self->d->validCppObject)
        return;

    // Mark object as invalid only if this is not a wrapper class
    self->d->validCppObject = true;

    // If it is a parent make  all children valid
    if (self->d->parentInfo) {
        for (SbkObject *child : self->d->parentInfo->children)
            makeValid(child);
    }

    // If has ref to other objects make all valid again
    if (self->d->referredObjects) {
        RefCountMap &refCountMap = *(self->d->referredObjects);
        RefCountMap::iterator iter;
        for (auto it = refCountMap.begin(), end = refCountMap.end(); it != end; ++it) {
            if (Shiboken::Object::checkType(it->second))
                makeValid(reinterpret_cast<SbkObject *>(it->second));
        }
    }
}

void *cppPointer(SbkObject *pyObj, PyTypeObject *desiredType)
{
    PyTypeObject *pyType = Py_TYPE(pyObj);
    auto *sotp = PepType_SOTP(pyType);
    int idx = 0;
    if (sotp->is_multicpp)
        idx = getTypeIndexOnHierarchy(pyType, desiredType);
    if (pyObj->d->cptr)
        return pyObj->d->cptr[idx];
    return nullptr;
}

std::vector<void *> cppPointers(SbkObject *pyObj)
{
    int n = getNumberOfCppBaseClasses(Py_TYPE(pyObj));
    std::vector<void *> ptrs(n);
    for (int i = 0; i < n; ++i)
        ptrs[i] = pyObj->d->cptr[i];
    return ptrs;
}


bool setCppPointer(SbkObject *sbkObj, PyTypeObject *desiredType, void *cptr)
{
    PyTypeObject *type = Py_TYPE(sbkObj);
    int idx = 0;
    if (PepType_SOTP(type)->is_multicpp)
        idx = getTypeIndexOnHierarchy(type, desiredType);

    const bool alreadyInitialized = sbkObj->d->cptr[idx] != nullptr;
    if (alreadyInitialized)
        PyErr_Format(PyExc_RuntimeError, "You can't initialize an %s object in class %s twice!",
                                         desiredType->tp_name, type->tp_name);
    else
        sbkObj->d->cptr[idx] = cptr;

    sbkObj->d->cppObjectCreated = true;
    return !alreadyInitialized;
}

bool isValid(PyObject *pyObj)
{
    if (!pyObj || pyObj == Py_None
        || PyType_Check(pyObj) != 0
        || Py_TYPE(Py_TYPE(pyObj)) != SbkObjectType_TypeF()) {
        return true;
    }

    auto priv = reinterpret_cast<SbkObject *>(pyObj)->d;

    if (!priv->cppObjectCreated && isUserType(pyObj)) {
        PyErr_Format(PyExc_RuntimeError, "'__init__' method of object's base class (%s) not called.",
                     Py_TYPE(pyObj)->tp_name);
        return false;
    }

    if (!priv->validCppObject) {
        PyErr_Format(PyExc_RuntimeError, "Internal C++ object (%s) already deleted.",
                     Py_TYPE(pyObj)->tp_name);
        return false;
    }

    return true;
}

bool isValid(SbkObject *pyObj, bool throwPyError)
{
    if (!pyObj)
        return false;

    SbkObjectPrivate *priv = pyObj->d;
    if (!priv->cppObjectCreated && isUserType(reinterpret_cast<PyObject *>(pyObj))) {
        if (throwPyError)
            PyErr_Format(PyExc_RuntimeError, "Base constructor of the object (%s) not called.",
                         Py_TYPE(pyObj)->tp_name);
        return false;
    }

    if (!priv->validCppObject) {
        if (throwPyError)
            PyErr_Format(PyExc_RuntimeError, "Internal C++ object (%s) already deleted.",
                         (Py_TYPE(pyObj))->tp_name);
        return false;
    }

    return true;
}

bool isValid(PyObject *pyObj, bool throwPyError)
{
    if (!pyObj || pyObj == Py_None ||
        !PyType_IsSubtype(Py_TYPE(pyObj), SbkObject_TypeF())) {
        return true;
    }
    return isValid(reinterpret_cast<SbkObject *>(pyObj), throwPyError);
}

SbkObject *findColocatedChild(SbkObject *wrapper,
                              const PyTypeObject *instanceType)
{
    // Degenerate case, wrapper is the correct wrapper.
    if (reinterpret_cast<const void *>(Py_TYPE(wrapper)) == reinterpret_cast<const void *>(instanceType))
        return wrapper;

    if (!(wrapper->d && wrapper->d->cptr))
        return nullptr;

    ParentInfo *pInfo = wrapper->d->parentInfo;
    if (!pInfo)
        return nullptr;

    ChildrenList &children = pInfo->children;

    for (SbkObject *child : children) {
        if (!(child->d && child->d->cptr))
            continue;
        if (child->d->cptr[0] == wrapper->d->cptr[0]) {
            return reinterpret_cast<const void *>(Py_TYPE(child)) == reinterpret_cast<const void *>(instanceType)
                ? child : findColocatedChild(child, instanceType);
        }
    }
    return nullptr;
}

PyObject *newObject(PyTypeObject *instanceType,
                    void *cptr,
                    bool hasOwnership,
                    bool isExactType,
                    const char *typeName)
{
    // Try to find the exact type of cptr.
    if (!isExactType) {
        if (PyTypeObject *exactType = ObjectType::typeForTypeName(typeName))
            instanceType = exactType;
        else
            instanceType = BindingManager::instance().resolveType(&cptr, instanceType);
    }

    bool shouldCreate = true;
    bool shouldRegister = true;
    SbkObject *self = nullptr;

    // Some logic to ensure that colocated child field does not overwrite the parent
    if (BindingManager::instance().hasWrapper(cptr)) {
        SbkObject *existingWrapper = BindingManager::instance().retrieveWrapper(cptr);

        self = findColocatedChild(existingWrapper, instanceType);
        if (self) {
            // Wrapper already registered for cptr.
            // This should not ideally happen, binding code should know when a wrapper
            // already exists and retrieve it instead.
            shouldRegister = shouldCreate = false;
        } else if (hasOwnership &&
                  (!(Shiboken::Object::hasCppWrapper(existingWrapper) ||
                     Shiboken::Object::hasOwnership(existingWrapper)))) {
            // Old wrapper is likely junk, since we have ownership and it doesn't.
            BindingManager::instance().releaseWrapper(existingWrapper);
        } else {
            // Old wrapper may be junk caused by some bug in identifying object deletion
            // but it may not be junk when a colocated field is accessed for an
            // object which was not created by python (returned from c++ factory function).
            // Hence we cannot release the wrapper confidently so we do not register.
            shouldRegister = false;
        }
    }

    if (shouldCreate) {
        self = reinterpret_cast<SbkObject *>(SbkObject_tp_new(instanceType, nullptr, nullptr));
        self->d->cptr[0] = cptr;
        self->d->hasOwnership = hasOwnership;
        self->d->validCppObject = 1;
        if (shouldRegister) {
            BindingManager::instance().registerWrapper(self, cptr);
        }
    } else {
        Py_IncRef(reinterpret_cast<PyObject *>(self));
    }
    return reinterpret_cast<PyObject *>(self);
}

void destroy(SbkObject *self, void *cppData)
{
    // Skip if this is called with NULL pointer this can happen in derived classes
    if (!self)
        return;

    // This can be called in c++ side
    Shiboken::GilState gil;

    // Remove all references attached to this object
    clearReferences(self);

    // Remove the object from parent control

    // Verify if this object has parent
    bool hasParent = (self->d->parentInfo && self->d->parentInfo->parent);

    if (self->d->parentInfo) {
        // Check for children information and make all invalid if they exists
        _destroyParentInfo(self, true);
        // If this object has parent then the pyobject can be invalid now, because we remove the last ref after remove from parent
    }

    //if !hasParent this object could still alive
    if (!hasParent && self->d->containsCppWrapper && !self->d->hasOwnership) {
        // Remove extra ref used by c++ object this will case the pyobject destruction
        // This can cause the object death
        Py_DECREF(reinterpret_cast<PyObject *>(self));
    }

    //Python Object is not destroyed yet
    if (cppData && Shiboken::BindingManager::instance().hasWrapper(cppData)) {
        // Remove from BindingManager
        Shiboken::BindingManager::instance().releaseWrapper(self);
        self->d->hasOwnership = false;

        // the cpp object instance was deleted
        delete[] self->d->cptr;
        self->d->cptr = nullptr;
    }

    // After this point the object can be death do not use the self pointer bellow
}

void removeParent(SbkObject *child, bool giveOwnershipBack, bool keepReference)
{
    ParentInfo *pInfo = child->d->parentInfo;
    if (!pInfo || !pInfo->parent) {
        if (pInfo && pInfo->hasWrapperRef) {
            pInfo->hasWrapperRef = false;
        }
        return;
    }

    ChildrenList &oldBrothers = pInfo->parent->d->parentInfo->children;
    // Verify if this child is part of parent list
    auto iChild = oldBrothers.find(child);
    if (iChild == oldBrothers.end())
        return;

    oldBrothers.erase(iChild);

    pInfo->parent = nullptr;

    // This will keep the wrapper reference, will wait for wrapper destruction to remove that
    if (keepReference &&
        child->d->containsCppWrapper) {
        //If have already a extra ref remove this one
        if (pInfo->hasWrapperRef)
            Py_DECREF(child);
        else
            pInfo->hasWrapperRef = true;
        return;
    }

    // Transfer ownership back to Python
    child->d->hasOwnership = giveOwnershipBack;

    // Remove parent ref
    Py_DECREF(child);
}

void setParent(PyObject *parent, PyObject *child)
{
    if (!child || child == Py_None || child == parent)
        return;

    /*
     * setParent is recursive when the child is a native Python sequence, i.e. objects not binded by Shiboken
     * like tuple and list.
     *
     * This "limitation" exists to fix the following problem: A class multiple inherits QObject and QString,
     * so if you pass this class to someone that takes the ownership, we CAN'T enter in this if, but hey! QString
     * follows the sequence protocol.
     */
    if (PySequence_Check(child) && !Object::checkType(child)) {
        Shiboken::AutoDecRef seq(PySequence_Fast(child, nullptr));
        for (Py_ssize_t i = 0, max = PySequence_Size(seq); i < max; ++i)
            setParent(parent, PySequence_Fast_GET_ITEM(seq.object(), i));
        return;
    }

    bool parentIsNull = !parent || parent == Py_None;
    auto parent_ = reinterpret_cast<SbkObject *>(parent);
    auto child_ = reinterpret_cast<SbkObject *>(child);

    if (!parentIsNull) {
        if (!parent_->d->parentInfo)
            parent_->d->parentInfo = new ParentInfo;

        // do not re-add a child
        if (child_->d->parentInfo && (child_->d->parentInfo->parent == parent_))
            return;
    }

    ParentInfo *pInfo = child_->d->parentInfo;
    bool hasAnotherParent = pInfo && pInfo->parent && pInfo->parent != parent_;

    //Avoid destroy child during reparent operation
    Py_INCREF(child);

    // check if we need to remove this child from the old parent
    if (parentIsNull || hasAnotherParent)
        removeParent(child_);

    // Add the child to the new parent
    pInfo = child_->d->parentInfo;
    if (!parentIsNull) {
        if (!pInfo)
            pInfo = child_->d->parentInfo = new ParentInfo;

        pInfo->parent = parent_;
        parent_->d->parentInfo->children.insert(child_);

        // Add Parent ref
        Py_INCREF(child_);

        // Remove ownership
        child_->d->hasOwnership = false;
    }

    // Remove previous safe ref
    Py_DECREF(child);
}

void deallocData(SbkObject *self, bool cleanup)
{
    // Make cleanup if this is not a wrapper otherwise this will be done on wrapper destructor
    if(cleanup) {
        removeParent(self);

        if (self->d->parentInfo)
            _destroyParentInfo(self, true);

        clearReferences(self);
    }

    if (self->d->cptr) {
        // Remove from BindingManager
        Shiboken::BindingManager::instance().releaseWrapper(self);
        delete[] self->d->cptr;
        self->d->cptr = nullptr;
        // delete self->d; PYSIDE-205: wrong!
    }
    delete self->d; // PYSIDE-205: always delete d.
    Py_XDECREF(self->ob_dict);
    Py_TYPE(self)->tp_free(self);
}

void setTypeUserData(SbkObject *wrapper, void *userData, DeleteUserDataFunc d_func)
{
    auto *type = Py_TYPE(wrapper);
    auto *sotp = PepType_SOTP(type);
    if (sotp->user_data)
        sotp->d_func(sotp->user_data);

    sotp->d_func = d_func;
    sotp->user_data = userData;
}

void *getTypeUserData(SbkObject *wrapper)
{
    auto *type = Py_TYPE(wrapper);
    return PepType_SOTP(type)->user_data;
}

static inline bool isNone(const PyObject *o)
{
    return o == nullptr || o == Py_None;
}

static void removeRefCountKey(SbkObject *self, const char *key)
{
    if (self->d->referredObjects) {
        const auto iterPair = self->d->referredObjects->equal_range(key);
        if (iterPair.first != iterPair.second) {
            decRefPyObjectList(iterPair.first, iterPair.second);
            self->d->referredObjects->erase(iterPair.first, iterPair.second);
        }
    }
}

void keepReference(SbkObject *self, const char *key, PyObject *referredObject, bool append)
{
    if (isNone(referredObject)) {
        removeRefCountKey(self, key);
        return;
    }

    if (!self->d->referredObjects) {
        self->d->referredObjects =
            new Shiboken::RefCountMap{RefCountMap::value_type{key, referredObject}};
        Py_INCREF(referredObject);
        return;
    }

    RefCountMap &refCountMap = *(self->d->referredObjects);
    const auto iterPair = refCountMap.equal_range(key);
    if (std::any_of(iterPair.first, iterPair.second,
                    [referredObject](const RefCountMap::value_type &v) { return v.second == referredObject; })) {
        return;
    }

    if (!append && iterPair.first != iterPair.second) {
        decRefPyObjectList(iterPair.first, iterPair.second);
        refCountMap.erase(iterPair.first, iterPair.second);
    }

    refCountMap.insert(RefCountMap::value_type{key, referredObject});
    Py_INCREF(referredObject);
}

void removeReference(SbkObject *self, const char *key, PyObject *referredObject)
{
    if (!isNone(referredObject))
        removeRefCountKey(self, key);
}

void clearReferences(SbkObject *self)
{
    if (!self->d->referredObjects)
        return;

    RefCountMap &refCountMap = *(self->d->referredObjects);
    for (auto it = refCountMap.begin(), end = refCountMap.end(); it != end; ++it)
        Py_DECREF(it->second);
    self->d->referredObjects->clear();
}

// Helpers for debug / info formatting

static std::vector<PyTypeObject *> getBases(SbkObject *self)
{
    return ObjectType::isUserType(Py_TYPE(self))
        ? getCppBaseClasses(Py_TYPE(self))
        : std::vector<PyTypeObject *>(1, Py_TYPE(self));
}

void _debugFormat(std::ostream &s, SbkObject *self)
{
    assert(self);
    auto *d = self->d;
    if (!d) {
        s  << "[Invalid]";
        return;
    }
    if (d->cptr) {
        const std::vector<PyTypeObject *> bases = getBases(self);
        for (size_t i = 0, size = bases.size(); i < size; ++i)
            s << ", C++: " << bases[i]->tp_name << '/' << self->d->cptr[i];
    } else {
         s << " [Deleted]";
    }
    if (d->hasOwnership)
        s << " [hasOwnership]";
    if (d->containsCppWrapper)
        s << " [containsCppWrapper]";
    if (d->validCppObject)
        s << " [validCppObject]";
    if (d->cppObjectCreated)
        s << " [wasCreatedByPython]";
    if (d->parentInfo) {
        if (auto *parent = d->parentInfo->parent)
            s << ", parent=" << reinterpret_cast<PyObject *>(parent)->ob_type->tp_name
                << '/' << parent;
        if (!d->parentInfo->children.empty())
            s << ", " << d->parentInfo->children.size() << " child(ren)";
    }
    if (d->referredObjects && !d->referredObjects->empty())
        s << ", " << d->referredObjects->size() << " referred object(s)";
}

std::string info(SbkObject *self)
{
    std::ostringstream s;

    if (self->d && self->d->cptr) {
        const std::vector<PyTypeObject *> bases = getBases(self);

        s << "C++ address....... ";
        for (size_t i = 0, size = bases.size(); i < size; ++i)
            s << bases[i]->tp_name << '/' << self->d->cptr[i] << ' ';
        s << "\n";
    }
    else {
        s << "C++ address....... <<Deleted>>\n";
    }

    s << "hasOwnership...... " << bool(self->d->hasOwnership) << "\n"
         "containsCppWrapper " << self->d->containsCppWrapper << "\n"
         "validCppObject.... " << self->d->validCppObject << "\n"
         "wasCreatedByPython " << self->d->cppObjectCreated << "\n";


    if (self->d->parentInfo && self->d->parentInfo->parent) {
        s << "parent............ ";
        Shiboken::AutoDecRef parent(PyObject_Str(reinterpret_cast<PyObject *>(self->d->parentInfo->parent)));
        s << String::toCString(parent) << "\n";
    }

    if (self->d->parentInfo && !self->d->parentInfo->children.empty()) {
        s << "children.......... ";
        for (SbkObject *sbkChild : self->d->parentInfo->children) {
            Shiboken::AutoDecRef child(PyObject_Str(reinterpret_cast<PyObject *>(sbkChild)));
            s << String::toCString(child) << ' ';
        }
        s << '\n';
    }

    if (self->d->referredObjects && !self->d->referredObjects->empty()) {
        Shiboken::RefCountMap &map = *self->d->referredObjects;
        s << "referred objects.. ";
        std::string lastKey;
        for (auto it = map.begin(), end = map.end(); it != end; ++it) {
            if (it->first != lastKey) {
                if (!lastKey.empty())
                    s << "                   ";
                s << '"' << it->first << "\" => ";
                lastKey = it->first;
            }
            Shiboken::AutoDecRef obj(PyObject_Str(it->second));
            s << String::toCString(obj) << ' ';
        }
        s << '\n';
    }
    return s.str();
}

} // namespace Object

} // namespace Shiboken

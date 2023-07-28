// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "pysidestaticstrings.h"
#include <sbkstring.h>

#define STATIC_STRING_IMPL(funcName, value) \
PyObject *funcName() \
{ \
    static PyObject *const s = Shiboken::String::createStaticString(value); \
    return s; \
}

namespace PySide
{
namespace PySideName
{
STATIC_STRING_IMPL(qtConnect, "connect")
STATIC_STRING_IMPL(qtDisconnect, "disconnect")
STATIC_STRING_IMPL(qtEmit, "emit")
STATIC_STRING_IMPL(dict_ring, "dict_ring")
STATIC_STRING_IMPL(fset, "fset")
STATIC_STRING_IMPL(im_func, "im_func")
STATIC_STRING_IMPL(im_self, "im_self")
STATIC_STRING_IMPL(name, "name")
STATIC_STRING_IMPL(parameters, "parameters")
STATIC_STRING_IMPL(property, "property")
STATIC_STRING_IMPL(select_id, "select_id")
} // namespace PyName
namespace PySideMagicName
{
STATIC_STRING_IMPL(code, "__code__")
STATIC_STRING_IMPL(doc, "__doc__")
STATIC_STRING_IMPL(func, "__func__")
STATIC_STRING_IMPL(name, "__name__")
STATIC_STRING_IMPL(property_methods, "__property_methods__")
} // namespace PyMagicName
} // namespace PySide

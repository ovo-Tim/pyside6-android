#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for methods that receive references to objects.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import *

class ExtendedReference(Reference):
    def __init__(self):
        Reference.__init__(self)
        self.uses_reference_virtual_called = False
        self.uses_const_reference_virtual_called = False
        self.reference_inc = 1
        self.const_reference_inc = 2
        self.multiplier = 333

    def usesReferenceVirtual(self, ref, inc):
        self.uses_reference_virtual_called = True
        return ref.objId() + inc + self.reference_inc

    def usesConstReferenceVirtual(self, ref, inc):
        self.uses_const_reference_virtual_called = True
        return ref.objId() + inc + self.const_reference_inc

    def alterReferenceIdVirtual(self, ref):
        ref.setObjId(ref.objId() * self.multiplier)


class ReferenceTest(unittest.TestCase):
    '''Test case for methods that receive references to objects.'''

    def testMethodThatReceivesReference(self):
        '''Test a method that receives a reference to an object as argument.'''
        objId = 123
        r = Reference(objId)
        self.assertEqual(Reference.usesReference(r), objId)

    def testCantSegFaultWhenReceiveNone(self):
        '''do not segfault when receiving None as argument.'''
        s = Str()
        self.assertTrue(None == s)

    def testMethodThatReceivesConstReference(self):
        '''Test a method that receives a const reference to an object as argument.'''
        objId = 123
        r = Reference(objId)
        self.assertEqual(Reference.usesConstReference(r), objId)

    def testModificationOfReference(self):
        '''Tests if the identity of a reference argument is preserved when passing it to be altered in C++.'''
        objId = 123
        r1 = Reference(objId)
        r1.alterReferenceIdVirtual(r1)
        self.assertEqual(r1.objId(), objId * Reference.multiplier())

    def testModificationOfReferenceCallingAVirtualIndirectly(self):
        '''Tests if the identity of a reference argument is preserved when passing it to be altered in C++ through a method that calls a virtual method.'''
        objId = 123
        r1 = Reference(objId)
        r1.callAlterReferenceIdVirtual(r1)
        self.assertEqual(r1.objId(), objId * Reference.multiplier())

    def testModificationOfReferenceCallingAReimplementedVirtualIndirectly(self):
        '''Test if a Python override of a virtual method with a reference parameter called from C++ alters the argument properly.'''
        objId = 123
        r = Reference(objId)
        er = ExtendedReference()
        result = er.callAlterReferenceIdVirtual(r)
        self.assertEqual(r.objId(), objId * er.multiplier)

    def testReimplementedVirtualMethodCallWithReferenceParameter(self):
        '''Test if a Python override of a virtual method with a reference parameter is correctly called from C++.'''
        inc = 9
        objId = 123
        r = Reference(objId)
        er = ExtendedReference()
        result = er.callUsesReferenceVirtual(r, inc)
        self.assertEqual(result, objId + inc + er.reference_inc)

    def testReimplementedVirtualMethodCallWithConstReferenceParameter(self):
        '''Test if a Python override of a virtual method with a const reference parameter is correctly called from C++.'''
        inc = 9
        objId = 123
        r = Reference(objId)
        er = ExtendedReference()
        result = er.callUsesConstReferenceVirtual(r, inc)
        self.assertEqual(result, objId + inc + er.const_reference_inc)

if __name__ == '__main__':
    unittest.main()


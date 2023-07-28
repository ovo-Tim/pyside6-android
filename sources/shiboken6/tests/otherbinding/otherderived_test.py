#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for OtherDerived class'''

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import Abstract, Derived
from other import OtherDerived, Number

class Multiple(Derived, Number):
    def __init__(self):
        Derived.__init__(self, 42)
        Number.__init__(self, 42)

    def testCall(self):
        return True

class OtherDeviant(OtherDerived):
    def __init__(self):
        OtherDerived.__init__(self)
        self.pure_virtual_called = False
        self.unpure_virtual_called = False

    def pureVirtual(self):
        self.pure_virtual_called = True

    def unpureVirtual(self):
        self.unpure_virtual_called = True

    def className(self):
        return 'OtherDeviant'

class MultipleTest(unittest.TestCase):
    '''Test case for Multiple derived class'''

    def testConstructor(self):
        o = Multiple()
        self.assertTrue(isinstance(o, Multiple))
        self.assertTrue(isinstance(o, Number))
        self.assertTrue(isinstance(o, Derived))
        del o
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()

    def testMethodCall(self):
        o = Multiple()
        self.assertTrue(o.id_(), 42)
        self.assertTrue(o.value(), 42)
        self.assertTrue(o.testCall())

class OtherDerivedTest(unittest.TestCase):
    '''Test case for OtherDerived class'''

    def testParentClassMethodsAvailability(self):
        '''Test if OtherDerived class really inherits its methods from parent.'''
        inherited_methods = set(['callPureVirtual', 'callUnpureVirtual',
                                 'id_', 'pureVirtual', 'unpureVirtual'])
        self.assertTrue(inherited_methods.issubset(dir(OtherDerived)))

    def testReimplementedPureVirtualMethodCall(self):
        '''Test if a Python override of a implemented pure virtual method is correctly called from C++.'''
        d = OtherDeviant()
        d.callPureVirtual()
        self.assertTrue(d.pure_virtual_called)

    def testReimplementedVirtualMethodCall(self):
        '''Test if a Python override of a reimplemented virtual method is correctly called from C++.'''
        d = OtherDeviant()
        d.callUnpureVirtual()
        self.assertTrue(d.unpure_virtual_called)

    def testVirtualMethodCallString(self):
        '''Test virtual method call returning string.'''
        d = OtherDerived()
        self.assertEqual(d.className(), 'OtherDerived')
        self.assertEqual(d.getClassName(), 'OtherDerived')

    def testReimplementedVirtualMethodCallReturningString(self):
        '''Test if a Python override of a reimplemented virtual method is correctly called from C++.'''
        d = OtherDeviant()
        self.assertEqual(d.className(), 'OtherDeviant')
        self.assertEqual(d.getClassName(), 'OtherDeviant')

    def testCallToMethodWithAbstractArgument(self):
        '''Call to method that expects an Abstract argument.'''
        objId = 123
        d = OtherDerived(objId)
        self.assertEqual(Abstract.getObjectId(d), objId)

if __name__ == '__main__':
    unittest.main()


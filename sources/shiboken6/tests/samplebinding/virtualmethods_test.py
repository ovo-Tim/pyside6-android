#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for virtual methods.'''

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import *

import warnings

class ExtendedVirtualMethods(VirtualMethods):
    def __init__(self):
        VirtualMethods.__init__(self)
        self.virtual_method0_called = False

    def virtualMethod0(self, pt, val, cpx, b):
        self.virtual_method0_called = True
        return VirtualMethods.virtualMethod0(self, pt, val, cpx, b) * -1.0

    def strListToStdList(self, arg):
        warnings.simplefilter('error')
        # returning wrong type for test purposes.
        return True

    def recursionOnModifiedVirtual(self, arg):
        # check if recursion is caused by injected code that calls C++.
        return VirtualMethods.recursionOnModifiedVirtual(self, arg) + 10

class ExtendedVirtualDaughter(VirtualDaughter):
    def __init__(self, name):
        VirtualDaughter.__init__(self, name)
        self.grand_daughter_name_called = False

    def name(self):
        self.grand_daughter_name_called = True
        return VirtualDaughter.name(self).prepend('Extended')

class ExtendedExtendedVirtualDaughter(ExtendedVirtualDaughter):
    def __init__(self, name):
        ExtendedVirtualDaughter.__init__(self, name)
        self.grand_grand_daughter_name_called = False

    def name(self):
        self.grand_grand_daughter_name_called = True
        return ExtendedVirtualDaughter.name(self).prepend('Extended')

class VirtualMethodsTest(unittest.TestCase):
    '''Test case for virtual methods'''

    def setUp(self):
        self.prefix_from_codeinjection = Str('Pimped')

    def tearDown(self):
        del self.prefix_from_codeinjection
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()

    def testReimplementedVirtualMethod0(self):
        '''Test Python override of a virtual method with various different parameters is correctly called from C++.'''
        vm = VirtualMethods()
        evm = ExtendedVirtualMethods()
        pt = Point(1.1, 2.2)
        val = 4
        cpx = complex(3.3, 4.4)
        b = True
        result0 = vm.callVirtualMethod0(pt, val, cpx, b)
        result1 = evm.callVirtualMethod0(pt, val, cpx, b)
        self.assertEqual(result0 * -1.0, result1)

    def testRecursionOnModifiedVirtual(self):
        evm = ExtendedVirtualMethods()
        self.assertEqual(evm.recursionOnModifiedVirtual(''), 10)
        self.assertEqual(evm.callRecursionOnModifiedVirtual(''), 10)

    def testReimplementedVirtualMethodInheritedFromGrandParent(self):
        '''Test Python override of a virtual method inherited from a grand parent.'''
        original_name = 'Foo'
        evd = ExtendedVirtualDaughter(original_name)

        self.assertEqual(VirtualDaughter.name(evd), original_name)
        self.assertEqual(VirtualMethods.name(evd), original_name)
        self.assertFalse(evd.grand_daughter_name_called)

        name = evd.callName()
        self.assertTrue(evd.grand_daughter_name_called)
        self.assertEqual(evd.name().prepend(self.prefix_from_codeinjection), name)

    def testReimplementedVirtualMethodInheritedFromGrandGrandParent(self):
        '''Test Python override of a virtual method inherited from a grand grand parent.'''
        original_name = 'Foo'
        eevd = ExtendedExtendedVirtualDaughter(original_name)

        self.assertEqual(VirtualDaughter.name(eevd), original_name)
        self.assertEqual(VirtualMethods.name(eevd), original_name)
        self.assertFalse(eevd.grand_daughter_name_called)
        self.assertFalse(eevd.grand_grand_daughter_name_called)

        name = eevd.callName()
        self.assertTrue(eevd.grand_daughter_name_called)
        self.assertTrue(eevd.grand_grand_daughter_name_called)
        self.assertEqual(eevd.name().prepend(self.prefix_from_codeinjection), name)

    def testStringView(self):
        virtual_methods = VirtualMethods()
        self.assertEqual(virtual_methods.stringViewLength('bla'), 3)


class PrettyErrorMessageTest(unittest.TestCase):
    def testIt(self):
        obj = ExtendedVirtualMethods()
        self.assertRaises(RuntimeWarning, obj.callStrListToStdList, StrList())

if __name__ == '__main__':
    unittest.main()


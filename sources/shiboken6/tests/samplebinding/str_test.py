#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for a method that receives a reference to class that is implicitly convertible from a Python native type.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import Str

class StrTest(unittest.TestCase):
    '''Test cases for thr Str class.'''

    def test__str__Method(self):
        '''Test if the binding correcly implements the Python __str__ method.'''
        s1 = 'original string'
        s2 = Str(s1)
        self.assertEqual(s1, s2)
        self.assertEqual(s1, str(s2))

    def testPassExactClassAsReferenceToArgument(self):
        '''Test passing the expected class as an argument to a method that expects a reference.'''
        s1 = Str('This is %VAR!').arg(Str('Sparta'))
        self.assertEqual(str(s1), 'This is Sparta!')

    def testPassPythonTypeImplictlyConvertibleToAClassUsedAsReference(self):
        '''Test passing a Python class implicitly convertible to a wrapped class that is expected to be passed as reference.'''
        s1 = Str('This is %VAR!').arg('Athens')
        self.assertEqual(str(s1), 'This is Athens!')

    def testSequenceOperators(self):
        s1 = Str("abcdef")
        self.assertEqual(len(s1), 6);
        self.assertEqual(len(Str()), 0);

        # getitem
        self.assertEqual(s1[0], "a");
        self.assertEqual(s1[1], "b");
        self.assertEqual(s1[2], "c");
        self.assertEqual(s1[3], "d");
        self.assertEqual(s1[4], "e");
        self.assertEqual(s1[5], "f");
        self.assertEqual(s1[-1], "f");
        self.assertEqual(s1[-2], "e");

        self.assertRaises(TypeError, s1.__getitem__, 6)

        # setitem
        s1[0] = 'A'
        s1[1] = 'B'
        self.assertEqual(s1[0], 'A');
        self.assertEqual(s1[1], 'B');
        self.assertRaises(TypeError, s1.__setitem__(6, 67))

    def testReverseOperator(self):
        s1 = Str("hello")
        n1 = 2
        self.assertEqual(s1+2, "hello2")
        self.assertEqual(2+s1, "2hello")

    def testToIntError(self):
        self.assertEqual(Str('Z').toInt(), (0, False))

    def testToIntWithDecimal(self):
        decimal = Str('37')
        val, ok = decimal.toInt()
        self.assertEqual(type(val), int)
        self.assertEqual(type(ok), bool)
        self.assertEqual(val, int(str(decimal)))

    def testToIntWithOctal(self):
        octal = Str('52')
        val, ok = octal.toInt(8)
        self.assertEqual(type(val), int)
        self.assertEqual(type(ok), bool)
        self.assertEqual(val, int(str(octal), 8))

    def testToIntWithHexadecimal(self):
        hexa = Str('2A')
        val, ok = hexa.toInt(16)
        self.assertEqual(type(val), int)
        self.assertEqual(type(ok), bool)
        self.assertEqual(val, int(str(hexa), 16))
        self.assertEqual(hexa.toInt(), (0, False))

if __name__ == '__main__':
    unittest.main()


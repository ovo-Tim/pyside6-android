#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test case for inplicit converting C++ numeric types.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()
import sys
import sample

# Hardcode the limits of the underlying C-types depending on architecture and memory
# model (taking MSVC using LLP64 into account).
cIntMin = -2147483648
cIntMax = 2147483647
cLongMin = cIntMin
cLongMax = cIntMax
maxRepresentableInt = sys.maxsize
is64bitArchitecture = maxRepresentableInt > 2**32
if is64bitArchitecture and sys.platform != 'win32':
    cLongMin = -9223372036854775808
    cLongMax = 9223372036854775807

class NumericTester(unittest.TestCase):
    '''Helper class for numeric comparison testing'''

    def assertRaises(self, *args, **kwds):
        if not hasattr(sys, "pypy_version_info"):
            # PYSIDE-535: PyPy complains "Fatal RPython error: NotImplementedError"
            return super().assertRaises(*args, **kwds)

    def check_value(self, source, expected, callback, desired_type=None):
        result = callback(source)
        self.assertEqual(result, expected)

        if desired_type:
            self.assertEqual(type(result), desired_type)


class FloatImplicitConvert(NumericTester):
    '''Test case for implicit converting C++ numeric types.'''

    def testFloatAsInt(self):
        '''Float as Int'''
        self.check_value(3.14, 3, sample.acceptInt, int)
        self.assertRaises(OverflowError, sample.acceptInt, cIntMax + 400)

    def testFloatAsLong(self):
        '''Float as Long'''
        #C++ longs are python ints for us
        self.check_value(3.14, 3, sample.acceptLong, int)
        self.assertRaises(OverflowError, sample.acceptLong, cLongMax + 400)

    def testFloatAsUInt(self):
        '''Float as unsigned Int'''
        self.check_value(3.14, 3, sample.acceptUInt, int)
        self.assertRaises(OverflowError, sample.acceptUInt, -3.14)

    def testFloatAsULong(self):
        '''Float as unsigned Long'''
        #FIXME Breaking with SystemError "bad argument to internal function"
        self.check_value(3.14, 3, sample.acceptULong, int)
        self.assertRaises(OverflowError, sample.acceptULong, -3.14)

    def testFloatAsDouble(self):
        '''Float as double'''
        self.check_value(3.14, 3.14, sample.acceptDouble, float)


class IntImplicitConvert(NumericTester):
    '''Test case for implicit converting C++ numeric types.'''

    def testIntAsInt(self):
        '''Int as Int'''
        self.check_value(3, 3, sample.acceptInt, int)

    def testIntAsLong(self):
        '''Int as Long'''
        self.check_value(3, 3, sample.acceptLong, int)

        # cLongMax goes here as CPython implements int as a C long
        self.check_value(cLongMax, cLongMax, sample.acceptLong, int)
        self.check_value(cLongMin, cLongMin, sample.acceptLong, int)

    def testIntAsUInt(self):
        '''Int as unsigned Int'''
        self.check_value(3, 3, sample.acceptUInt, int)
        self.assertRaises(OverflowError, sample.acceptUInt, -3)

    def testIntAsULong(self):
        '''Int as unsigned Long'''
        self.check_value(3, 3, sample.acceptULong, int)
        self.assertRaises(OverflowError, sample.acceptULong, -3)

    def testFloatAsDouble(self):
        '''Float as double'''
        self.check_value(3.14, 3.14, sample.acceptDouble, float)


class LongImplicitConvert(NumericTester):
    '''Test case for implicit converting C++ numeric types.'''

    def testLongAsInt(self):
        '''Long as Int'''
        self.check_value(24224, 24224, sample.acceptInt, int)
        self.assertRaises(OverflowError, sample.acceptInt, cIntMax + 20)

    def testLongAsLong(self):
        '''Long as Long'''
        self.check_value(2405, 2405, sample.acceptLong, int)
        self.assertRaises(OverflowError, sample.acceptLong, cLongMax + 20)

    def testLongAsUInt(self):
        '''Long as unsigned Int'''
        self.check_value(260, 260, sample.acceptUInt, int)
        self.assertRaises(OverflowError, sample.acceptUInt, -42)

    def testLongAsULong(self):
        '''Long as unsigned Long'''
        self.check_value(128, 128, sample.acceptULong, int)
        self.assertRaises(OverflowError, sample.acceptULong, -334)

    def testLongAsDouble(self):
        '''Float as double'''
        self.check_value(42, 42, sample.acceptDouble, float)


if __name__ == '__main__':
    unittest.main()

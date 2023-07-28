#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for overloads involving static and non-static versions of a method.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import SimpleFile

class SimpleFile2 (SimpleFile):
    def exists(self):
        return "Mooo"

class SimpleFile3 (SimpleFile):
    pass

class SimpleFile4 (SimpleFile):
    exists = 5

class StaticNonStaticMethodsTest(unittest.TestCase):
    '''Test cases for overloads involving static and non-static versions of a method.'''

    def setUp(self):
        filename = f'simplefile{os.getpid()}.txt'
        self.existing_filename = Path(filename)
        self.delete_file = False
        if not self.existing_filename.exists():
            with self.existing_filename.open('w') as f:
                for line in range(10):
                    f.write('sbrubbles\n')
            self.delete_file = True

        self.non_existing_filename = Path('inexistingfile.txt')
        i = 0
        while self.non_existing_filename.exists():
            i += 1
            filename = 'inexistingfile-{i}.txt'
            self.non_existing_filename = Path(filename)

    def tearDown(self):
        if self.delete_file:
            os.remove(self.existing_filename)

    def testCallingStaticMethodWithClass(self):
        '''Call static method using class.'''
        self.assertTrue(SimpleFile.exists(os.fspath(self.existing_filename)))
        self.assertFalse(SimpleFile.exists(os.fspath(self.non_existing_filename)))

    def testCallingStaticMethodWithInstance(self):
        '''Call static method using instance of class.'''
        f = SimpleFile(os.fspath(self.non_existing_filename))
        self.assertTrue(f.exists(os.fspath(self.existing_filename)))
        self.assertFalse(f.exists(os.fspath(self.non_existing_filename)))

    def testCallingInstanceMethod(self):
        '''Call instance method.'''
        f1 = SimpleFile(os.fspath(self.non_existing_filename))
        self.assertFalse(f1.exists())
        f2 = SimpleFile(os.fspath(self.existing_filename))
        self.assertTrue(f2.exists())

    def testOverridingStaticNonStaticMethod(self):
        f = SimpleFile2(os.fspath(self.existing_filename))
        self.assertEqual(f.exists(), "Mooo")

        f = SimpleFile3(os.fspath(self.existing_filename))
        self.assertTrue(f.exists())

        f = SimpleFile4(os.fspath(self.existing_filename))
        self.assertEqual(f.exists, 5)

    def testDuckPunchingStaticNonStaticMethod(self):
        f = SimpleFile(os.fspath(self.existing_filename))
        f.exists = lambda : "Meee"
        self.assertEqual(f.exists(), "Meee")

if __name__ == '__main__':
    unittest.main()


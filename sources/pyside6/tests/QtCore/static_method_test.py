#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for overloads involving static and non-static versions of a method.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QFile


class StaticNonStaticMethodsTest(unittest.TestCase):
    '''Test cases for overloads involving static and non-static versions of a method.'''

    def setUp(self):
        filename = 'somefile%d.txt' % os.getpid()
        self.existing_filename = os.path.join(os.path.curdir, filename)
        self.delete_file = False
        if not os.path.exists(self.existing_filename):
            f = open(self.existing_filename, 'w')
            for line in range(10):
                f.write('sbrubbles\n')
            f.close()
            self.delete_file = True

        self.non_existing_filename = os.path.join(os.path.curdir, 'inexistingfile.txt')
        i = 0
        while os.path.exists(self.non_existing_filename):
            i += 1
            filename = 'inexistingfile-%d.txt' % i
            self.non_existing_filename = os.path.join(os.path.curdir, filename)

    def tearDown(self):
        if self.delete_file:
            os.remove(self.existing_filename)

    def testCallingStaticMethodWithClass(self):
        '''Call static method using class.'''
        self.assertTrue(QFile.exists(self.existing_filename))
        self.assertFalse(QFile.exists(self.non_existing_filename))

    def testCallingStaticMethodWithInstance(self):
        '''Call static method using instance of class.'''
        f = QFile(self.non_existing_filename)
        self.assertTrue(f.exists(self.existing_filename))
        self.assertFalse(f.exists(self.non_existing_filename))

    def testCallingInstanceMethod(self):
        '''Call instance method.'''
        f1 = QFile(self.non_existing_filename)
        self.assertFalse(f1.exists())
        f2 = QFile(self.existing_filename)
        self.assertTrue(f2.exists())


if __name__ == '__main__':
    unittest.main()


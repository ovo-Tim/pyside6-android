#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for SimpleFile class'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import SimpleFile

class SimpleFileTest(unittest.TestCase):
    '''Test cases for SimpleFile class.'''

    def setUp(self):
        filename = f'simplefile{os.getpid()}.txt'
        self.existing_filename = Path(os.curdir) / filename
        self.delete_file = False
        if not self.existing_filename.exists():
            with self.existing_filename.open('w') as f:
                for line in range(10):
                    f.write('sbrubbles\n')
            self.delete_file = True

        self.non_existing_filename = Path(os.curdir) / 'inexistingfile.txt'
        i = 0
        while self.non_existing_filename.exists():
            i += 1
            filename = f'inexistingfile-{i}.txt'
            self.non_existing_filename = Path(os.curdir) / filename

    def tearDown(self):
        if self.delete_file:
            os.remove(self.existing_filename)

    def testExistingFile(self):
        '''Test SimpleFile class with existing file.'''
        f = SimpleFile(os.fspath(self.existing_filename))
        self.assertEqual(f.filename(), os.fspath(self.existing_filename))
        f.open()
        self.assertNotEqual(f.size(), 0)
        f.close()

    def testNonExistingFile(self):
        '''Test SimpleFile class with non-existing file.'''
        f = SimpleFile(os.fspath(self.non_existing_filename))
        self.assertEqual(f.filename(), os.fspath(self.non_existing_filename))
        self.assertRaises(IOError, f.open)
        self.assertEqual(f.size(), 0)

if __name__ == '__main__':
    unittest.main()


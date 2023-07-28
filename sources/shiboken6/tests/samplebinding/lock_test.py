#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Simple test with a blocking C++ method that should allow python
   threads to run.'''

import os
import sys
import threading
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import Bucket


class Unlocker(threading.Thread):

    def __init__(self, bucket):
        threading.Thread.__init__(self)
        self.bucket = bucket

    def run(self):
        while not self.bucket.locked():
            pass

        self.bucket.unlock()


class MyBucket(Bucket):

    def __init__(self):
        Bucket.__init__(self)

    def virtualBlockerMethod(self):
        self.lock()
        return True


class TestLockUnlock(unittest.TestCase):

    def testBasic(self):
        '''Locking in C++ and releasing in a python thread'''
        bucket = Bucket()
        unlocker = Unlocker(bucket)

        unlocker.start()
        bucket.lock()
        unlocker.join()

    def testVirtualBlocker(self):
        '''Same as the basic case but blocker method is a C++ virtual called from C++.'''
        bucket = Bucket()
        unlocker = Unlocker(bucket)

        unlocker.start()
        result = bucket.callVirtualBlockerMethodButYouDontKnowThis()
        unlocker.join()
        self.assertTrue(result)

    def testReimplementedVirtualBlocker(self):
        '''Same as the basic case but blocker method is a C++ virtual reimplemented in Python and called from C++.'''
        mybucket = MyBucket()
        unlocker = Unlocker(mybucket)

        unlocker.start()
        result = mybucket.callVirtualBlockerMethodButYouDontKnowThis()
        unlocker.join()
        self.assertTrue(result)

if __name__ == '__main__':
    unittest.main()

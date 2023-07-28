#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Producer-Consumer test/example with QThread'''

import gc
import logging
import os
from random import random
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

logging.basicConfig(level=logging.WARNING)

from PySide6.QtCore import QThread, QCoreApplication, QObject, SIGNAL


class Bucket(QObject):
    '''Dummy class to hold the produced values'''
    def __init__(self, max_size=10, *args):
        # Constructor which receives the max number of produced items
        super().__init__(*args)
        self.data = []
        self.max_size = 10

    def pop(self):
        # Retrieves an item
        return self.data.pop(0)

    def push(self, data):
        # Pushes an item
        self.data.append(data)


class Producer(QThread):
    '''Producer thread'''

    def __init__(self, bucket, *args):
        # Constructor. Receives the bucket
        super().__init__(*args)
        self.runs = 0
        self.bucket = bucket
        self.production_list = []

    def run(self):
        # Produces at most bucket.max_size items
        while self.runs < self.bucket.max_size:
            value = int(random() * 10) % 10
            self.bucket.push(value)
            self.production_list.append(value)
            logging.debug(f'PRODUCER - pushed {value}')
            self.runs += 1
            self.msleep(5)


class Consumer(QThread):
    '''Consumer thread'''
    def __init__(self, bucket, *args):
        # Constructor. Receives the bucket
        super().__init__(*args)
        self.runs = 0
        self.bucket = bucket
        self.consumption_list = []

    def run(self):
        # Consumes at most bucket.max_size items
        while self.runs < self.bucket.max_size:
            try:
                value = self.bucket.pop()
                self.consumption_list.append(value)
                logging.debug(f'CONSUMER - got {value}')
                self.runs += 1
            except IndexError:
                logging.debug('CONSUMER - empty bucket')
            self.msleep(5)


class ProducerConsumer(unittest.TestCase):
    '''Basic test case for producer-consumer QThread'''

    def setUp(self):
        # Create fixtures
        self.app = QCoreApplication([])

    def tearDown(self):
        # Destroy fixtures
        del self.app
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()

    def testProdCon(self):
        # QThread producer-consumer example
        bucket = Bucket()
        prod = Producer(bucket)
        cons = Consumer(bucket)

        cons.finished.connect(QCoreApplication.quit)
        prod.start()
        cons.start()

        self.app.exec()

        self.assertTrue(prod.wait(1000))
        self.assertTrue(cons.wait(1000))

        self.assertEqual(prod.production_list, cons.consumption_list)


if __name__ == '__main__':
    unittest.main()

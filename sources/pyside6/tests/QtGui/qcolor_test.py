# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import colorsys
import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

import PySide6
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QColorConstants


class QColorGetTest(unittest.TestCase):

    def setUp(self):
        self.color = QColor(20, 40, 60, 80)

    def testGetRgb(self):
        self.assertEqual(self.color.getRgb(), (20, 40, 60, 80))

    def testGetHslF(self):
        hls = colorsys.rgb_to_hls(20.0 / 255, 40.0 / 255, 60.0 / 255)
        hsla = hls[0], hls[2], hls[1], self.color.alphaF()
        for x, y in zip(self.color.getHslF(), hsla):  # Due to rounding problems
            self.assertTrue(x - y < 1 / 100000.0)

    def testGetHsv(self):
        hsv = colorsys.rgb_to_hsv(20.0 / 255, 40.0 / 255, 60.0 / 255)
        hsva = int(hsv[0] * 360.0), int(hsv[1] * 255), int(hsv[2] * 256), self.color.alpha()
        self.assertEqual(self.color.getHsv(), hsva)

    def testGetCmyk(self):  # not supported by colorsys
        self.assertEqual(self.color.getCmyk(), (170, 85, 0, 195, 80))

    def testGetCmykF(self):  # not supported by colorsys
        for x, y in zip(self.color.getCmykF(), (170 / 255.0, 85 / 255.0, 0, 195 / 255.0, 80 / 255.0)):
            self.assertTrue(x - y < 1/10000.0)


class QColorQRgbConstructor(unittest.TestCase):
    '''QColor(QRgb) constructor'''
    # Affected by bug #170 - QColor(QVariant) coming before QColor(uint)
    # in overload sorting

    def testBasic(self):
        '''QColor(QRgb)'''
        color = QColor(255, 0, 0)
        # QRgb format #AARRGGBB
        rgb = 0x00FF0000
        self.assertEqual(QColor(rgb), color)


class QColorEqualGlobalColor(unittest.TestCase):

    def testEqualGlobalColor(self):
        '''QColor == Qt::GlobalColor'''
        self.assertEqual(QColor(255, 0, 0), Qt.red)


class QColorCopy(unittest.TestCase):

    def testDeepCopy(self):
        '''QColor deepcopy'''

        from copy import deepcopy

        original = QColor(0, 0, 255)
        copy = deepcopy([original])[0]

        self.assertTrue(original is not copy)
        self.assertEqual(original, copy)
        del original
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(copy, QColor(0, 0, 255))

    def testEmptyCopy(self):
        from copy import deepcopy

        original = QColor()
        copy = deepcopy([original])[0]
        self.assertTrue(original is not copy)
        self.assertEqual(original, copy)
        del original
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(copy, QColor())


class QColorRepr(unittest.TestCase):
    def testReprFunction(self):
        # QColorConstants are disabled for MSVC/5.15, fixme: Check Qt 6
        c = QColorConstants.Yellow if sys.platform != 'win32' else QColor(100, 120, 200)
        c2 = eval(c.__repr__())
        self.assertEqual(c, c2)

    def testStrFunction(self):
        c = QColor('red')
        c2 = eval(c.__str__())
        self.assertEqual(c, c2)


if __name__ == '__main__':
    unittest.main()

# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

from copy import deepcopy
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QPoint
from PySide6.QtGui import QMatrix2x2, QMatrix2x3, QMatrix2x4
from PySide6.QtGui import QMatrix3x2, QMatrix3x3, QMatrix3x4
from PySide6.QtGui import QMatrix4x2, QMatrix4x3, QMatrix4x4
from PySide6.QtGui import QVector2D, QVector3D, QVector4D
from PySide6.QtGui import QColor, QTransform, QKeySequence, QQuaternion
from PySide6.QtGui import QPolygon


class DeepCopyHelper:
    def testCopy(self):
        copy = deepcopy([self.original])[0]
        self.assertTrue(copy is not self.original)
        self.assertEqual(copy, self.original)


class DeepCopyColorHelperF:
    def testCopy(self):
        copy = deepcopy([self.original])[0]
        self.assertTrue(copy is not self.original)
        self.assertEqual(copy.spec(), self.original.spec())
        # impossible to compare float point
        # self.assertEqual(copy, self.original)


class QColorDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QColor("red")


class QColorRGBDeepCopy(DeepCopyColorHelperF, unittest.TestCase):
    def setUp(self):
        self.original = QColor.fromRgbF(0.2, 0.3, 0.4, 0.5)


class QColorHSLDeepCopy(DeepCopyColorHelperF, unittest.TestCase):
    def setUp(self):
        self.original = QColor.fromHslF(0.2, 0.3, 0.4, 0.5)


class QColorHSVDeepCopy(DeepCopyColorHelperF, unittest.TestCase):
    def setUp(self):
        self.original = QColor.fromHsvF(0.2, 0.3, 0.4, 0.5)


class QColorCMYKDeepCopy(DeepCopyColorHelperF, unittest.TestCase):
    def setUp(self):
        self.original = QColor.fromCmykF(0.2, 0.3, 0.4, 0.5, 0.6)


class QTransformDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QTransform(1, 2, 3, 4, 5, 6, 7, 8, 9)


class QKeySequenceDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QKeySequence("Ctrl+P")


class QQuaternionDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QQuaternion(1, 2, 3, 4)


class QVector2DDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QVector2D(1, 2)


class QVector3DDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QVector3D(1, 2, 3)


class QVector4DDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QVector4D(1, 2, 3, 4)


class QPolygonDeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QPolygon([QPoint(1, 2), QPoint(3, 4), QPoint(5, 6)])


# Avoid these tests until get gcc fixed
# Related bug: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=43247
"""
class QMatrix2x2DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix2x2([1, 2, 3, 4])

class QMatrix2x3DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix2x3([1, 2, 3, 4, 5, 6])

class QMatrix2x4DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix2x4([1, 2, 3, 4, 5, 6, 7, 8])

class QMatrix3x2DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix3x2([1, 2, 3, 4, 5, 6])

class QMatrix3x3DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix3x3([1, 2, 3, 4, 5, 6, 7, 8, 9])

class QMatrix3x4DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix3x4([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])

class QMatrix4x2DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix4x2([1, 2, 3, 4, 5, 6, 7, 8])

class QMatrix4x3DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix4x3([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])

class QMatrix4x4DeepCopy(DeepCopyHelper, unittest.TestCase):
    def setUp(self):
        self.original = QMatrix4x4([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16])
"""

if __name__ == '__main__':
    unittest.main()

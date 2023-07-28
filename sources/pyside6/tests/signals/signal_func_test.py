# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import SIGNAL, SLOT


class SIGNALSLOTTests(unittest.TestCase):
    '''Test the output of SIGNAL and SLOT.'''

    def testSIGNAL(self):
        # SIGNAL function
        a = "foobar"
        self.assertEqual(str(SIGNAL(a)), "2foobar")

    def testSLOT(self):
        # SLOT function
        a = "foobar"
        self.assertEqual(str(SLOT(a)), "1foobar")


if __name__ == '__main__':
    unittest.main()

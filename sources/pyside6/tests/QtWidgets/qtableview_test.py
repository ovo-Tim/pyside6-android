# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QAbstractTableModel
from PySide6.QtWidgets import QTableWidget
from helper.usesqapplication import UsesQApplication


class QPenTest(UsesQApplication):

    def testItemModel(self):
        tv = QTableWidget()

        self.assertEqual(type(tv.model()), QAbstractTableModel)


if __name__ == '__main__':
    unittest.main()


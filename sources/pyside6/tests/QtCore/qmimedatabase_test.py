#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Unit tests for QMimeDatabase'''

import ctypes
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QMimeDatabase, QLocale


class QMimeDatabaseTest(unittest.TestCase):
    def testMimeTypeForName(self):
        db = QMimeDatabase()

        s0 = db.mimeTypeForName("application/x-zerosize")
        self.assertTrue(s0.isValid())
        self.assertEqual(s0.name(), "application/x-zerosize")
        if "en" in QLocale().name():
            self.assertEqual(s0.comment(), "empty document")

        s0Again = db.mimeTypeForName("application/x-zerosize")
        self.assertEqual(s0Again.name(), s0.name())

        s1 = db.mimeTypeForName("text/plain")
        self.assertTrue(s1.isValid())
        self.assertEqual(s1.name(), "text/plain")

        krita = db.mimeTypeForName("application/x-krita")
        self.assertTrue(krita.isValid())

        rdf = db.mimeTypeForName("application/rdf+xml")
        self.assertTrue(rdf.isValid())
        self.assertEqual(rdf.name(), "application/rdf+xml")
        if "en" in QLocale().name():
            self.assertEqual(rdf.comment(), "RDF file")

        bzip2 = db.mimeTypeForName("application/x-bzip2")
        self.assertTrue(bzip2.isValid())
        if "en" in QLocale().name():
            self.assertEqual(bzip2.comment(), "Bzip archive")

        defaultMime = db.mimeTypeForName("application/octet-stream")
        self.assertTrue(defaultMime.isValid())
        self.assertEqual(defaultMime.name(), "application/octet-stream")
        self.assertTrue(defaultMime.isDefault())

        doesNotExist = db.mimeTypeForName("foobar/x-doesnot-exist")
        self.assertTrue(not doesNotExist.isValid())


if __name__ == '__main__':
    unittest.main()

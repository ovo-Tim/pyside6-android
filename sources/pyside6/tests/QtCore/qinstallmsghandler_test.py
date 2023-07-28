# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for qInstallMsgHandler'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import (QLibraryInfo, QtMsgType,
                            QMessageLogContext,
                            qCritical, qFormatLogMessage, qDebug,
                            qInstallMessageHandler, qWarning)


param = []


def handler(msgt, ctx, msg):
    global param
    param = [msgt, ctx, msg.strip()]


def handleruseless(msgt, ctx, msg):
    pass


class QInstallMsgHandlerTest(unittest.TestCase):

    def tearDown(self):
        # Ensure that next test will have a clear environment
        qInstallMessageHandler(None)

    def testNone(self):
        ret = qInstallMessageHandler(None)
        self.assertEqual(ret, None)

    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testRet(self):
        ret = qInstallMessageHandler(None)
        self.assertEqual(ret, None)
        refcount = sys.getrefcount(handleruseless)
        retNone = qInstallMessageHandler(handleruseless)
        self.assertEqual(sys.getrefcount(handleruseless), refcount + 1)
        rethandler = qInstallMessageHandler(None)
        self.assertEqual(rethandler, handleruseless)
        del rethandler
        self.assertEqual(sys.getrefcount(handleruseless), refcount)

    def testHandler(self):
        rethandler = qInstallMessageHandler(handler)
        if QLibraryInfo.isDebugBuild():
            qDebug("Test Debug")
            self.assertEqual(param[0], QtMsgType.QtDebugMsg)
            self.assertEqual(param[2], "Test Debug")
        qWarning("Test Warning")
        self.assertEqual(param[0], QtMsgType.QtWarningMsg)
        self.assertEqual(param[2], "Test Warning")
        qCritical("Test Critical")
        self.assertEqual(param[0], QtMsgType.QtCriticalMsg)
        self.assertEqual(param[2], "Test Critical")

    def testFormat(self):
        ctx = QMessageLogContext()
        s = qFormatLogMessage(QtMsgType.QtInfoMsg, ctx, 'bla')
        self.assertTrue(s)


if __name__ == '__main__':
    unittest.main()


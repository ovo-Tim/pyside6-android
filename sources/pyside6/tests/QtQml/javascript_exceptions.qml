// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick 2.0
import QtQuick.Controls 2.0
import JavaScriptExceptions 1.0

Rectangle {
    JavaScriptExceptions {
        id: obj
    }

    Component.onCompleted: {
        // Method call test
        try {
            obj.methodThrows();
        } catch(e) {
            obj.passTest(1);
        }

        // Property accessor test
        try {
            obj.propertyThrows;
        } catch(e) {
            obj.passTest(2);
        }
    }
}

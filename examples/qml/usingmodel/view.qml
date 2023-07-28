// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import PersonModel

ListView {
    width: 100
    height: 100
    anchors.fill: parent
    model: MyModel
    delegate: Component {
        Rectangle {
            height: 25
            width: 100
            Text {
                function displayText() {
                    var result = ""
                    if (typeof display !== "undefined")
                        result = display + ": "
                    result += modelData
                    return result
                }

                text: displayText()
            }
        }
    }
}

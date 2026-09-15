// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
import QtQuick
import Muse.UiComponents

FlatButton {
    id: root

    property bool useMeloIcon: false

    // Engraving fonts position heads around a musical baseline, rather than
    // a text line's centre. Keep their outlines and centre the measured ink.
    contentItem: useMeloIcon ? meloIconContent : null

    Component {
        id: meloIconContent

        Item {
            width: root.width
            height: root.height

            FontMetrics {
                id: metrics
                font: root.iconFont
            }

            Text {
                id: glyph
                objectName: "meloAccidentalGlyph"
                readonly property rect ink: {
                    // FontMetrics methods do not expose a binding dependency.
                    // Read font so a changed toolbar font remeasures the ink.
                    const measuredFont = metrics.font
                    return metrics.tightBoundingRect(text)
                }
                text: String.fromCharCode(root.icon)
                font: root.iconFont
                color: root.iconColor
                x: (parent.width - ink.width) / 2 - ink.x
                y: (parent.height - ink.height) / 2 - ink.y - baselineOffset
            }
        }
    }
}

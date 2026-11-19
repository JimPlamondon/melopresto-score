/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 * Copyright (C) 2026 Jim Plamondon
 */
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Muse.Ui
import Muse.UiComponents
import MuseScore.Inspector

Column {
    id: root
    required property MeloTuningModel model
    required property NavigationPanel navigationPanel
    property bool showFigure: true
    spacing: 8
    width: 370

    StyledTextLabel {
        width: parent.width
        text: qsTrc("notation", "Tuning — M5 (major fifth)")
        horizontalAlignment: Text.AlignLeft
    }
    TextInputField {
        id: centsInput
        property bool escaping: false
        onEscaped: { escaping = true; root.model.cancel(); inputField.text = Qt.binding(function() { return centsInput.currentText }); Qt.callLater(function() { centsInput.escaping = false }) }
        width: parent.width
        currentText: root.model.cents.toLocaleString(Qt.locale(), 'f', 3)
        measureUnitsSymbol: qsTrc("notation", "cents")
        navigation.panel: root.navigationPanel
        navigation.row: 1
        navigation.accessible.name: qsTrc("notation", "Tuning in cents; 1200 cents is a 2:1 frequency ratio")
        onTextEditingFinished: function(value) { if (!escaping) root.model.acceptText(value) }
    }
    StyledTextLabel {
        width: parent.width
        text: qsTrc("notation", "Range: %1 to %2 cents").arg(root.model.minimum.toFixed(3)).arg(root.model.maximum.toFixed(3))
        horizontalAlignment: Text.AlignLeft
    }
    Item {
        id: figure
        visible: root.showFigure
        width: parent.width
        height: visible ? Math.min(520, Math.max(220, Screen.height - 300)) : 0
        readonly property real factor: height / 729
        readonly property real split: 173 * factor
        readonly property real slot: 28
        readonly property real header: 100 * factor
        NavigationPanel {
            id: sliderPanel
            name: "TuningContinuum"
            section: root.navigationPanel.section
            order: root.navigationPanel.order + 1
            direction: NavigationPanel.Vertical
            enabled: figure.visible && root.visible
            accessible.name: qsTrc("notation", "Tuning continuum")
        }
        // Preserve the owner's figure, split immediately before its integer cent stack.
        Item {
            width: figure.split
            y: figure.header
            height: figure.height - y
            clip: true
            Image {
                y: -figure.header
                width: 461 * figure.factor
                height: figure.height
                source: "qrc:/melo/internal/melo/rank2_temperaments_fifth_octave.jpg"
                layer.enabled: true
                layer.effect: ShaderEffect {
                    property var source
                    property color foreground: ui.theme.fontPrimaryColor
                    property color background: ui.theme.popupBackgroundColor
                    fragmentShader: "qrc:/qt/qml/MuseScore/Inspector/shaders/melofigure.frag.qsb"
                }
            }
        }
        Item {
            x: figure.split + figure.slot
            y: figure.header
            width: 288 * figure.factor
            height: figure.height - y
            clip: true
            Image {
                x: -figure.split
                y: -figure.header
                width: 461 * figure.factor
                height: figure.height
                source: "qrc:/melo/internal/melo/rank2_temperaments_fifth_octave.jpg"
                layer.enabled: true
                layer.effect: ShaderEffect {
                    property var source
                    property color foreground: ui.theme.fontPrimaryColor
                    property color background: ui.theme.popupBackgroundColor
                    fragmentShader: "qrc:/qt/qml/MuseScore/Inspector/shaders/melofigure.frag.qsb"
                }
            }
        }
        Item {
            width: parent.width
            height: figure.header
            clip: true
            Image {
                x: figure.slot / 2
                width: 461 * figure.factor
                height: figure.height
                source: "qrc:/melo/internal/melo/rank2_temperaments_fifth_octave.jpg"
                layer.enabled: true
                layer.effect: ShaderEffect {
                    property var source
                    property color foreground: ui.theme.fontPrimaryColor
                    property color background: ui.theme.popupBackgroundColor
                    fragmentShader: "qrc:/qt/qml/MuseScore/Inspector/shaders/melofigure.frag.qsb"
                }
            }
        }
        StyledSlider {
            id: slider
            objectName: "meloTuningSlider"
            x: figure.split
            y: (121.5 + (720 - root.model.maximum) * 16) * figure.factor - 7
            width: figure.slot
            height: (root.model.maximum - root.model.minimum) * 16 * figure.factor + 14
            orientation: Qt.Vertical
            from: root.model.minimum
            to: root.model.maximum
            stepSize: 0.1
            value: root.model.cents
            navigation.panel: sliderPanel
            navigation.row: 2
            navigation.accessible.name: qsTrc("notation", "Tuning in cents")
            wheelEnabled: true
            activeFocusOnTab: true
            Connections {
                target: slider.navigation
                function onActiveChanged() {
                    if (slider.navigation.active) slider.forceActiveFocus()
                }
            }
            Keys.onShortcutOverride: function(event) {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) event.accepted = true
            }
            onPressedChanged: {
                if (pressed) root.model.beginPreview()
                else root.model.commit(value)
            }
            onMoved: {
                if (pressed) root.model.preview(value)
                else root.model.commit(value)
            }
            Keys.onEscapePressed: root.model.cancel()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    root.model.commit(Math.max(from, Math.min(to, value + (event.key === Qt.Key_PageUp ? 1 : -1))))
                    event.accepted = true
                }
            }
            background: Rectangle {
                x: slider.width / 2 - 2
                y: 7
                width: 4
                height: slider.height - 14
                color: ui.theme.fontPrimaryColor
            }
            handle: Rectangle {
                width: 14
                height: 14
                x: slider.width / 2 - width / 2
                y: slider.visualPosition * (slider.height - height)
                radius: 7
                color: ui.theme.accentColor
                border.color: ui.theme.fontPrimaryColor
                border.width: slider.navigation.active ? 2 : 1
            }
        }
    }
    StyledTextLabel {
        visible: root.showFigure
        width: parent.width
        text: qsTrc("notation", "Figure: Andrew Milne & Jim Plamondon, CC BY-SA 4.0")
        font: ui.theme.bodyFont
        wrapMode: Text.WordWrap
    }
    StyledTextLabel {
        id: errorLabel
        width: parent.width
        visible: root.model.error.length > 0
        text: root.model.error.length > 0 ? "⚠ " + root.model.error : ""
        color: root.model.criticalColor
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignLeft
        Loader {
            active: errorLabel.visible && errorLabel.enabled
            sourceComponent: Component {
                AccessibleItem {
                    accessibleParent: root.navigationPanel.accessible
                    visualItem: errorLabel
                    role: MUAccessible.Information
                    name: errorLabel.text
                }
            }
        }
    }
}

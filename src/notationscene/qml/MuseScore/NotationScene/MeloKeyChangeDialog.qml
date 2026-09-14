// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

StyledDialogView {
    id: root
    objectName: "meloKeyChangeDialog"
    property int staffIndex: -1
    property int tickNumerator: 0
    property int tickDenominator: 1
    property string expectedState: ""
    property string expectedTimeline: ""
    contentWidth: 460
    contentHeight: 540
    margins: 20
    title: qsTrc("notation", "Relative key change")
    MeloKeyChangeModel { id: keyModel; onCancelled: root.reject() }
    NavigationPanel { id: keyNavigation; name: "RelativeKeyChange"; section: root.navigationSection; order: 1 }
    Component.onCompleted: {
        keyModel.load(staffIndex, tickNumerator, tickDenominator, expectedState, expectedTimeline)
        intervalInput.currentText = keyModel.expression
        Qt.callLater(function() { intervalInput.ensureActiveFocus() })
    }
    function applyChange() {
        previewDelay.stop()
        if (keyModel.commit(intervalInput.inputField.text)) { root.hide() }
        else { Qt.callLater(function() { intervalInput.ensureActiveFocus() }) }
    }
    Timer { id: previewDelay; interval: 250; onTriggered: keyModel.preview(intervalInput.inputField.text) }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("notation", "Change the key at the selected position for the whole piece. Enter the interval from the preceding key to the new key. Pitch annotations remain read-only.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        StyledTextLabel { text: qsTrc("notation", "Relative interval") }
        TextInputField {
            id: intervalInput
            objectName: "meloRelativeIntervalInput"
            Layout.fillWidth: true
            enabled: keyModel.active
            navigation.panel: keyNavigation
            navigation.order: 1
            accessible.name: qsTrc("notation", "Relative key-change interval")
            onTextChanged: previewDelay.restart()
            onAccepted: root.applyChange()
            onEscaped: keyModel.cancel()
        }
        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("notation", "For example: M6 moves new Do to the preceding La above it; -M6 moves down. P8 + M6 adds an octave. Integer multiples, such as 7*M5 - 4*P8, preserve other intervals.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        StyledTextLabel {
            Layout.fillWidth: true
            text: keyModel.error || keyModel.description
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        MeloKeyChangePreview {
            objectName: "meloKeyChangePreview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 140
            model: keyModel
        }
        StyledTextLabel { text: qsTrc("notation", "Pitch labels use Middle C = C4.") }
        FlatButton {
            text: qsTrc("notation", "Remove relative key change")
            enabled: keyModel.active && keyModel.existing
            navigation.panel: keyNavigation
            navigation.order: 2
            onClicked: { previewDelay.stop(); if (keyModel.commit("P1")) { root.hide() } }
        }
        ButtonBox {
            Layout.fillWidth: true
            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2
            buttons: [ButtonBoxModel.Cancel, ButtonBoxModel.Ok]
            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) { previewDelay.stop(); keyModel.cancel() }
                else if (buttonId === ButtonBoxModel.Ok) { root.applyChange() }
            }
        }
    }
}

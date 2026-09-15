// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

StyledDialogView {
    id: root
    objectName: "meloInitialPitchDialog"
    property int staffIndex: -1
    property int tickNumerator: 0
    property int tickDenominator: 1
    property int periodIndex: 0
    property string expectedState: ""
    property string expectedTimeline: ""
    contentWidth: 440
    contentHeight: 270
    margins: 20
    title: qsTrc("notation", "Initial tonic pitch")

    MeloInitialPitchModel { id: model; onCancelled: root.reject() }
    NavigationPanel {
        id: pitchNavigation
        name: "InitialTonicPitch"
        section: root.navigationSection
        order: 1
    }
    Component.onCompleted: {
        model.load(staffIndex, tickNumerator, tickDenominator, periodIndex, expectedState, expectedTimeline)
        input.currentText = model.pitch
        Qt.callLater(function() { input.ensureActiveFocus() })
    }
    function applyPitch() {
        if (model.commit(input.inputField.text)) { root.hide() }
        else { Qt.callLater(function() { input.ensureActiveFocus() }) }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("notation", "Change the initial key for the whole piece. All staves and later systems update; relative key changes stay the same.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        StyledTextLabel {
            text: qsTrc("notation", "Tonic pitch — Middle C = C4")
            horizontalAlignment: Text.AlignLeft
        }
        TextInputField {
            id: input
            objectName: "meloInitialPitchInput"
            Layout.fillWidth: true
            enabled: model.active
            navigation.panel: pitchNavigation
            navigation.order: 1
            accessible.name: qsTrc("notation", "Initial tonic pitch for the whole piece. Middle C = C4")
            onAccepted: root.applyPitch()
            onEscaped: model.cancel()
        }
        StyledTextLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: model.error
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        ButtonBox {
            Layout.fillWidth: true
            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2
            buttons: [ButtonBoxModel.Cancel, ButtonBoxModel.Ok]
            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) { model.cancel() }
                else if (buttonId === ButtonBoxModel.Ok) { root.applyPitch() }
            }
        }
    }
}

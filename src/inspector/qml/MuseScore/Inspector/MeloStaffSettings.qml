// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
pragma ComponentBehavior: Bound
import QtQuick
import Muse.Ui
import Muse.UiComponents
import MuseScore.Inspector
import "common"
InspectorSectionView {
    id: root
    required property MeloStaffSettingsModel model
    implicitHeight: controls.implicitHeight
    Column {
        id: controls
        width: parent.width
        spacing: 10
        StyledTextLabel {
            width: parent.width
            text: root.model.settings.target ?? qsTrc("inspector", "Select a note or measure on a compatible staff.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        StyledTextLabel {
            width: parent.width
            text: qsTrc("inspector", "Mode, key, and scale changes apply to all compatible parts at this position.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        StyledTextLabel {
            width: parent.width
            visible: text.length > 0
            text: root.model.settings.indicator ?? ""
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }
        Repeater {
            model: [
                { key: "tonics", label: qsTrc("inspector", "Tonic") },
                { key: "scales", label: qsTrc("inspector", "Scale") }
            ]
            Column {
                required property var modelData
                required property int index
                width: controls.width
                spacing: 4
                StyledTextLabel { text: parent.modelData.label }
                StyledDropdown {
                    width: parent.width
                    model: root.model.settings[parent.modelData.key] ?? []
                    currentIndex: root.model.settings[parent.modelData.key + "Index"] ?? -1
                    enabled: !!root.model.settings.canChange
                    navigation.panel: root.navigationPanel
                    navigation.row: root.navigationRow(parent.index + 1)
                    navigation.name: parent.modelData.key
                    navigation.accessible.name: parent.modelData.label
                    onActivated: function(index, value) { root.model.applyOption(parent.modelData.key, value) }
                }
            }
        }
        FlatButton {
            width: parent.width
            text: qsTrc("inspector", "Add or edit relative key change…")
            enabled: !!root.model.settings.canKeyChange
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow(5)
            onClicked: root.model.editKeyChange()
        }
        FlatButton {
            width: parent.width
            text: qsTrc("inspector", "Remove change from this staff")
            enabled: !!root.model.settings.hasChange
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow(6)
            onClicked: root.model.removeChange()
        }
        StyledTextLabel {
            id: statusLabel
            color: root.model.hasError || (!root.model.status && root.model.settings.reason) ? root.model.criticalColor : ui.theme.fontPrimaryColor
            width: parent.width
            visible: text.length > 0
            text: (root.model.hasError || (!root.model.status && root.model.settings.reason) ? "⚠ " : "") + (root.model.status || root.model.settings.reason || "")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            Loader {
                active: statusLabel.visible && statusLabel.enabled
                sourceComponent: Component {
                    AccessibleItem {
                        accessibleParent: root.navigationPanel.accessible
                        visualItem: statusLabel
                        role: MUAccessible.Information
                        name: statusLabel.text
                    }
                }
            }
        }
        StyledTextLabel { text: qsTrc("inspector", "Hide empty octave bands (hollow stacks)") }
        StyledDropdown {
            width: parent.width
            model: [ { text: qsTrc("inspector", "Follow score"), value: 0 }, { text: qsTrc("inspector", "On"), value: 1 }, { text: qsTrc("inspector", "Off"), value: 2 } ]
            currentIndex: root.model.settings.elision ?? 0
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow(7)
            navigation.accessible.name: qsTrc("inspector", "Hide empty octave bands on this staff")
            onActivated: function(index, value) { root.model.setStaffOption("elision", value) }
        }
        FlatButton {
            id: more
            width: parent.width
            property bool expanded: false
            text: expanded ? qsTrc("inspector", "Show less") : qsTrc("inspector", "Show more")
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow(8)
            onClicked: expanded = !expanded
        }
        StyledTextLabel { visible: more.expanded; text: qsTrc("inspector", "Scale-dot labels") }
        StyledDropdown {
            width: parent.width
            visible: more.expanded
            model: [ { text: qsTrc("inspector", "Automatic"), value: 0 }, { text: qsTrc("inspector", "None"), value: 1 }, { text: qsTrc("inspector", "Left"), value: 2 }, { text: qsTrc("inspector", "Split"), value: 3 } ]
            currentIndex: root.model.settings.labels ?? 0
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow(9)
            navigation.accessible.name: qsTrc("inspector", "Scale-dot labels")
            onActivated: function(index, value) { root.model.setStaffOption("labels", value) }
        }
    }
}

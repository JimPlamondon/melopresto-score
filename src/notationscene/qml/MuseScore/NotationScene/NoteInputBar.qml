/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene
import MuseScore.Inspector

Item {
    id: root

    property alias orientation: gridView.orientation

    property bool floating: false

    property int maximumWidth: 0
    property int maximumHeight: 0

    width: gridView.isHorizontal ? childrenRect.width : 76
    height: !gridView.isHorizontal ? childrenRect.height : 40

    property NavigationPanel navigationPanel: NavigationPanel {
        name: "NoteInputBar"
        enabled: root.enabled && root.visible
        accessible.name: qsTrc("notation", "Note input toolbar")
    }

    MeloTuningModel {
        id: meloTuning
        Component.onCompleted: init()
    }

    FlatButton {
        id: tuningButton
        visible: meloTuning.available && noteInputModel.showMeloTuning
        width: gridView.isHorizontal ? 126 : 76
        height: 32
        text: "M5= " + meloTuning.cents.toLocaleString(Qt.locale(), 'f', 1) + "¢"
        transparent: true
        toolTipTitle: qsTrc("notation", "Tuning — M5 (major fifth)")
        toolTipDescription: qsTrc("notation", "Change tuning in cents. The score updates as you move the slider.")
        navigation.panel: root.navigationPanel
        navigation.order: 101
        navigation.accessible.name: toolTipTitle + " " + text
        onClicked: tuningPopup.toggleOpened()
        StyledPopupView {
            id: tuningPopup
            contentWidth: 370
            contentHeight: tuningControl.implicitHeight
            onClosed: meloTuning.cancel()
            MeloTuningControl {
                id: tuningControl
                model: meloTuning
                navigationPanel: NavigationPanel { name: "TuningPopup"; section: tuningPopup.navigationSection; order: 1; direction: NavigationPanel.Vertical }
            }
        }
    }

    NoteInputBarModel {
        id: noteInputModel
    }

    QtObject {
        id: prv

        function resolveHorizontalGridViewWidth() {
            if (root.floating) {
                return gridView.contentWidth
            }

            var requiredFreeSpace = gridView.cellWidth * 3 + gridView.rowSpacing * 4

            var available = root.maximumWidth - requiredFreeSpace - (tuningButton.visible ? tuningButton.width : 0)
            return Math.max(gridView.cellWidth, Math.min(gridView.contentWidth, available))
        }

        function resolveVerticalGridViewHeight() {
            if (root.floating) {
                return gridView.contentHeight
            }

            var requiredFreeSpace = gridView.cellHeight * 3 + gridView.rowSpacing * 4

            var available = root.maximumHeight - requiredFreeSpace - (tuningButton.visible ? tuningButton.height : 0)
            return Math.max(gridView.cellHeight, Math.min(gridView.contentHeight, available))
        }
    }

    GridViewSectional {
        id: gridView

        sectionRole: "section"

        rowSpacing: 4
        columnSpacing: 4

        cellWidth: 32
        cellHeight: cellWidth

        sectionWidth: isHorizontal ? 1 : width
        sectionHeight: isHorizontal ? height : 1

        clip: true

        model: noteInputModel

        sectionDelegate: SeparatorLine {
            required property int itemIndex

            orientation: gridView.isHorizontal ? Qt.Vertical : Qt.Horizontal
            visible: itemIndex !== 0
        }

        itemDelegate: MeloAccidentalButton {
            id: btn

            required property var itemModel

            readonly property MenuItem item: Boolean(itemModel) ? itemModel.item : null
            readonly property bool hasMenu: Boolean(item) && item.subitems.length !== 0
            readonly property var meloAccidental: Boolean(item)
                ? noteInputModel.accidentalPresentation[item.code] : null

            useMeloIcon: Boolean(meloAccidental)

            width: gridView.cellWidth
            height: gridView.cellWidth

            enabled: noteInputModel.isInputAllowed

            accentButton: (Boolean(item) && item.checked) || menuLoader.isMenuOpened
            transparent: !accentButton

            icon: meloAccidental ? meloAccidental.icon : (Boolean(item) ? item.icon : IconCode.NONE)
            iconFont: meloAccidental ? noteInputModel.accidentalPresentation.font : ui.theme.toolbarIconsFont

            toolTipTitle: meloAccidental ? meloAccidental.title : (Boolean(item) ? item.title : "")
            toolTipDescription: Boolean(item) ? item.description : ""
            toolTipShortcut: Boolean(item) ? item.shortcuts : ""

            navigation.panel: root.navigationPanel
            navigation.name: Boolean(item) ? item.id : ""
            navigation.order: Boolean(itemModel) ? itemModel.order : 0
            isClickOnKeyNavTriggered: false
            navigation.onTriggered: {
                if (btn.hasMenu) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            function toggleMenuOpened() {
                menuLoader.toggleOpened(item.subitems)
            }

            function handleMenuItem() {
                Qt.callLater(noteInputModel.handleMenuItem, item.id)
            }

            onClicked: {
                if (btn.hasMenu) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            mouseArea.onPressAndHold: function(event) {
                if (menuLoader.isMenuOpened || !btn.hasMenu) {
                    event.accepted = false // do not suppress the click event
                    return
                }

                btn.toggleMenuOpened()
            }

            StyledMenuLoader {
                id: menuLoader

                onHandleMenuItem: function(itemId) {
                    noteInputModel.handleMenuItem(itemId)
                }
            }
        }
    }

    FlatButton {
        id: customizeButton

        anchors.margins: 4

        width: gridView.cellWidth
        height: gridView.cellHeight

        icon: IconCode.SETTINGS_COG
        iconFont: ui.theme.toolbarIconsFont
        toolTipTitle: qsTrc("notation", "Customize toolbar")
        toolTipDescription: qsTrc("notation", "Show/hide toolbar buttons")
        transparent: true

        enabled: noteInputModel.isInputAllowed

        navigation.panel: root.navigationPanel
        navigation.order: 100
        navigation.accessible.name: qsTrc("notation", "Customize toolbar")

        onClicked: {
            customizePopup.toggleOpened()
        }

        NoteInputBarCustomisePopup {
            id: customizePopup

            anchorItem: !root.floating ? ui.rootItem : null
        }
    }

    states: [
        State {
            when: gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: prv.resolveHorizontalGridViewWidth()
                height: root.height
                sectionWidth: 1
                sectionHeight: root.height
                rows: 1
                columns: gridView.noLimit
            }

            AnchorChanges {
                target: tuningButton
                anchors.left: customizeButton.right
                anchors.verticalCenter: root.verticalCenter
            }

            AnchorChanges {
                target: customizeButton
                anchors.left: gridView.right
                anchors.verticalCenter: root.verticalCenter
            }
        },
        State {
            when: !gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: root.width
                height: prv.resolveVerticalGridViewHeight()
                sectionWidth: root.width
                sectionHeight: 1
                rows: gridView.noLimit
                columns: 2
            }

            AnchorChanges {
                target: tuningButton
                anchors.top: customizeButton.bottom
                anchors.horizontalCenter: root.horizontalCenter
            }

            AnchorChanges {
                target: customizeButton
                anchors.top: gridView.bottom
                anchors.right: parent.right
            }
        }
    ]
}

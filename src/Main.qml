import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 700
    minimumHeight: 460
    visible: true
    title: qsTr("Ping Monitor")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property string selected: ""
    readonly property string state: backend.serverState
    readonly property bool dialogOpen: addPopup.opened || helpVisible

    function stateText() {
        switch (state) {
        case "starting": return qsTr("Starting server…")
        case "running": return qsTr("Server running on port %1").arg(backend.port)
        case "external": return backend.reachable ? qsTr("Connected to server on port %1").arg(backend.port)
                                                  : qsTr("Server on port %1 is not answering").arg(backend.port)
        case "error": return qsTr("Server stopped")
        default: return qsTr("Server stopped")
        }
    }
    function stateColor() {
        if (state === "error" || ((state === "running" || state === "external") && !backend.reachable))
            return "#f87171"
        return state === "running" || state === "external" ? "#4ade80" : "#8b8b93"
    }
    function errorText() {
        switch (backend.errorKind) {
        case "python": return qsTr("Python 3 is needed to run the server, and it was not found.")
        case "ping": return qsTr("The ping command is needed to run the server, and it was not found.")
        case "script": return qsTr("The bundled server is missing: %1").arg(backend.errorDetail)
        case "exited": return qsTr("The server exited: %1").arg(backend.errorDetail)
        default: return ""
        }
    }
    function addError(kind) {
        switch (kind) {
        case "empty": return qsTr("Type a hostname or IP address.")
        case "invalid": return qsTr("That is not a valid hostname or IP address.")
        case "duplicate": return qsTr("Already monitoring that host.")
        default: return ""
        }
    }
    function move(step) {
        const count = backend.hosts.count
        if (count === 0)
            return
        const current = backend.hosts.indexOf(selected)
        const next = current < 0 ? 0 : Math.max(0, Math.min(count - 1, current + step))
        selected = backend.hosts.nameAt(next)
        list.positionViewAtIndex(next, ListView.Contain)
    }
    function removeSelected() {
        if (selected === "")
            return
        // Pick the neighbor first, so repeated deletes keep working.
        const index = backend.hosts.indexOf(selected)
        const neighbor = backend.hosts.nameAt(index + 1) || backend.hosts.nameAt(index - 1)
        backend.removeHost(selected)
        selected = neighbor
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.toggleServer() }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: addPopup.open() }
    Shortcut { sequences: ["Delete", "X"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.removeSelected() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.move(-1) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: qsTr("Ping Monitor"); font.pixelSize: 22; font.bold: true }
                Label {
                    text: qsTr("%n hosts", "", backend.hosts.count)
                    color: "#8b8b93"
                    Layout.alignment: Qt.AlignBottom
                }
                Item { Layout.fillWidth: true }
                Rectangle { width: 10; height: 10; radius: 5; color: win.stateColor() }
                Label { text: win.stateText(); color: "#c8c8cf" }
            }

            Rectangle {
                visible: backend.serverState === "error"
                Layout.fillWidth: true
                implicitHeight: banner.implicitHeight + 24
                radius: theme.radius
                color: "#2a1517"
                border.color: "#f87171"
                Label {
                    id: banner
                    anchors.fill: parent
                    anchors.margins: 12
                    text: win.errorText() + "  " + qsTr("Press Space to try again.")
                    wrapMode: Text.WordWrap
                    color: "#fecaca"
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 10
                model: backend.hosts
                ScrollBar.vertical: ScrollBar {}
                delegate: HostCard {
                    width: ListView.view.width - (ListView.view.ScrollBar.vertical.visible ? 12 : 0)
                    selected: name === win.selected
                    stale: backend.serverState !== "idle" && !backend.reachable && backend.serverState !== "starting"
                    onClicked: win.selected = name
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    color: "#8b8b93"
                    text: qsTr("No hosts yet.") + "\n" + qsTr("Press A to add one.")
                }
            }

            Label {
                text: qsTr("A add host  ·  Del remove  ·  Space start/stop server  ·  ? help")
                color: "#6b6b73"
                font.pixelSize: 12
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    Popup {
        id: addPopup
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(420, win.width - 40)
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property string error: ""

        function submit() {
            error = win.addError(backend.addHost(field.text))
            if (error === "") {
                win.selected = field.text.trim()
                close()
            }
        }
        onOpened: { field.text = ""; error = ""; field.forceActiveFocus() }

        background: Rectangle { radius: theme.radius; color: "#17171a"; border.color: "#26262b" }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Label { text: qsTr("Add host"); font.pixelSize: 18; font.bold: true }
            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: qsTr("Hostname or IP address")
                onAccepted: addPopup.submit()
            }
            Label {
                visible: addPopup.error !== ""
                text: addPopup.error
                color: "#f87171"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("Cancel"); flat: true; onClicked: addPopup.close() }
                Button { text: qsTr("Add"); highlighted: true; onClicked: addPopup.submit() }
            }
        }
    }

    Popup {
        id: helpPopup
        visible: win.helpVisible
        anchors.centerIn: parent
        modal: true
        padding: 24
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: win.helpVisible = false
        background: Rectangle { radius: theme.radius; color: "#17171a"; border.color: "#26262b" }

        ColumnLayout {
            spacing: 12
            Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 18; font.bold: true }
            GridLayout {
                columns: 2
                columnSpacing: 28
                rowSpacing: 6
                Repeater {
                    model: [
                        ["A", qsTr("Add a host")],
                        ["Delete / X", qsTr("Remove the selected host")],
                        ["↑ ↓ / K J", qsTr("Select a host")],
                        ["Space", qsTr("Start or stop the server")],
                        ["?", qsTr("Show or hide this help")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.columnSpan: 2
                        spacing: 28
                        Label { text: modelData[0]; color: theme.accent; font.bold: true; Layout.preferredWidth: 110 }
                        Label { text: modelData[1] }
                    }
                }
            }
        }
    }
}

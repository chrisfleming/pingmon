import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: card
    required property string name
    required property string status
    required property string resolvedIp
    required property real last
    required property real average
    required property real minimum
    required property real maximum
    required property real loss
    required property int sent
    required property int failures
    required property var history
    property bool selected: false
    property bool stale: false

    signal clicked()

    implicitHeight: 112
    radius: theme.radius
    color: selected ? "#1f1f24" : "#17171a"
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accent : "#26262b"
    opacity: stale ? 0.6 : 1

    function statusColor() {
        return status === "up" ? "#4ade80" : status === "down" ? "#f87171" : "#8b8b93"
    }
    function statusText() {
        return status === "up" ? qsTr("UP") : status === "down" ? qsTr("DOWN") : qsTr("Waiting")
    }
    // Reading translator.language makes the numbers re-render on a language switch.
    function ms(value) {
        const language = translator.language
        return value < 0 ? "—" : value.toLocaleString(Qt.locale(), 'f', 1)
    }
    function percent(value) {
        const language = translator.language
        return value.toLocaleString(Qt.locale(), 'f', 1) + "%"
    }
    function latencyColor(value) {
        return value < 0 ? "#8b8b93" : value < backend.goodMs ? "#4ade80" : value < backend.warnMs ? "#facc15" : "#f87171"
    }

    MouseArea { anchors.fill: parent; onClicked: card.clicked() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle { width: 12; height: 12; radius: 6; color: card.statusColor() }

            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.maximumWidth: 190
                spacing: 0
                Label {
                    text: card.name
                    font.pixelSize: 16
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: card.status === "down" && card.failures > 0
                          ? qsTr("%n missed in a row", "", card.failures)
                          : card.resolvedIp !== "" ? card.resolvedIp : card.statusText()
                    color: card.status === "down" ? "#f87171" : "#8b8b93"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { label: qsTr("Last"), value: card.ms(card.last), color: card.latencyColor(card.last) },
                    { label: qsTr("Avg"), value: card.ms(card.average), color: card.latencyColor(card.average) },
                    { label: qsTr("Min"), value: card.ms(card.minimum), color: "#e8e8ea" },
                    { label: qsTr("Max"), value: card.ms(card.maximum), color: "#e8e8ea" },
                    { label: qsTr("Loss"), value: card.percent(card.loss), color: card.loss > 0 ? "#f87171" : "#e8e8ea" }
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.preferredWidth: 68
                    spacing: 0
                    Label { text: modelData.label; color: "#8b8b93"; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    Label { text: modelData.value; color: modelData.color; font.pixelSize: 15; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }

        Sparkline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            history: card.history
            goodMs: backend.goodMs
            warnMs: backend.warnMs
            dimmed: card.stale
        }
    }
}

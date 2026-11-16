import QtQuick

// One bar per ping, colored by latency, with a red tick for a dropped ping.
Item {
    id: root
    property var history: []
    property int goodMs: 50
    property int warnMs: 150
    property bool dimmed: false

    function colorFor(ms) {
        return ms < goodMs ? "#4ade80" : ms < warnMs ? "#facc15" : "#f87171"
    }

    onHistoryChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onDimmedChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.globalAlpha = root.dimmed ? 0.35 : 1
            const values = root.history
            if (!values || values.length === 0)
                return
            let top = 1
            for (let i = 0; i < values.length; ++i)
                top = Math.max(top, values[i])
            // A fixed slot width keeps bars from stretching while the
            // history is still filling.
            const slots = Math.max(values.length, 60)
            const slot = width / slots
            const bar = Math.max(1, slot - 1)
            for (let i = 0; i < values.length; ++i) {
                const x = width - (values.length - i) * slot
                if (values[i] < 0) {
                    ctx.fillStyle = "#f87171"
                    ctx.globalAlpha = (root.dimmed ? 0.35 : 1) * 0.45
                    ctx.fillRect(x, 0, bar, height)
                    ctx.globalAlpha = root.dimmed ? 0.35 : 1
                } else {
                    const h = Math.max(2, values[i] / top * height)
                    ctx.fillStyle = root.colorFor(values[i])
                    ctx.fillRect(x, height - h, bar, h)
                }
            }
        }
    }
}

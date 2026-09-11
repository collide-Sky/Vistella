// =============================================================================
//  QQuickWidget demo QML (阶段 1 前置 B.2, 2026-09-03)
//
//  目的: 验证 DWrite 修法 (PhaseA.1 5 组合) 下 QQuickWidget 能正常渲染 QML,
//        给阶段 1+ 关键 UI (滤镜 / waveform / timeline) 集成 QML 留模板.
//
//  注意:
//   - 只用 QtQuick 标准组件 (Rectangle / Text / MouseArea), 不依赖 QtQuick.Controls
//   - 阶段 1+ 真用 Controls 时再装, 现阶段保持最简
//   - QQuickWidget 在 QWidget 应用里嵌 QML, 走 Qt Quick RHI
//   - 我们已经 QSG_RHI_BACKEND=software + QT_QUICK_BACKEND=software, 不调 D3D/DWrite
// =============================================================================

import QtQuick

Rectangle {
    id: root
    width: 360
    height: 200
    color: "#1e1e2e"   // 深色背景, 验证 QQuickWidget 透明度
    radius: 8

    // 标题
    Text {
        id: titleText
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 16
        text: "QQuickWidget Demo"
        color: "#cdd6f4"
        font.pixelSize: 18
        font.bold: true
    }

    // 副标题 (DWrite 修法验证)
    Text {
        anchors.top: titleText.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.margins: 16
        text: "DWrite 修法验证 (PhaseA.1 5 组合 + software RHI)"
        color: "#a6adc8"
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // 计数器
    Rectangle {
        id: buttonRect
        width: 80
        height: 32
        radius: 6
        color: buttonArea.containsMouse ? "#89b4fa" : "#b4befe"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16

        Text {
            anchors.centerIn: parent
            text: "测试 +1"
            color: "#1e1e2e"
            font.pixelSize: 12
        }

        MouseArea {
            id: buttonArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: counterText.text = (parseInt(counterText.text) + 1).toString()
        }
    }

    Text {
        id: counterText
        anchors.left: buttonRect.right
        anchors.leftMargin: 12
        anchors.verticalCenter: buttonRect.verticalCenter
        text: "0"
        color: "#f38ba8"
        font.pixelSize: 16
    }

    // 状态显示
    Text {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 16
        text: "状态: " + (root.status === 0 ? "Null" :
                          root.status === 1 ? "Ready" :
                          root.status === 2 ? "Loading" : "Error")
        color: "#a6e3a1"
        font.pixelSize: 10
    }
}

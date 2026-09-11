import QtQuick

// 启动屏: 半透明 + 圆角窗口 (C++ 端 setMask), 中心能量核心 + 3 圈轨道 + 浮动星光
// 5 秒后由 C++ 端关闭并进入主窗口
// 注: QQuickView 加载时根 item 必须是 Item, 不能是 Window
//     圆角窗口外观由 C++ 端 setMask 实现, QML 不画背景
Item {
    id: splash
    width: 600
    height: 400

    // ===========================================================
    // 浮动星光 (背景粒子, 20 颗)
    // 透明背景下颜色要更亮, 才能在浅色桌面上看到
    // ===========================================================
    Repeater {
        model: 20
        Rectangle {
            property real posX: (index * 73 + 17) % 600
            property real posY: (index * 137 + 41) % 400
            property real baseSize: 1.5 + (index % 4) * 0.5
            property real twinkleDur: 1800 + (index % 7) * 350

            x: posX - baseSize / 2
            y: posY - baseSize / 2
            width: baseSize; height: baseSize
            radius: width / 2
            color: "#a8e6c0"
            opacity: 0.35 + (index % 5) * 0.1

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.1;  duration: twinkleDur; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.85; duration: twinkleDur; easing.type: Easing.InOutQuad }
            }
        }
    }

    // ===========================================================
    // 中心内容 (球 + 3 圈轨道)
    // ===========================================================
    Item {
        id: coreArea
        anchors.centerIn: parent
        width: 380
        height: 380

        // ----- 最外层轨道 (半径 170, 慢速逆向) -----
        Item {
            width: parent.width; height: parent.height
            anchors.centerIn: parent
            RotationAnimation on rotation {
                from: 0; to: -360
                duration: 12000
                loops: Animation.Infinite
            }
            Repeater {
                model: 2
                Rectangle {
                    x: parent.width / 2 - 4
                    y: parent.height / 2 - 170 - 4
                    width: 8; height: 8
                    radius: 4
                    color: "#88dfaa"
                    opacity: 0.75
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3;  duration: 2200; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1.0;  duration: 2200; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }

        // ----- 中层轨道 (半径 140, 中速) -----
        Item {
            width: parent.width; height: parent.height
            anchors.centerIn: parent
            RotationAnimation on rotation {
                from: 0; to: 360
                duration: 8000
                loops: Animation.Infinite
            }
            Repeater {
                model: 3
                Rectangle {
                    x: parent.width / 2 - 5
                    y: parent.height / 2 - 140 - 5
                    width: 10; height: 10
                    radius: 5
                    color: "#4caf80"
                    opacity: 0.9
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.4;  duration: 1800; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1.0;  duration: 1800; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }

        // ----- 内层轨道 (半径 110, 快速) -----
        Item {
            width: parent.width; height: parent.height
            anchors.centerIn: parent
            RotationAnimation on rotation {
                from: 0; to: -360
                duration: 5000
                loops: Animation.Infinite
            }
            Repeater {
                model: 2
                Rectangle {
                    x: parent.width / 2 - 4
                    y: parent.height / 2 - 110 - 4
                    width: 8; height: 8
                    radius: 4
                    color: "#a8e6c0"
                    opacity: 0.95
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.5; duration: 1400; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1.0; duration: 1400; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }

        // ----- 球体光晕 (3 层) -----
        Rectangle {
            anchors.centerIn: parent
            width: 220; height: 220
            radius: width / 2
            color: "#88dfaa"
            opacity: 0.10
            scale: ball.scale * 1.35
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.42; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.10; duration: 1000; easing.type: Easing.InOutQuad }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 160; height: 160
            radius: width / 2
            color: "#88dfaa"
            opacity: 0.22
            scale: ball.scale * 1.15
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.65; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.22; duration: 1000; easing.type: Easing.InOutQuad }
            }
        }

        // ----- 能量核心球 (120x120) -----
        Item {
            id: ball
            anchors.centerIn: parent
            width: 120; height: 120

            SequentialAnimation on scale {
                loops: Animation.Infinite
                NumberAnimation { to: 1.0;  duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.75; duration: 1000; easing.type: Easing.InOutQuad }
            }

            RotationAnimation on rotation {
                from: 0; to: 360
                duration: 6000
                loops: Animation.Infinite
            }

            // 外层: 主色
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "#4caf80"
            }
            // 中层: 较亮
            Rectangle {
                anchors.centerIn: parent
                width: 90; height: 90
                radius: width / 2
                color: "#5dc295"
                opacity: 0.55
            }
            // 内层: 中心高光
            Rectangle {
                anchors.centerIn: parent
                width: 50; height: 50
                radius: width / 2
                color: "#88dfaa"
                opacity: 0.7
            }
            // 反射高光点
            Rectangle {
                x: parent.width * 0.3
                y: parent.height * 0.2
                width: 14; height: 14
                radius: 7
                color: "white"
                opacity: 0.9
            }
        }
    }

    // ===========================================================
    // 底部品牌 + 进度条
    // ===========================================================
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 36
        spacing: 8

        Text {
            text: "MultiDoc"
            color: "white"
            font.pixelSize: 22
            font.weight: Font.Bold
            font.letterSpacing: 3
            anchors.horizontalCenter: parent.horizontalCenter

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 1.0; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.75; duration: 1000; easing.type: Easing.InOutQuad }
            }
        }

        Text {
            text: "多模态文档工作台"
            color: "#b8c4d0"
            font.pixelSize: 10
            font.letterSpacing: 1
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // 进度条
        Item {
            id: progressTrack
            width: 200; height: 2
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                anchors.fill: parent
                color: "white"
                opacity: 0.18
                radius: 1
            }
            Rectangle {
                id: progressFill
                height: parent.height
                width: 0
                color: "#4caf80"
                radius: 1

                NumberAnimation on width {
                    from: 0; to: 200
                    duration: 5000
                    easing.type: Easing.OutCubic
                }
                Rectangle {
                    x: progressFill.width - 3
                    y: -1
                    width: 6; height: 4
                    radius: 3
                    color: "white"
                    opacity: 0.8
                }
            }

            Text {
                anchors.top: parent.bottom
                anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#b8c4d0"
                font.pixelSize: 9
                font.family: "Consolas"
                text: Math.round(progressFill.width / 200 * 100) + "%"
            }
        }
    }
}

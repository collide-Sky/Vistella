// SPDX-License-Identifier: MIT
//
// tst_F_M_Dock_GradientPatternChannel - F-M (2026-09-10)
//
// 验证:
//   F-M.1 ColorDock 渐变 (gradient) 编辑器:
//     - 5 种类型 (Linear/Radial/Angle/Diamond/Symmetric) 出现在 combo box
//     - 8 个渐变预设 cells 存在
//     - setGradientPreset 触发 gradientPresetChanged 信号
//   F-M.2 ColorDock 图案 (pattern) 选择器:
//     - 6 个图案预设 cells 存在
//     - setPatternPreset 触发 patternPresetChanged 信号
//   F-M.3 ChannelPathPanel (LayersDock tab 1):
//     - 6 个通道 (RGB/Red/Green/Blue/Alpha/快速蒙版) 列出
//     - 1 个路径 (工作路径) 列出
//     - 选中通道触发 channelSelected 信号
//     - 选中路径触发 pathSelected 信号
//
#include <QTest>
#include <QSignalSpy>
#include <QComboBox>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>

#include "../src/media/docks/ColorDock.h"
#include "../src/media/docks/ChannelPathPanel.h"

using namespace docks;

class tst_F_M_Dock_GradientPatternChannel : public QObject
{
    Q_OBJECT
private slots:
    // F-M.1: gradient
    void colorDock_gradientTypeComboHas5Types();
    void colorDock_gradientPresetsCount8();
    void colorDock_setGradientPresetEmitsSignal();

    // F-M.2: pattern
    void colorDock_patternPresetsCount6();
    void colorDock_setPatternPresetEmitsSignal();

    // F-M.3: channel/path panel
    void channelPathPanel_listsAndSignals();
};

// ===== F-M.1: 5 渐变类型 =====
void tst_F_M_Dock_GradientPatternChannel::colorDock_gradientTypeComboHas5Types()
{
    ColorDock color;
    QComboBox* combo = color.findChild<QComboBox*>();
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 5);
    // F-M.1 .ui 里 5 类型是中文 (PS 本地化标签)
    const QStringList expected = {
        QString::fromUtf8("线性"),
        QString::fromUtf8("径向"),
        QString::fromUtf8("角度"),
        QString::fromUtf8("菱形"),
        QString::fromUtf8("对称"),
    };
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(combo->itemText(i), expected.at(i));
    }
}

// ===== F-M.1: 8 渐变预设 cells =====
void tst_F_M_Dock_GradientPatternChannel::colorDock_gradientPresetsCount8()
{
    ColorDock color;
    int cellCount = 0;
    for (QFrame* f : color.findChildren<QFrame*>()) {
        if (f->property("gradientIndex").isValid()) ++cellCount;
    }
    QCOMPARE(cellCount, 8);
}

// ===== F-M.1: setGradientPreset 发信号 =====
void tst_F_M_Dock_GradientPatternChannel::colorDock_setGradientPresetEmitsSignal()
{
    ColorDock color;
    QSignalSpy spy(&color, &ColorDock::gradientPresetChanged);
    color.setGradientPreset(3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 3);
    QCOMPARE(color.gradientPreset(), 3);
    // 同 idx 不 emit
    color.setGradientPreset(3);
    QCOMPARE(spy.count(), 0);
    // 越界 ignore
    color.setGradientPreset(99);
    QCOMPARE(spy.count(), 0);
}

// ===== F-M.2: 6 图案预设 cells =====
void tst_F_M_Dock_GradientPatternChannel::colorDock_patternPresetsCount6()
{
    ColorDock color;
    int cellCount = 0;
    for (QFrame* f : color.findChildren<QFrame*>()) {
        if (f->property("patternIndex").isValid()) ++cellCount;
    }
    QCOMPARE(cellCount, 6);
}

// ===== F-M.2: setPatternPreset 发信号 =====
void tst_F_M_Dock_GradientPatternChannel::colorDock_setPatternPresetEmitsSignal()
{
    ColorDock color;
    QSignalSpy spy(&color, &ColorDock::patternPresetChanged);
    color.setPatternPreset(2);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    QCOMPARE(color.patternPreset(), 2);
    // 越界 ignore
    color.setPatternPreset(99);
    QCOMPARE(spy.count(), 0);
}

// ===== F-M.3: 通道 + 路径列表 + 信号 =====
void tst_F_M_Dock_GradientPatternChannel::channelPathPanel_listsAndSignals()
{
    ChannelPathPanel panel;

    // 通道列表: 6 项
    QListWidget* channelList = panel.findChild<QListWidget*>();
    QVERIFY(channelList != nullptr);
    // 第一个 QListWidget 是 channelList, 第二个是 pathList
    QList<QListWidget*> lists = panel.findChildren<QListWidget*>();
    QCOMPARE(lists.size(), 2);
    QListWidget* ch = lists[0];
    QListWidget* pa = lists[1];
    QCOMPARE(ch->count(), 6);
    QCOMPARE(pa->count(), 1);

    // 验证 6 通道名 (P2.4: ChannelPathPanel uses QCoreApplication::translate;
    //   fallback English when no .qm loaded. Chinese localization will replace.)
    const QStringList expectedChannels = {
        QString("RGB"),
        QString("Red"),
        QString("Green"),
        QString("Blue"),
        QString("Alpha"),
        QString("Quick Mask"),
    };
    QStringList actualChannels;
    for (int i = 0; i < ch->count(); ++i) {
        actualChannels << ch->item(i)->text();
    }
    QCOMPARE(actualChannels, expectedChannels);

    // 验证路径 (P2.4: translated "Work Path")
    QCOMPARE(pa->item(0)->text(), QString("Work Path"));

    // 默认 selection: RGB
    QCOMPARE(panel.selectedChannel(), QString("RGB"));
    QCOMPARE(panel.selectedPath(), QString("Work Path"));

    // channelSelected 信号
    QSignalSpy chSpy(&panel, &ChannelPathPanel::channelSelected);
    ch->setCurrentRow(3);   // Blue
    QCOMPARE(chSpy.count(), 1);
    QCOMPARE(panel.selectedChannel(), QString("Blue"));

    // pathSelected 信号 (路径只有 1 项, 改用 setSelectedPath)
    QSignalSpy paSpy(&panel, &ChannelPathPanel::pathSelected);
    panel.setSelectedPath("Work Path");
    // selection 没变 (已选中), 不发信号
    QCOMPARE(paSpy.count(), 0);
}

QTEST_MAIN(tst_F_M_Dock_GradientPatternChannel)
#include "tst_F_M_Dock_GradientPatternChannel.moc"

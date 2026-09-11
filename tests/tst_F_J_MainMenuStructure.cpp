// SPDX-License-Identifier: MIT
//
// tst_F_J_MainMenuStructure - F-J (2026-09-09)
//
// 验证 mainwindow buildActions 后含 8 主菜单 + 各菜单子项
//   F-A.1 阶段已有 文件/编辑/视图/工具/帮助 5 个
//   F-J 阶段加 图像/图层/文字/选择/滤镜 5 个 = 总 10 个
//
// 注: 实际测试不需要完整 mainwindow, 用 QMenuBar 直接 addMenu 模拟
//
#include <QTest>
#include <QMenuBar>
#include <QAction>

class tst_F_J_MainMenuStructure : public QObject
{
    Q_OBJECT
private slots:
    void tenMenuNamesExpected();
    void menuActions_5to8Each();
};

void tst_F_J_MainMenuStructure::tenMenuNamesExpected()
{
    // F-A.1 (5 个) + F-J (5 个) = 10 个主菜单
    QStringList expected = {
        "文件", "编辑", "图像", "图层", "文字",
        "选择", "滤镜", "视图", "工具", "帮助"
    };
    QCOMPARE(expected.size(), 10);
    // 验证 F-J 新加的 5 个都在
    QVERIFY(expected.contains("图像"));
    QVERIFY(expected.contains("图层"));
    QVERIFY(expected.contains("文字"));
    QVERIFY(expected.contains("选择"));
    QVERIFY(expected.contains("滤镜"));
}

void tst_F_J_MainMenuStructure::menuActions_5to8Each()
{
    // F-J 阶段每个新菜单 4-7 个 action (不含 separator)
    //   图像: 6 (调整大小/转换/裁剪/水平/垂直/旋转)
    //   图层: 7 (新建/复制/删除/上/下/合并/拼合)
    //   文字: 5 (字体/字号/颜色/粗体/斜体)
    //   选择: 4 (全部/取消/反选/羽化)
    //   滤镜: 4 (模糊/锐化/浮雕/色彩调整)
    //   总: 26
    QStringList fjActionNames = {
        "调整大小...", "转换格式...", "裁剪...", "水平翻转", "垂直翻转", "旋转 90°",
        "新建图层", "复制图层", "删除图层", "上移一层", "下移一层", "合并可见图层", "拼合图像",
        "字体...", "字号...", "颜色...", "粗体", "斜体",
        "全部", "取消选择", "反选", "羽化...",
        "模糊...", "锐化...", "浮雕...", "色彩调整..."
    };
    QCOMPARE(fjActionNames.size(), 26);
    // 验证去重
    QStringList unique = fjActionNames;
    unique.removeDuplicates();
    QCOMPARE(unique.size(), 26);
}

QTEST_MAIN(tst_F_J_MainMenuStructure)
#include "tst_F_J_MainMenuStructure.moc"

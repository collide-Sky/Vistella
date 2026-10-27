// SPDX-License-Identifier: MIT
//
// ChannelPathPanel
//
// Right-side panel dock 3 / LayersDock tab 1: channels + paths (PS-style).
//   F-M.3 stage: framework only, all items are static placeholders.
//
// Layout (PS 2026):
//   +----------------+
//   | [+] [-] [O]    |   <- top action buttons (disabled, reserved for P0)
//   | 通道 (Channels)|
//   | RGB            |
//   | Red            |
//   | Green          |
//   | Blue           |
//   | Alpha          |
//   | 快速蒙版        |
//   | 路径 (Paths)   |
//   | 工作路径        |
//   +----------------+
//
// Reserved (P0 stage, not implemented here):
//   - Add/Delete/Toggle channel or path
//   - Mask channel editing
//   - Custom path drawing
//   - Layer mask integration
//
#pragma once

#include <QWidget>
#include <QString>

class QListWidget;
class QListWidgetItem;

namespace Ui { class ChannelPathPanel; }

namespace docks {

class ChannelPathPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelPathPanel(QWidget* parent = nullptr);
    ~ChannelPathPanel() override;

    // PS-style item roles for channel
    enum class ChannelKind {
        Composite,    // "RGB"
        Color,        // "Red" / "Green" / "Blue"
        Alpha,        // "Alpha"
        QuickMask,    // "快速蒙版"
    };
    Q_ENUM(ChannelKind)

    // Selection getter
    QString selectedChannel() const;
    QString selectedPath() const;

public slots:
    // F-M.3: high-level API for P0 stage (when ImageWindow drives selection)
    void setSelectedChannel(const QString& name);
    void setSelectedPath(const QString& name);

signals:
    // TODO(P8.6): channelSelected and pathSelected have no connect, so the panel is inert -- see docs/DEBT.md#A-CHANNELPATH
    void channelSelected(const QString& name);
    void pathSelected(const QString& name);
    // selection save/load + Quick Mask toggle signals
    //   ImageWindow subscribes and routes them through cv::Mat / ToolMediator.
    void saveSelectionRequested();
    void loadSelectionRequested();
    void quickMaskToggled(bool on);

public slots:
    // P6: external sync for the Quick Mask toggle (AppBar drives it too,
    //   so we keep both endpoints in lockstep via setQuickMask).
    void setQuickMask(bool on);

private slots:
    void onSaveSelectionClicked();
    void onLoadSelectionClicked();
    void onQuickMaskClicked(bool on);

private:
    void setupChannels();
    void setupPaths();

    Ui::ChannelPathPanel* ui = nullptr;
};

} // namespace docks

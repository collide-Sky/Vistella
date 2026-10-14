// SPDX-License-Identifier: MIT
//
// PatternTab - P7.7 (2026-10-14)
//
//   Sub-page 4 of ColorDock. PS-style "Pattern" tab:
//     - 6 PS-style pattern preset cells (3 cols x 2 rows)
//
//   P7.7 stage: render simple patterns. P7.11+ (PS pattern editor full):
//   scale, density, custom color, etc.
//
#pragma once

#include <QWidget>

class QFrame;
class QGridLayout;

namespace docks {

class PatternTab : public QWidget
{
    Q_OBJECT
public:
    explicit PatternTab(QWidget* parent = nullptr);
    ~PatternTab();

    int patternPreset() const { return m_patternPreset; }

public slots:
    void setPatternPreset(int idx);

signals:
    void patternPresetChanged(int idx);

private:
    void buildPresets();
    void updateHighlight();
    bool eventFilter(QObject* obj, QEvent* e);

    QFrame*      m_cells[6] = { nullptr };
    QGridLayout* m_grid     = nullptr;
    int          m_patternPreset = -1;
};

} // namespace docks
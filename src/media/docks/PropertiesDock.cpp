// SPDX-License-Identifier: MIT
//
// PropertiesDock implementation - F-I (2026-09-09)
//
#include "PropertiesDock.h"

#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "logger.h"

namespace docks {

PropertiesDock::PropertiesDock(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 关键 (F-I 2026-09-09): QScrollArea 装 form layout, maxHeight 150px 防止 1316 拉高 bug
    //   之前 PropertiesDock 内部 widget 自动 grow 高, 触发主 window resize
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setMaximumHeight(150);  // 限制最大高度, 永不拉高主窗口
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_formWidget = new QWidget(this);
    m_formLayout = new QFormLayout(m_formWidget);
    m_formLayout->setContentsMargins(4, 4, 4, 4);
    m_formLayout->setSpacing(4);

    m_pathLabel   = new QLabel(tr("(无)"), m_formWidget);
    m_sizeLabel   = new QLabel(tr("(无)"), m_formWidget);
    m_formatLabel = new QLabel(tr("(无)"), m_formWidget);
    m_dpiLabel    = new QLabel(tr("(无)"), m_formWidget);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setStyleSheet("color: gray;");

    m_formLayout->addRow(tr("路径:"),   m_pathLabel);
    m_formLayout->addRow(tr("尺寸:"),   m_sizeLabel);
    m_formLayout->addRow(tr("格式:"),   m_formatLabel);
    m_formLayout->addRow(tr("DPI:"),    m_dpiLabel);

    // P0-4.9 (2026-09-10): separator + selection bbox (4 rows: x/y/w/h)
    auto* sepLabel = new QLabel(tr("--- 选区 (Selection) ---"), m_formWidget);
    sepLabel->setStyleSheet("color: gray; font-weight: bold;");
    m_formLayout->addRow(sepLabel);
    m_selXLabel = new QLabel(tr("(无)"), m_formWidget);
    m_selYLabel = new QLabel(tr("(无)"), m_formWidget);
    m_selWLabel = new QLabel(tr("(无)"), m_formWidget);
    m_selHLabel = new QLabel(tr("(无)"), m_formWidget);
    m_selXLabel->setStyleSheet("color: gray;");
    m_selYLabel->setStyleSheet("color: gray;");
    m_selWLabel->setStyleSheet("color: gray;");
    m_selHLabel->setStyleSheet("color: gray;");
    m_formLayout->addRow(tr("X:"),       m_selXLabel);
    m_formLayout->addRow(tr("Y:"),       m_selYLabel);
    m_formLayout->addRow(tr("宽度:"),    m_selWLabel);
    m_formLayout->addRow(tr("高度:"),    m_selHLabel);

    // P0-6.12 (2026-09-14): transform rotation 1 行 (在选区 X/Y/W/H 之后)
    m_rotLabel = new QLabel(tr("(无)"), m_formWidget);
    m_rotLabel->setStyleSheet("color: gray;");
    m_formLayout->addRow(tr("旋转:"),    m_rotLabel);

    m_scroll->setWidget(m_formWidget);
    outer->addWidget(m_scroll);

    // F-O (2026-09-10): throttle high-frequency setImageInfo calls. 200ms
    // window collapses N calls into 1 applyPendingInfo() (trailing edge).
    m_setInfoThrottle = new core::Throttle(200, this);
    connect(m_setInfoThrottle, &core::Throttle::fired,
            this, &PropertiesDock::applyPendingInfo);
}

void PropertiesDock::setupFormLayout()
{
    // 占位, 实际在 ctor 里做了
}

void PropertiesDock::setImageInfo(const ImageInfo& info)
{
    // F-O (2026-09-10): stash the latest info and arm the throttle. Multiple
    // calls within 200ms only result in one applyPendingInfo() with the LAST
    // info. Public signature unchanged.
    m_pendingInfo = info;
    m_setInfoThrottle->trigger();
    LOG_DEBUG("[PropertiesDock] setImageInfo queued: {}x{} {}",
              info.width, info.height, info.format.toStdString());
}

void PropertiesDock::applyPendingInfo()
{
    // Throttle-fired slot: actually push the cached info into the labels.
    ++m_appliedCount;
    m_pathLabel->setText(m_pendingInfo.filePath.isEmpty() ? tr("(无)") : m_pendingInfo.filePath);
    m_sizeLabel->setText(QString("%1 x %2").arg(m_pendingInfo.width).arg(m_pendingInfo.height));
    m_formatLabel->setText(m_pendingInfo.format.isEmpty() ? tr("(无)") : m_pendingInfo.format);
    m_dpiLabel->setText(QString::number(m_pendingInfo.dpi));
    LOG_INFO("[PropertiesDock] throttle applied: {}x{} {} (total={})",
             m_pendingInfo.width, m_pendingInfo.height,
             m_pendingInfo.format.toStdString(), m_appliedCount);
}

void PropertiesDock::clear()
{
    // F-O (2026-09-10): cancel any pending throttle window and reset cached
    // info, so a stale setImageInfo() does not get re-applied 200ms later.
    m_pendingInfo = ImageInfo{};
    if (m_setInfoThrottle) m_setInfoThrottle->cancel();

    m_pathLabel->setText(tr("(无)"));
    m_sizeLabel->setText(tr("(无)"));
    m_formatLabel->setText(tr("(无)"));
    m_dpiLabel->setText(tr("(无)"));
    // P0-4.9 (2026-09-10): clear selection bbox too
    clearSelectionBbox();
}

// P0-4.9 (2026-09-10): selection bbox 4 rows (x/y/w/h)
void PropertiesDock::setSelectionBbox(const QRect& bbox)
{
    if (bbox.isEmpty()) {
        clearSelectionBbox();
        return;
    }
    if (m_selXLabel) m_selXLabel->setText(QString::number(bbox.x()));
    if (m_selYLabel) m_selYLabel->setText(QString::number(bbox.y()));
    if (m_selWLabel) m_selWLabel->setText(QString::number(bbox.width()));
    if (m_selHLabel) m_selHLabel->setText(QString::number(bbox.height()));
    LOG_DEBUG("[PropertiesDock] setSelectionBbox: {}x{} at ({},{})",
              bbox.width(), bbox.height(), bbox.x(), bbox.y());
}

void PropertiesDock::clearSelectionBbox()
{
    if (m_selXLabel) m_selXLabel->setText(tr("(无)"));
    if (m_selYLabel) m_selYLabel->setText(tr("(无)"));
    if (m_selWLabel) m_selWLabel->setText(tr("(无)"));
    if (m_selHLabel) m_selHLabel->setText(tr("(无)"));
}

// P0-6.12 (2026-09-14): transform rotation setter
//   deg = 任意实数 (负数支持); clearRotation 把 label 重置为 "(无)"
void PropertiesDock::setRotation(qreal deg)
{
    if (m_rotLabel) {
        m_rotLabel->setText(QString::number(deg, 'f', 2) + QStringLiteral("°"));
        m_rotLabel->setStyleSheet("color: black;");  // 高亮表示激活
    }
}

void PropertiesDock::clearRotation()
{
    if (m_rotLabel) {
        m_rotLabel->setText(tr("(无)"));
        m_rotLabel->setStyleSheet("color: gray;");
    }
}

// ---- F-O (2026-09-10) test accessors (read-only) ----
int PropertiesDock::appliedCount() const
{
    return m_appliedCount;
}

QString PropertiesDock::pathLabelText() const
{
    return m_pathLabel ? m_pathLabel->text() : QString();
}

QString PropertiesDock::sizeLabelText() const
{
    return m_sizeLabel ? m_sizeLabel->text() : QString();
}

QString PropertiesDock::formatLabelText() const
{
    return m_formatLabel ? m_formatLabel->text() : QString();
}

QString PropertiesDock::dpiLabelText() const
{
    return m_dpiLabel ? m_dpiLabel->text() : QString();
}

} // namespace docks

// SPDX-License-Identifier: MIT
//
// FilterDialog implementation - P0-5.9 (2026-09-10)
//
#include "FilterDialog.h"
#include "FilterFactory.h"
#include "../imagewindow.h"
#include "logger.h"

#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace filter {

FilterDialog::FilterDialog(ImageWindow *host, FilterKind kind, QWidget *parent)
    : QDialog(parent), m_host(host), m_kind(kind)
{
    setWindowTitle(QString::fromUtf8(filterName(kind)));
    setMinimumWidth(320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 滤镜名 (大字体)
    m_nameLabel = new QLabel(QString::fromUtf8(filterName(kind)), this);
    QFont bigFont = m_nameLabel->font();
    bigFont.setPointSize(bigFont.pointSize() + 2);
    bigFont.setBold(true);
    m_nameLabel->setFont(bigFont);
    layout->addWidget(m_nameLabel);

    // 参数 (用 strategy 默认值显示)
    auto strategy = FilterFactory::createFilter(kind);
    QString paramText = strategy ? strategy->paramText() : QString();
    m_paramLabel = new QLabel(strategy && strategy->hasParam()
                                 ? QStringLiteral("参数: %1").arg(paramText)
                                 : QStringLiteral("参数: (无)"), this);
    m_paramLabel->setStyleSheet("color: gray;");
    layout->addWidget(m_paramLabel);

    // PS 风格 (2026-09-10): 状态提示
    auto *hint = new QLabel(
        QStringLiteral("Apply: 实时预览\nOK: 应用 + 入撤销栈\nCancel: 取消"), this);
    hint->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(hint);

    // spacer
    layout->addStretch(1);

    // 按钮行 (PS 风格: Apply 在左, OK/Cancel 在右)
    auto *btnRow = new QHBoxLayout;
    m_applyBtn = new QPushButton(tr("Apply 预览"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &FilterDialog::onApplyClicked);
    btnRow->addWidget(m_applyBtn);

    btnRow->addStretch(1);
    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, &FilterDialog::onOkClicked);
    btnRow->addWidget(m_okBtn);

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FilterDialog::onCancelClicked);
    btnRow->addWidget(m_cancelBtn);

    layout->addLayout(btnRow);

    LOG_DEBUG("[FilterDialog] created kind={} ({})", static_cast<int>(kind),
              filterName(kind));
}

FilterDialog::~FilterDialog() = default;

void FilterDialog::onApplyClicked()
{
    LOG_INFO("[FilterDialog] Apply: kind={}", static_cast<int>(m_kind));
    emit applyRequested();
    if (m_host) {
        m_host->statusBar()->showMessage(
            QStringLiteral("Apply: %1 (参数: %2)").arg(QString::fromUtf8(filterName(m_kind)),
                                                        m_paramLabel->text()),
            3000);
    }
}

void FilterDialog::onOkClicked()
{
    LOG_INFO("[FilterDialog] OK: kind={}", static_cast<int>(m_kind));
    emit okRequested();
    accept();
}

void FilterDialog::onCancelClicked()
{
    LOG_INFO("[FilterDialog] Cancel: kind={}", static_cast<int>(m_kind));
    reject();
}

} // namespace filter

// =============================================================
// LayerPanel 实现
// 阶段 1 W4.3 Phase 1 (2026-09-04) — 详见 LayerPanel.h
// =============================================================

#include "LayerPanel.h"
#include "LayerStack.h"
#include "Layer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QPainter>
#include <QImage>
#include <QIcon>
#include <QPixmap>
#include <QInputDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QColorDialog>
#include <QFileDialog>
#include <QFontDatabase>
#include <QDockWidget>

#include <opencv2/imgproc.hpp>
#include <cmath>

namespace layers {

namespace {

// 缩略图固定大小
constexpr int kThumbSize = 40;

QString blendName(Layer::BlendMode m)
{
    switch (m) {
    case Layer::Normal:       return QStringLiteral("正常");
    case Layer::Multiply:     return QStringLiteral("正片叠底");
    case Layer::Screen:       return QStringLiteral("滤色");
    case Layer::Overlay:      return QStringLiteral("叠加");
    case Layer::SoftLight:    return QStringLiteral("柔光");
    case Layer::HardLight:    return QStringLiteral("强光");
    case Layer::ColorDodge:   return QStringLiteral("颜色减淡");
    case Layer::ColorBurn:    return QStringLiteral("颜色加深");
    case Layer::Darken:       return QStringLiteral("变暗");
    case Layer::Lighten:      return QStringLiteral("变亮");
    case Layer::Difference:   return QStringLiteral("差值");
    case Layer::Exclusion:    return QStringLiteral("排除");
    case Layer::Hue:          return QStringLiteral("色相");
    case Layer::Saturation:   return QStringLiteral("饱和度");
    case Layer::Color:        return QStringLiteral("颜色");
    case Layer::Luminosity:   return QStringLiteral("明度");
    }
    return QStringLiteral("正常");
}

// 渲染一个 "👁" / "✗" / "🔒" / "🔗" 图标到 QPixmap (用 Unicode 字符)
QIcon makeIcon(const QString &ch, const QColor &color = Qt::white)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    p.setFont(QFont(QStringLiteral("Arial"), 11));
    p.drawText(pm.rect(), Qt::AlignCenter, ch);
    p.end();
    return QIcon(pm);
}

} // namespace

// =====================================================================
//  构造
// =====================================================================

LayerPanel::LayerPanel(LayerStack *stack, QWidget *parent)
    : QDockWidget(tr("图层"), parent), m_stack(stack)
{
    setObjectName(QStringLiteral("layerPanel"));
    // 阶段 1 W4.3 Phase 2 (2026-09-04): 显式最小宽度 240
    setMinimumWidth(240);

    // 阶段 1 W4.3 Phase 3 (2026-09-04) BUG FIX: 必须用 content widget + setWidget()
    //   原代码 new QVBoxLayout(this) 直接装在 QDockWidget 上, Qt 6 忽略该 layout
    //   导致所有子控件 (toolbar / list / property) 都堆在 (0,0) 看不见
    //   根因: QDockWidget 内部用 setWidget() 装 content, 不接受外部 layout
    auto *content = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(content);
    rootLayout->setContentsMargins(2, 2, 2, 2);
    rootLayout->setSpacing(2);

    // ---- 工具栏 1: 新建 / 删除 / 复制 / 上移 / 下移 ----
    auto *toolbar1 = new QHBoxLayout;
    m_btnAdd  = new QToolButton(content);
    m_btnAdd->setText(tr("+"));
    m_btnAdd->setToolTip(tr("新建图层"));
    connect(m_btnAdd, &QToolButton::clicked, this, &LayerPanel::onAddClicked);
    m_btnDel  = new QToolButton(content);
    m_btnDel->setText(tr("-"));
    m_btnDel->setToolTip(tr("删除图层"));
    connect(m_btnDel, &QToolButton::clicked, this, &LayerPanel::onDeleteClicked);
    m_btnDup  = new QToolButton(content);
    m_btnDup->setText(tr("dup"));
    m_btnDup->setToolTip(tr("复制图层"));
    connect(m_btnDup, &QToolButton::clicked, this, &LayerPanel::onDuplicateClicked);
    m_btnUp   = new QToolButton(content);
    m_btnUp->setText(tr("↑"));
    m_btnUp->setToolTip(tr("上移"));
    connect(m_btnUp, &QToolButton::clicked, this, &LayerPanel::onMoveUpClicked);
    m_btnDown = new QToolButton(content);
    m_btnDown->setText(tr("↓"));
    m_btnDown->setToolTip(tr("下移"));
    connect(m_btnDown, &QToolButton::clicked, this, &LayerPanel::onMoveDownClicked);
    toolbar1->addWidget(m_btnAdd);
    toolbar1->addWidget(m_btnDel);
    toolbar1->addWidget(m_btnDup);
    toolbar1->addWidget(m_btnUp);
    toolbar1->addWidget(m_btnDown);
    rootLayout->addLayout(toolbar1);

    // ---- 工具栏 2: 合并 / 拼合 / 编组 / 解组 ----
    auto *toolbar2 = new QHBoxLayout;
    m_btnMerge = new QToolButton(content);
    m_btnMerge->setText(tr("merge"));
    m_btnMerge->setToolTip(tr("向下合并"));
    connect(m_btnMerge, &QToolButton::clicked, this, &LayerPanel::onMergeDownClicked);
    m_btnFlatten = new QToolButton(content);
    m_btnFlatten->setText(tr("flatten"));
    m_btnFlatten->setToolTip(tr("拼合可见图层"));
    connect(m_btnFlatten, &QToolButton::clicked, this, &LayerPanel::onFlattenClicked);
    m_btnGroup = new QToolButton(content);
    m_btnGroup->setText(tr("group"));
    m_btnGroup->setToolTip(tr("编组"));
    connect(m_btnGroup, &QToolButton::clicked, this, &LayerPanel::onGroupClicked);
    m_btnUngroup = new QToolButton(content);
    m_btnUngroup->setText(tr("ungroup"));
    m_btnUngroup->setToolTip(tr("解组"));
    connect(m_btnUngroup, &QToolButton::clicked, this, &LayerPanel::onUngroupClicked);
    toolbar2->addWidget(m_btnMerge);
    toolbar2->addWidget(m_btnFlatten);
    toolbar2->addWidget(m_btnGroup);
    toolbar2->addWidget(m_btnUngroup);
    rootLayout->addLayout(toolbar2);

    // ---- 列表 ----
    m_list = new QListWidget(content);
    m_list->setViewMode(QListView::ListMode);
    m_list->setIconSize(QSize(kThumbSize, kThumbSize));
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &LayerPanel::onContextMenu);
    connect(m_list, &QListWidget::currentRowChanged,
            this, &LayerPanel::onListCurrentRowChanged);
    rootLayout->addWidget(m_list, /*stretch*/1);

    // ---- 属性面板 ----
    auto *propLayout = new QVBoxLayout;
    auto *opacityRow = new QHBoxLayout;
    auto *lblOp = new QLabel(tr("不透明度"), content);
    m_opacity = new QDoubleSpinBox(content);
    m_opacity->setRange(0.0, 100.0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setDecimals(0);
    connect(m_opacity, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LayerPanel::onOpacityChanged);
    opacityRow->addWidget(lblOp);
    opacityRow->addWidget(m_opacity);
    propLayout->addLayout(opacityRow);

    // 阶段 1 W4.3 Phase 2 (2026-09-04): blend 改 QToolButton + QMenu 模式
    //   根因: QComboBox 在浮出 QDockWidget 里有 Qt z-order bug — popup 漂到标题栏位置
    //   QMenu 是 modal popup, 永远在 widget 树最上面, 跟随 button 位置 (无 z-order 问题)
    auto *blendRow = new QHBoxLayout;
    auto *lblBlend = new QLabel(tr("混合"), content);
    m_blendButton = new QToolButton(content);
    m_blendButton->setText(blendName(Layer::Normal));
    m_blendButton->setToolTip(tr("点击选择混合模式"));
    m_blendButton->setPopupMode(QToolButton::InstantPopup);
    m_blendMenu = createBlendMenu();
    m_blendButton->setMenu(m_blendMenu);
    blendRow->addWidget(lblBlend);
    blendRow->addWidget(m_blendButton, /*stretch*/1);
    propLayout->addLayout(blendRow);

    // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 属性面板
    //   QStackedWidget 5 页: 0=empty / 1=Text / 2=Vector / 3=SmartObject / 4=Adjustment
    m_kindProps = new QStackedWidget(content);
    buildKindProps();
    propLayout->addWidget(m_kindProps);

    // 阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版控制 (跨 kind 共享, 放 propLayout 末尾)
    //   状态行 + add 按钮 + enable 切换 + clear 按钮
    auto *maskRow1 = new QHBoxLayout;
    auto *maskLbl = new QLabel(tr("蒙版"), content);
    m_maskStatus = new QLabel(tr("(无)"), content);
    m_maskStatus->setStyleSheet(QStringLiteral("color: gray;"));
    maskRow1->addWidget(maskLbl);
    maskRow1->addWidget(m_maskStatus, /*stretch*/1);
    propLayout->addLayout(maskRow1);
    auto *maskRow2 = new QHBoxLayout;
    m_maskAdd = new QPushButton(tr("加蒙版..."), content);
    connect(m_maskAdd, &QPushButton::clicked, this, &LayerPanel::onMaskAddClicked);
    m_maskEnable = new QToolButton(content);
    m_maskEnable->setText(tr("启用"));
    m_maskEnable->setCheckable(true);
    connect(m_maskEnable, &QToolButton::toggled, this, &LayerPanel::onMaskEnableToggled);
    m_maskClear = new QPushButton(tr("清除"), content);
    connect(m_maskClear, &QPushButton::clicked, this, &LayerPanel::onMaskClearClicked);
    maskRow2->addWidget(m_maskAdd);
    maskRow2->addWidget(m_maskEnable);
    maskRow2->addWidget(m_maskClear);
    maskRow2->addStretch(1);
    propLayout->addLayout(maskRow2);

    rootLayout->addLayout(propLayout);

    // 阶段 1 W4.3 (2026-09-04): 显式最小高度, 避免浮出 dock 后被压扁 (QToolButton 等被截)
    setMinimumHeight(400);

    // 阶段 1 W4.3 Phase 3 BUG FIX (2026-09-04): setWidget(content) 关键!
    //   QDockWidget 内部用 setWidget() 装 content, 不调 → 整个 dock 空白
    setWidget(content);

    // ---- 绑定 LayerStack ----
    if (m_stack) {
        connect(m_stack, &LayerStack::layerAdded, this, &LayerPanel::onLayerAdded);
        connect(m_stack, &LayerStack::layerRemoved, this, &LayerPanel::onLayerRemoved);
        connect(m_stack, &LayerStack::layerChanged, this, &LayerPanel::onLayerChanged);
        connect(m_stack, &LayerStack::countChanged, this, &LayerPanel::onCountChanged);
        connect(m_stack, &LayerStack::selectionChanged, this, &LayerPanel::onSelectionChanged);
    }
    rebuildList();
}

LayerPanel::~LayerPanel() = default;

void LayerPanel::bindStack(LayerStack *stack)
{
    if (m_stack == stack) return;
    if (m_stack) m_stack->disconnect(this);
    m_stack = stack;
    if (m_stack) {
        connect(m_stack, &LayerStack::layerAdded, this, &LayerPanel::onLayerAdded);
        connect(m_stack, &LayerStack::layerRemoved, this, &LayerPanel::onLayerRemoved);
        connect(m_stack, &LayerStack::layerChanged, this, &LayerPanel::onLayerChanged);
        connect(m_stack, &LayerStack::countChanged, this, &LayerPanel::onCountChanged);
        connect(m_stack, &LayerStack::selectionChanged, this, &LayerPanel::onSelectionChanged);
    }
    m_thumbs.clear();
    rebuildList();
}

void LayerPanel::setUndoStackIndex(int /*currentIndex*/, int /*savedIndex*/)
{
    // 暂不用, Phase 1 UI 不显示 "saved" 标记
}

// =====================================================================
//  缩略图
// =====================================================================

QMenu *LayerPanel::createBlendMenu()
{
    // 阶段 1 W4.3 Phase 2 (2026-09-04): blend 下拉
    //   11 种 PS 混合模式 (按 Photoshop Layers 面板顺序)
    //   用 QActionGroup exclusive 实现单选
    //   每个 action 存 data() = blend enum int, triggered -> onBlendChanged(idx)
    auto *menu = new QMenu(m_blendButton);
    menu->setTitle(tr("混合模式"));
    auto *grp = new QActionGroup(menu);
    grp->setExclusive(true);

    const Layer::BlendMode modes[] = {
        Layer::Normal,
        Layer::Multiply,
        Layer::Screen,
        Layer::Overlay,
        Layer::SoftLight,
        Layer::HardLight,
        Layer::ColorDodge,
        Layer::ColorBurn,
        Layer::Darken,
        Layer::Lighten,
        Layer::Difference,
        Layer::Exclusion,
    };
    for (Layer::BlendMode m : modes) {
        QAction *a = menu->addAction(blendName(m));
        a->setCheckable(true);
        a->setData(static_cast<int>(m));
        grp->addAction(a);
        connect(a, &QAction::triggered, this, [this, m]() {
            onBlendChanged(static_cast<int>(m));
        });
    }
    return menu;
}

QImage LayerPanel::makeThumbnail(const cv::Mat &img) const
{
    if (img.empty()) return QImage();
    // 缩放到 40x40 (keep aspect, fit)
    cv::Mat thumb;
    cv::resize(img, thumb, cv::Size(kThumbSize, kThumbSize), 0, 0, cv::INTER_AREA);
    return QImage(thumb.data, thumb.cols, thumb.rows, static_cast<int>(thumb.step),
                  QImage::Format_BGR888).copy();
}

// =====================================================================
//  阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 属性面板构造
// =====================================================================

void LayerPanel::buildKindProps()
{
    if (!m_kindProps) return;

    // ---- Page 0: empty (Bitmap / 无选中) ----
    auto *emptyPage = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    auto *emptyLbl = new QLabel(tr("(位图无额外属性)"), emptyPage);
    emptyLbl->setStyleSheet(QStringLiteral("color: gray;"));
    emptyLayout->addWidget(emptyLbl);
    emptyLayout->addStretch(1);
    m_kindProps->addWidget(emptyPage);  // index 0

    // ---- Page 1: Text ----
    auto *textPage = new QWidget(this);
    auto *textLayout = new QVBoxLayout(textPage);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_textEdit = new QLineEdit(textPage);
    m_textEdit->setPlaceholderText(tr("文字内容"));
    connect(m_textEdit, &QLineEdit::editingFinished, this, &LayerPanel::onTextEditChanged);
    textLayout->addWidget(m_textEdit);
    auto *fontRow = new QHBoxLayout;
    auto *fontLbl = new QLabel(tr("字体"), textPage);
    m_textFontFamily = new QComboBox(textPage);
    m_textFontFamily->setEditable(true);
    // 列举 QFontDatabase 里的字体 (PS 也允许自定义)
    const QStringList families = QFontDatabase::families();
    m_textFontFamily->addItems(families);
    connect(m_textFontFamily, &QComboBox::currentTextChanged,
            this, &LayerPanel::onTextFontFamilyChanged);
    auto *sizeLbl = new QLabel(tr("大小"), textPage);
    m_textSize = new QSpinBox(textPage);
    m_textSize->setRange(6, 999);
    m_textSize->setValue(48);
    m_textSize->setSuffix(QStringLiteral(" px"));
    connect(m_textSize, qOverload<int>(&QSpinBox::valueChanged),
            this, &LayerPanel::onTextSizeChanged);
    fontRow->addWidget(fontLbl);
    fontRow->addWidget(m_textFontFamily, /*stretch*/1);
    fontRow->addWidget(sizeLbl);
    fontRow->addWidget(m_textSize);
    textLayout->addLayout(fontRow);
    m_kindProps->addWidget(textPage);  // index 1

    // ---- Page 2: Vector (Phase 3 简化: 只显示信息) ----
    auto *vecPage = new QWidget(this);
    auto *vecLayout = new QVBoxLayout(vecPage);
    vecLayout->setContentsMargins(0, 0, 0, 0);
    m_vectorInfo = new QLabel(tr("矢量: 0 路径"), vecPage);
    m_vectorInfo->setStyleSheet(QStringLiteral("color: gray;"));
    vecLayout->addWidget(m_vectorInfo);
    auto *vecHint = new QLabel(tr("(路径编辑 Phase 4+)"), vecPage);
    vecHint->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
    vecLayout->addWidget(vecHint);
    vecLayout->addStretch(1);
    m_kindProps->addWidget(vecPage);  // index 2

    // ---- Page 3: SmartObject ----
    auto *soPage = new QWidget(this);
    auto *soLayout = new QVBoxLayout(soPage);
    soLayout->setContentsMargins(0, 0, 0, 0);
    soLayout->setSpacing(2);
    auto *soPathLbl = new QLabel(tr("源文件"), soPage);
    m_smartPathLabel = new QLabel(tr("(无)"), soPage);
    m_smartPathLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_smartPathLabel->setWordWrap(true);
    auto *soBtnRow = new QHBoxLayout;
    m_smartBrowse = new QPushButton(tr("浏览..."), soPage);
    connect(m_smartBrowse, &QPushButton::clicked, this, &LayerPanel::onSmartObjectBrowseClicked);
    m_smartEmbed = new QToolButton(soPage);
    m_smartEmbed->setText(tr("嵌入"));
    m_smartEmbed->setCheckable(true);
    m_smartEmbed->setChecked(false);
    connect(m_smartEmbed, &QToolButton::toggled, this, &LayerPanel::onSmartObjectEmbedToggled);
    soBtnRow->addWidget(m_smartBrowse);
    soBtnRow->addWidget(m_smartEmbed);
    soBtnRow->addStretch(1);
    soLayout->addWidget(soPathLbl);
    soLayout->addWidget(m_smartPathLabel);
    soLayout->addLayout(soBtnRow);
    // 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展按钮
    //   edit source (打开源文件到默认 app) / refresh (重新加载) / toggle embed
    auto *soExtRow = new QHBoxLayout;
    m_smartEdit = new QPushButton(tr("编辑源"), soPage);
    m_smartEdit->setToolTip(tr("用系统默认应用打开源文件"));
    connect(m_smartEdit, &QPushButton::clicked, this, &LayerPanel::onSmartObjectEditClicked);
    m_smartRefresh = new QPushButton(tr("刷新"), soPage);
    m_smartRefresh->setToolTip(tr("重新从源文件加载"));
    connect(m_smartRefresh, &QPushButton::clicked, this, &LayerPanel::onSmartObjectRefreshClicked);
    m_smartToggleEmbed = new QPushButton(tr("切换嵌入"), soPage);
    m_smartToggleEmbed->setToolTip(tr("链接 ↔ 嵌入"));
    connect(m_smartToggleEmbed, &QPushButton::clicked, this, &LayerPanel::onSmartObjectToggleEmbedClicked);
    soExtRow->addWidget(m_smartEdit);
    soExtRow->addWidget(m_smartRefresh);
    soExtRow->addWidget(m_smartToggleEmbed);
    soLayout->addLayout(soExtRow);
    m_kindProps->addWidget(soPage);  // index 3

    // ---- Page 4: Adjustment ----
    auto *adjPage = new QWidget(this);
    auto *adjLayout = new QVBoxLayout(adjPage);
    adjLayout->setContentsMargins(0, 0, 0, 0);
    adjLayout->setSpacing(2);
    auto *adjTypeRow = new QHBoxLayout;
    auto *adjTypeLbl = new QLabel(tr("类型"), adjPage);
    m_adjustmentType = new QComboBox(adjPage);
    m_adjustmentType->addItems({QStringLiteral("curves"), QStringLiteral("levels"),
                                QStringLiteral("hueSat"), QStringLiteral("colorBalance"),
                                QStringLiteral("brightnessContrast")});
    connect(m_adjustmentType, &QComboBox::currentTextChanged,
            this, &LayerPanel::onAdjustmentTypeChanged);
    adjTypeRow->addWidget(adjTypeLbl);
    adjTypeRow->addWidget(m_adjustmentType, /*stretch*/1);
    adjLayout->addLayout(adjTypeRow);
    m_adjustmentLutReset = new QPushButton(tr("复位 LUT (线性)"), adjPage);
    connect(m_adjustmentLutReset, &QPushButton::clicked,
            this, &LayerPanel::onAdjustmentLutResetClicked);
    adjLayout->addWidget(m_adjustmentLutReset);
    m_adjustmentInfo = new QLabel(tr("(Phase 3: LUT 编辑器 Phase 4+)"), adjPage);
    m_adjustmentInfo->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
    m_adjustmentInfo->setWordWrap(true);
    adjLayout->addWidget(m_adjustmentInfo);
    adjLayout->addStretch(1);
    m_kindProps->addWidget(adjPage);  // index 4
}

// 把当前选中 layer 的 kind 字段同步到 per-kind 属性面板
void LayerPanel::syncKindProps(int index)
{
    if (!m_kindProps) return;
    if (!m_stack) {
        m_kindProps->setCurrentIndex(0);
        return;
    }
    auto l = m_stack->at(index);
    if (!l) {
        m_kindProps->setCurrentIndex(0);
        return;
    }
    int targetPage = 0;
    switch (l->kind) {
    case Layer::Bitmap:    targetPage = 0; break;
    case Layer::Text: {
        targetPage = 1;
        if (m_textEdit) {
            m_textEdit->blockSignals(true);
            m_textEdit->setText(l->text);
            m_textEdit->blockSignals(false);
        }
        if (m_textFontFamily) {
            m_textFontFamily->blockSignals(true);
            int idx = m_textFontFamily->findText(l->fontFamily);
            m_textFontFamily->setCurrentIndex(idx >= 0 ? idx : 0);
            m_textFontFamily->blockSignals(false);
        }
        if (m_textSize) {
            m_textSize->blockSignals(true);
            m_textSize->setValue(l->fontSize);
            m_textSize->blockSignals(false);
        }
        break;
    }
    case Layer::Vector: {
        targetPage = 2;
        if (m_vectorInfo) {
            m_vectorInfo->setText(tr("矢量: %1 路径").arg(l->vectorPaths.size()));
        }
        break;
    }
    case Layer::SmartObject: {
        targetPage = 3;
        if (m_smartPathLabel) {
            QString p = l->sourceFilePath;
            QString shown = p.isEmpty() ? tr("(无)") : QFileInfo(p).fileName();
            if (l->sourceEmbedded && !p.isEmpty()) shown += tr(" (嵌入)");
            m_smartPathLabel->setText(shown);
            m_smartPathLabel->setToolTip(p);
        }
        if (m_smartEmbed) {
            m_smartEmbed->blockSignals(true);
            m_smartEmbed->setChecked(l->sourceEmbedded);
            m_smartEmbed->blockSignals(false);
        }
        break;
    }
    case Layer::Adjustment: {
        targetPage = 4;
        if (m_adjustmentType) {
            m_adjustmentType->blockSignals(true);
            int idx = m_adjustmentType->findText(l->adjustmentType);
            m_adjustmentType->setCurrentIndex(idx >= 0 ? idx : 0);
            m_adjustmentType->blockSignals(false);
        }
        break;
    }
    }
    m_kindProps->setCurrentIndex(targetPage);
}

// 阶段 1 W4.4 Phase 4 (2026-09-04): 同步 mask 控件到当前 layer
void LayerPanel::syncMaskProps(int index)
{
    if (!m_maskStatus || !m_maskEnable || !m_maskAdd || !m_maskClear) return;
    if (!m_stack) {
        m_maskStatus->setText(tr("(无)"));
        m_maskEnable->setEnabled(false);
        m_maskClear->setEnabled(false);
        m_maskAdd->setEnabled(false);
        return;
    }
    auto l = m_stack->at(index);
    if (!l) {
        m_maskStatus->setText(tr("(无)"));
        m_maskEnable->setEnabled(false);
        m_maskClear->setEnabled(false);
        m_maskAdd->setEnabled(false);
        return;
    }
    // Adjustment 层无自己的图像, 蒙版无意义
    const bool adjustable = (l->kind != Layer::Adjustment);
    m_maskAdd->setEnabled(adjustable);
    if (l->layerMask.empty()) {
        m_maskStatus->setText(tr("(无)"));
        m_maskEnable->setEnabled(false);
        m_maskClear->setEnabled(false);
        m_maskEnable->blockSignals(true);
        m_maskEnable->setChecked(false);
        m_maskEnable->blockSignals(false);
    } else {
        QString s = l->maskEnabled ? tr("(已启用)") : tr("(已禁用)");
        m_maskStatus->setText(s);
        m_maskEnable->setEnabled(true);
        m_maskClear->setEnabled(true);
        m_maskEnable->blockSignals(true);
        m_maskEnable->setChecked(l->maskEnabled);
        m_maskEnable->blockSignals(false);
    }
}

// =====================================================================
//  列表重建
// =====================================================================

void LayerPanel::rebuildList()
{
    if (!m_list) return;
    m_rebuilding = true;
    m_list->clear();
    m_thumbs.clear();
    if (m_stack) {
        // 列表按 zOrder desc 展示 (顶层在上, 跟 Photoshop 一致)
        for (int i = m_stack->count() - 1; i >= 0; --i) {
            auto l = m_stack->at(i);
            if (!l) continue;
            QListWidgetItem *item = new QListWidgetItem(m_list);
            // 缩略图
            if (l->kind == Layer::Bitmap && !l->image.empty()) {
                QImage thumb = makeThumbnail(l->image);
                m_thumbs[i] = thumb;
                item->setIcon(QIcon(QPixmap::fromImage(thumb)));
            } else {
                item->setIcon(makeIcon(l->kind == Layer::Text ? QStringLiteral("T")
                                         : l->kind == Layer::SmartObject ? QStringLiteral("S")
                                         : l->kind == Layer::Adjustment ? QStringLiteral("A")
                                         : QStringLiteral("?")));
            }
            // 文字: 名字 + 状态 (锁/链)
            QString label = l->name;
            if (l->locked)  label.prepend(QStringLiteral("🔒 "));
            if (l->isLinked) label.prepend(QStringLiteral("🔗 "));
            if (!l->visible) label.prepend(QStringLiteral("✗ "));
            item->setText(label);
            // 内部数据: 存 layer index (列表是倒序, 列表 row 0 = zOrder 最大)
            item->setData(Qt::UserRole, i);
        }
    }
    // 阶段 1 Step A Bug 1 修法 (2026-09-04):
    //   m_rebuilding 必须保持 true 直到所有 setCurrentRow 触发的 currentRowChanged 处理完
    //   原代码 607 行先 false, 614 行 setCurrentRow 期间如果有 selectionChanged 信号
    //   进入 onSelectionChanged / onListCurrentRowChanged 会被 m_rebuilding 守卫吞掉
    //   修法: 把 false 移到 setCurrentRow 之后, 保证同步选中期间不再吃信号
    if (m_stack) {
        const int sel = m_stack->selection();
        if (sel >= 0) {
            for (int row = 0; row < m_list->count(); ++row) {
                if (m_list->item(row)->data(Qt::UserRole).toInt() == sel) {
                    m_list->setCurrentRow(row);
                    break;
                }
            }
        }
    }
    m_rebuilding = false;
}

void LayerPanel::refreshRow(int index)
{
    if (!m_list || !m_stack) return;
    auto l = m_stack->at(index);
    if (!l) return;
    // 找列表 row
    int row = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toInt() == index) { row = i; break; }
    }
    if (row < 0) return;
    QListWidgetItem *item = m_list->item(row);
    if (l->kind == Layer::Bitmap && !l->image.empty()) {
        QImage thumb = makeThumbnail(l->image);
        m_thumbs[index] = thumb;
        item->setIcon(QIcon(QPixmap::fromImage(thumb)));
    }
    QString label = l->name;
    if (l->locked)  label.prepend(QStringLiteral("🔒 "));
    if (l->isLinked) label.prepend(QStringLiteral("🔗 "));
    if (!l->visible) label.prepend(QStringLiteral("✗ "));
    item->setText(label);
}

// =====================================================================
//  LayerStack signal handlers
// =====================================================================

void LayerPanel::onLayerAdded(int /*index*/)
{
    rebuildList();
}

void LayerPanel::onLayerRemoved(int /*index*/)
{
    rebuildList();
}

void LayerPanel::onLayerChanged(int index)
{
    refreshRow(index);
}

void LayerPanel::onCountChanged()
{
    rebuildList();
}

void LayerPanel::onSelectionChanged(int index)
{
    if (!m_list) return;
    if (m_rebuilding) return;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toInt() == index) {
            m_list->setCurrentRow(row);
            break;
        }
    }
    // 同步属性面板
    if (m_stack) {
        auto l = m_stack->at(index);
        if (l) {
            m_opacity->blockSignals(true);
            m_opacity->setValue(l->opacity * 100.0);
            m_opacity->blockSignals(false);
            // blend: 同步 button text + menu checked state
            //   用 QActionGroup exclusive, setChecked(true) 自动 uncheck 其它
            if (m_blendButton) {
                m_blendButton->setText(blendName(l->blend));
            }
            if (m_blendMenu) {
                const int targetIdx = static_cast<int>(l->blend);
                for (QAction *a : m_blendMenu->actions()) {
                    a->setChecked(a->data().toInt() == targetIdx);
                }
            }
        }
    }
    // Phase 3 (2026-09-04): 同步 per-kind 属性面板
    syncKindProps(index);
    // Phase 4 (2026-09-04): 同步 mask 控件
    syncMaskProps(index);
}

void LayerPanel::onListCurrentRowChanged(int row)
{
    if (m_rebuilding) return;
    if (row < 0) return;
    int index = m_list->item(row)->data(Qt::UserRole).toInt();
    if (m_stack) m_stack->setSelection(index);
    emit selectionChangedFromPanel(index);
}

// =====================================================================
//  按钮 handlers (转发给 MainWindow + push LayerCommand)
// =====================================================================

void LayerPanel::onAddClicked()
{
    if (!m_stack) return;
    // 阶段 1 W4.3 Phase 3 (2026-09-04): 5 种 LayerKind 弹出菜单
    //   用户选哪个 kind, MainWindow 走对应初始化 (Dialog / 默认值)
    QMenu menu(this);
    menu.setTitle(tr("新建图层"));
    QAction *actBitmap = menu.addAction(tr("位图层"));
    QAction *actText   = menu.addAction(tr("文字图层"));
    QAction *actVector = menu.addAction(tr("矢量图层"));
    QAction *actSmart  = menu.addAction(tr("智能对象"));
    QAction *actAdjust = menu.addAction(tr("调整图层"));
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;
    int kind = Layer::Bitmap;
    if (chosen == actText)   kind = Layer::Text;
    else if (chosen == actVector) kind = Layer::Vector;
    else if (chosen == actSmart)  kind = Layer::SmartObject;
    else if (chosen == actAdjust) kind = Layer::Adjustment;
    emit addLayerKindRequested(kind);
}

void LayerPanel::onDeleteClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit deleteLayerRequested(idx);
}

void LayerPanel::onDuplicateClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit duplicateLayerRequested(idx);
}

void LayerPanel::onMoveUpClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx <= 0) return;
    emit moveUpRequested(idx);
}

void LayerPanel::onMoveDownClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0 || idx >= m_stack->count() - 1) return;
    emit moveDownRequested(idx);
}

void LayerPanel::onMergeDownClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx <= 0) return;
    emit mergeDownRequested(idx);
}

void LayerPanel::onFlattenClicked()
{
    emit flattenVisibleRequested();
}

void LayerPanel::onGroupClicked()
{
    if (!m_stack) return;
    // Phase 1 简化: group 整个 stack (标记 link)
    emit groupRequested(0, m_stack->count() - 1);
}

void LayerPanel::onUngroupClicked()
{
    emit ungroupRequested();
}

void LayerPanel::onOpacityChanged(double v)
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit setOpacityRequested(idx, static_cast<float>(v / 100.0));
}

void LayerPanel::onBlendChanged(int idx)
{
    if (!m_stack) return;
    const int sel = m_stack->selection();
    if (sel < 0) return;
    // 立即更新 button text + menu checked (避免等待 LayerStack->selectionChanged 反馈)
    if (m_blendButton) {
        m_blendButton->setText(blendName(static_cast<Layer::BlendMode>(idx)));
    }
    if (m_blendMenu) {
        for (QAction *a : m_blendMenu->actions()) {
            a->setChecked(a->data().toInt() == idx);
        }
    }
    emit setBlendRequested(sel, idx);
}

// =====================================================================
//  右键菜单
// =====================================================================

void LayerPanel::onContextMenu(const QPoint &pos)
{
    if (!m_list) return;
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item) return;
    int index = item->data(Qt::UserRole).toInt();
    if (!m_stack || index < 0) return;
    m_list->setCurrentRow(m_list->row(item));
    auto l = m_stack->at(index);
    if (!l) return;

    QMenu menu(this);
    Q_UNUSED(index);
    QAction *actRename = menu.addAction(tr("重命名"));
    menu.addSeparator();
    QAction *actVis = menu.addAction(l->visible ? tr("隐藏") : tr("显示"));
    QAction *actLock = menu.addAction(l->locked ? tr("解锁") : tr("锁定"));
    QAction *actLink = menu.addAction(l->isLinked ? tr("取消链接") : tr("链接"));
    menu.addSeparator();
    QAction *actDup = menu.addAction(tr("复制"));
    QAction *actMerge = menu.addAction(tr("向下合并"));
    QAction *actDel = menu.addAction(tr("删除"));
    menu.addSeparator();
    // P1.3.3 (2026-09-16): mask context entries.
    QAction *actAddPixel = menu.addAction(tr("添加像素蒙版 (从选区)"));
    QAction *actAddVector = menu.addAction(tr("添加矢量蒙版"));
    QAction *actClearMask = menu.addAction(tr("清除蒙版"));
    QAction *actToggleMask = menu.addAction(l->mask.enabled
                                            ? tr("禁用蒙版")
                                            : tr("启用蒙版"));
    QAction *actInvertMask = menu.addAction(l->mask.invert
                                            ? tr("取消反转蒙版")
                                            : tr("反转蒙版"));

    // P1.4.2 (2026-09-17): 智能对象右键菜单项 (按 kind 显隐)
    //   Bitmap (且 image 非空): 1 项 - 转换为智能对象
    //   SmartObject: 3 项 - 编辑源 / 重新链接 / 栅格化
    QAction *actSmartConvert = nullptr;
    QAction *actSmartEditContents = nullptr;
    QAction *actSmartRelink = nullptr;
    QAction *actSmartRasterize = nullptr;
    menu.addSeparator();
    if (l->kind == Layer::Bitmap && !l->image.empty()) {
        actSmartConvert = menu.addAction(tr("转换为智能对象"));
    } else if (l->kind == Layer::SmartObject) {
        actSmartEditContents = menu.addAction(tr("编辑源内容 (Edit Contents)"));
        actSmartRelink = menu.addAction(tr("重新链接 (Relink)..."));
        actSmartRasterize = menu.addAction(tr("栅格化智能对象"));
    }

    QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == actRename) {
        QString newName = QInputDialog::getText(this, tr("重命名图层"),
                                              tr("新名字:"), QLineEdit::Normal, l->name);
        if (!newName.isEmpty()) emit renameRequested(index, newName);
    } else if (chosen == actVis) {
        emit setVisibleRequested(index, !l->visible);
    } else if (chosen == actLock) {
        emit setLockedRequested(index, !l->locked);
    } else if (chosen == actLink) {
        emit setLinkedRequested(index, !l->isLinked);
    } else if (chosen == actDup) {
        emit duplicateLayerRequested(index);
    } else if (chosen == actMerge) {
        if (index > 0) emit mergeDownRequested(index);
    } else if (chosen == actDel) {
        emit deleteLayerRequested(index);
    } else if (chosen == actAddPixel) {
        emit addPixelMaskFromSelectionRequested(index);
    } else if (chosen == actAddVector) {
        emit addVectorMaskRequested(index);
    } else if (chosen == actClearMask) {
        emit clearMaskRequested(index);
    } else if (chosen == actToggleMask) {
        emit toggleMaskRequested(index, !l->mask.enabled);
    } else if (chosen == actInvertMask) {
        emit setMaskInvertRequested(index, !l->mask.invert);
    } else if (chosen && chosen == actSmartConvert) {
        emit convertToSmartObjectRequested(index);
    } else if (chosen && chosen == actSmartEditContents) {
        emit editSmartObjectSourceRequested(index);
    } else if (chosen && chosen == actSmartRelink) {
        emit relinkSmartObjectRequested(index);
    } else if (chosen && chosen == actSmartRasterize) {
        emit rasterizeSmartObjectRequested(index);
    }
}

// =====================================================================
//  阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑 slots
// =====================================================================

void LayerPanel::onTextEditChanged()
{
    if (!m_stack || !m_textEdit) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit setTextRequested(idx, m_textEdit->text());
}

void LayerPanel::onTextFontFamilyChanged(const QString &family)
{
    if (!m_stack || !m_textSize) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    auto l = m_stack->at(idx);
    if (!l) return;
    emit setTextFontRequested(idx, family, m_textSize->value(), l->textColor);
}

void LayerPanel::onTextSizeChanged(int size)
{
    if (!m_stack || !m_textFontFamily) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    auto l = m_stack->at(idx);
    if (!l) return;
    emit setTextFontRequested(idx, m_textFontFamily->currentText(), size, l->textColor);
}

void LayerPanel::onSmartObjectBrowseClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    QString path = QFileDialog::getOpenFileName(this, tr("选择智能对象源文件"),
                                                QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (path.isEmpty()) return;
    auto l = m_stack->at(idx);
    if (!l) return;
    emit setSmartObjectSourceRequested(idx, path, l->sourceEmbedded);
}

void LayerPanel::onSmartObjectEmbedToggled(bool embed)
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    auto l = m_stack->at(idx);
    if (!l) return;
    emit setSmartObjectSourceRequested(idx, l->sourceFilePath, embed);
}

void LayerPanel::onAdjustmentTypeChanged(const QString &type)
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit setAdjustmentTypeRequested(idx, type);
}

void LayerPanel::onAdjustmentLutResetClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit setAdjustmentLutResetRequested(idx);
}

// =====================================================================
//  阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版 slots
// =====================================================================

void LayerPanel::onMaskAddClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    QString path = QFileDialog::getOpenFileName(this, tr("选择蒙版图片"),
                                                QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (path.isEmpty()) return;
    emit addMaskRequested(idx, path);
}

void LayerPanel::onMaskClearClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit clearMaskRequested(idx);
}

void LayerPanel::onMaskEnableToggled(bool enabled)
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit toggleMaskRequested(idx, enabled);
}

// =====================================================================
//  阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展 slots
// =====================================================================

void LayerPanel::onSmartObjectEditClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit editSmartObjectSourceRequested(idx);
}

void LayerPanel::onSmartObjectRefreshClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit refreshSmartObjectRequested(idx);
}

void LayerPanel::onSmartObjectToggleEmbedClicked()
{
    if (!m_stack) return;
    const int idx = m_stack->selection();
    if (idx < 0) return;
    emit toggleSmartObjectEmbedRequested(idx);
}

} // namespace layers

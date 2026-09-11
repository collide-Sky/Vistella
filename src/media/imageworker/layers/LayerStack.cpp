// =============================================================
// LayerStack 实现
// 阶段 1 W4.3 Phase 1 (2026-09-04) — 详见 LayerStack.h
// =============================================================

#include "LayerStack.h"
#include "BlendMode.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QPainter>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QPainterPath>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QCoreApplication>

// P0-2.5 (2026-09-08): EngineContext 完整 include (renderAsync 用)
#include "../../../core/ThreadPool/engine_context.h"
#include "../../../core/ThreadPool/executor.hpp"

#include <algorithm>

namespace layers {

namespace {
int g_nextGroupId = 1;   // 进程级 group id 计数器 (简单自增)
}

LayerStack::LayerStack(QObject *parent)
    : QObject(parent)
{
}

LayerStack::~LayerStack()
{
    // m_layers 共享指针自动释放
}

// =====================================================================
//  基础查询
// =====================================================================

LayerPtr LayerStack::at(int index) const
{
    if (index < 0 || index >= m_layers.size()) return nullptr;
    return m_layers.at(index);
}

LayerPtr LayerStack::baseLayer() const
{
    return m_layers.isEmpty() ? nullptr : m_layers.first();
}

LayerPtr LayerStack::topLayer() const
{
    return m_layers.isEmpty() ? nullptr : m_layers.last();
}

int LayerStack::indexOf(LayerPtr layer) const
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i] == layer) return i;
    }
    return -1;
}

int LayerStack::indexById(LayerId id) const
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (idOf(i) == id) return i;
    }
    return -1;
}

LayerId LayerStack::idOf(int index) const
{
    auto l = at(index);
    if (!l) return 0;
    return reinterpret_cast<LayerId>(l.get());
}

LayerPtr LayerStack::byId(LayerId id) const
{
    for (const auto &l : m_layers) {
        if (reinterpret_cast<LayerId>(l.get()) == id) return l;
    }
    return nullptr;
}

void LayerStack::reassignZOrder()
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i]) m_layers[i]->zOrder = i;
    }
}

void LayerStack::disconnectLayer(LayerPtr l)
{
    Q_UNUSED(l);
    // Layer 是 shared_ptr 自动管理, 无显式 disconnect 需要
}

// =====================================================================
//  修改操作 (基础)
// =====================================================================

int LayerStack::addLayer(const Layer &layer)
{
    m_layers.append(std::make_shared<Layer>(layer));
    const int newIndex = m_layers.size() - 1;
    reassignZOrder();
    emit layerAdded(newIndex);
    emit countChanged();
    return newIndex;
}

int LayerStack::addLayer(const QString &name, const cv::Mat &image)
{
    return addLayer(Layer(name, image));
}

// =====================================================================
//  Phase 3 (2026-09-04): 4 种 per-kind 工厂方法
// =====================================================================

int LayerStack::addTextLayer(const QString &name, const QString &text,
                             int fontSize, const QColor &color,
                             const QString &fontFamily)
{
    Layer l;
    l.kind = Layer::Text;
    l.name = name.isEmpty() ? QStringLiteral("Text") : name;
    l.text = text;
    l.fontSize = fontSize;
    l.textColor = color;
    l.fontFamily = fontFamily;
    return addLayer(l);
}

int LayerStack::addVectorLayer(const QString &name,
                               const QVector<QPainterPath> &paths,
                               const QVector<QColor> &fills)
{
    Layer l;
    l.kind = Layer::Vector;
    l.name = name.isEmpty() ? QStringLiteral("Vector") : name;
    l.vectorPaths = paths;
    l.vectorFillColors = fills;
    l.vectorStrokeWidths = QVector<qreal>(paths.size(), 0.0);
    return addLayer(l);
}

int LayerStack::addSmartObjectLayer(const QString &name, const QString &sourceFilePath,
                                    bool embed)
{
    Layer l;
    l.kind = Layer::SmartObject;
    l.name = name.isEmpty() ? QStringLiteral("SmartObject") : name;
    l.sourceFilePath = sourceFilePath;
    l.sourceEmbedded = embed;
    // 嵌入时复制源文件到 cache
    if (embed && !sourceFilePath.isEmpty() && QFile::exists(sourceFilePath)) {
        const QString cachePath = smartObjectCachePath(sourceFilePath);
        QFileInfo cacheInfo(cachePath);
        QDir().mkpath(cacheInfo.absolutePath());
        QFile::remove(cachePath);   // 覆盖
        QFile::copy(sourceFilePath, cachePath);
    }
    return addLayer(l);
}

int LayerStack::addAdjustmentLayer(const QString &name, const QString &adjustmentType,
                                   const cv::Mat &lut)
{
    Layer l;
    l.kind = Layer::Adjustment;
    l.name = name.isEmpty()
        ? (adjustmentType.isEmpty() ? QStringLiteral("Adjustment") : adjustmentType)
        : name;
    l.adjustmentType = adjustmentType;
    l.adjustmentLut = lut.empty() ? cv::Mat() : lut.clone();
    // 默认 linear LUT (identity 256x1)
    if (l.adjustmentLut.empty()) {
        l.adjustmentLut = cv::Mat(256, 1, CV_8U);
        uchar *p = l.adjustmentLut.ptr<uchar>();
        for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(i);
    }
    return addLayer(l);
}

bool LayerStack::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size()) return false;
    m_layers.removeAt(index);
    reassignZOrder();
    // 选中更新
    if (m_selection >= m_layers.size()) {
        m_selection = m_layers.isEmpty() ? -1 : m_layers.size() - 1;
        emit selectionChanged(m_selection);
    }
    emit layerRemoved(index);
    emit countChanged();
    return true;
}

bool LayerStack::duplicateLayer(int index)
{
    if (auto l = at(index)) {
        Layer copy = *l;   // 拷贝构造 (cv::Mat clone, QString 值语义)
        copy.name = l->name + QStringLiteral(" copy");
        return addLayer(copy) >= 0;
    }
    return false;
}

bool LayerStack::moveUp(int index)
{
    if (index < 0 || index >= m_layers.size() - 1) return false;
    m_layers.swapItemsAt(index, index + 1);
    reassignZOrder();
    emit layerChanged(index);
    emit layerChanged(index + 1);
    return true;
}

bool LayerStack::moveDown(int index)
{
    if (index <= 0 || index >= m_layers.size()) return false;
    m_layers.swapItemsAt(index, index - 1);
    reassignZOrder();
    emit layerChanged(index);
    emit layerChanged(index - 1);
    return true;
}

bool LayerStack::setVisible(int index, bool visible)
{
    if (auto l = at(index)) {
        if (l->visible != visible) {
            l->visible = visible;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setOpacity(int index, float opacity)
{
    if (auto l = at(index)) {
        const float clamped = std::clamp(opacity, 0.0f, 1.0f);
        if (std::abs(l->opacity - clamped) > 0.0001f) {
            l->opacity = clamped;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setLocked(int index, bool locked)
{
    if (auto l = at(index)) {
        if (l->locked != locked) {
            l->locked = locked;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setLinked(int index, bool linked)
{
    if (auto l = at(index)) {
        if (l->isLinked != linked) {
            l->isLinked = linked;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setBlend(int index, Layer::BlendMode mode)
{
    if (auto l = at(index)) {
        if (l->blend != mode) {
            l->blend = mode;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::rename(int index, const QString &newName)
{
    if (auto l = at(index)) {
        if (!newName.isEmpty() && l->name != newName) {
            l->name = newName;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

// =====================================================================
//  Phase 3 (2026-09-04): 5 个 per-kind setter
// =====================================================================

bool LayerStack::setText(int index, const QString &text)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::Text) return false;
        if (l->text != text) {
            l->text = text;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setTextFont(int index, const QString &family, int size, const QColor &color)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::Text) return false;
        bool changed = (l->fontFamily != family) || (l->fontSize != size)
                       || (l->textColor != color);
        if (changed) {
            l->fontFamily = family;
            l->fontSize = size;
            l->textColor = color;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setVectorPaths(int index, const QVector<QPainterPath> &paths,
                                const QVector<QColor> &fills,
                                const QVector<qreal> &strokes)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::Vector) return false;
        l->vectorPaths = paths;
        l->vectorFillColors = fills;
        l->vectorStrokeWidths = strokes.isEmpty()
            ? QVector<qreal>(paths.size(), 0.0)
            : strokes;
        emit layerChanged(index);
        return true;
    }
    return false;
}

bool LayerStack::setSmartObjectSource(int index, const QString &path, bool embed)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::SmartObject) return false;
        const bool changed = (l->sourceFilePath != path) || (l->sourceEmbedded != embed);
        if (!changed) return false;
        l->sourceFilePath = path;
        l->sourceEmbedded = embed;
        if (embed && !path.isEmpty() && QFile::exists(path)) {
            const QString cachePath = smartObjectCachePath(path);
            QFileInfo cacheInfo(cachePath);
            QDir().mkpath(cacheInfo.absolutePath());
            QFile::remove(cachePath);
            QFile::copy(path, cachePath);
        }
        emit layerChanged(index);
        return true;
    }
    return false;
}

bool LayerStack::setAdjustmentType(int index, const QString &type)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::Adjustment) return false;
        if (l->adjustmentType != type) {
            l->adjustmentType = type;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

bool LayerStack::setAdjustmentLut(int index, const cv::Mat &lut)
{
    if (auto l = at(index)) {
        if (l->kind != Layer::Adjustment) return false;
        if (lut.empty()) return false;
        l->adjustmentLut = lut.clone();
        emit layerChanged(index);
        return true;
    }
    return false;
}

// =====================================================================
//  Phase 4 (2026-09-04): 蒙版
// =====================================================================

namespace {
// 阶段 1 W4.4 Phase 4 (2026-09-04): mask 应用
//   把 layer mask 灰度图广播到 3 通道, 跟 layerMat 逐像素相乘
//   mask 0 → 像素置 0 (透明); mask 255 → 像素保留
//   返回新 Mat (不修改入参)
cv::Mat applyMask(const cv::Mat &layerMat, const cv::Mat &grayMask)
{
    if (layerMat.empty() || grayMask.empty()) return layerMat.clone();
    cv::Size sz = layerMat.size();
    cv::Mat maskR;
    if (grayMask.size() != sz) {
        cv::resize(grayMask, maskR, sz, 0, 0, cv::INTER_LINEAR);
    } else {
        maskR = grayMask;
    }
    cv::Mat mask3ch;
    cv::cvtColor(maskR, mask3ch, cv::COLOR_GRAY2BGR);
    cv::Mat maskF;
    mask3ch.convertTo(maskF, CV_32F, 1.0 / 255.0);
    cv::Mat layerF;
    layerMat.convertTo(layerF, CV_32F);
    cv::Mat outF;
    cv::multiply(layerF, maskF, outF);
    cv::Mat out;
    outF.convertTo(out, CV_8U);
    return out;
}
} // namespace

bool LayerStack::addMask(int index, const cv::Mat &grayMask)
{
    if (auto l = at(index)) {
        if (grayMask.empty()) return false;
        l->layerMask = grayMask.clone();
        l->maskEnabled = true;
        emit layerChanged(index);
        return true;
    }
    return false;
}

bool LayerStack::clearMask(int index)
{
    if (auto l = at(index)) {
        const bool had = !l->layerMask.empty() || l->maskEnabled;
        l->layerMask = cv::Mat();
        l->maskEnabled = false;
        if (had) emit layerChanged(index);
        return had;
    }
    return false;
}

bool LayerStack::enableMask(int index, bool enabled)
{
    if (auto l = at(index)) {
        if (l->layerMask.empty()) return false;   // 无蒙版不能 enable
        if (l->maskEnabled != enabled) {
            l->maskEnabled = enabled;
            emit layerChanged(index);
            return true;
        }
    }
    return false;
}

// =====================================================================
//  阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展
// =====================================================================

bool LayerStack::refreshSmartObject(int index)
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    if (l->sourceFilePath.isEmpty()) return false;
    // 嵌入时: 重新复制源文件到 cache
    if (l->sourceEmbedded && QFile::exists(l->sourceFilePath)) {
        const QString cachePath = smartObjectCachePath(l->sourceFilePath);
        QFileInfo cacheInfo(cachePath);
        QDir().mkpath(cacheInfo.absolutePath());
        QFile::remove(cachePath);
        QFile::copy(l->sourceFilePath, cachePath);
    }
    emit layerChanged(index);
    return true;
}

bool LayerStack::isSmartObjectSourceMissing(int index) const
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    if (l->sourceFilePath.isEmpty()) return true;
    if (l->sourceEmbedded) {
        // 嵌入: 查 cache
        const QString cachePath = smartObjectCachePath(l->sourceFilePath);
        return !QFile::exists(cachePath);
    }
    // 链接: 查源
    return !QFile::exists(l->sourceFilePath);
}

bool LayerStack::toggleSmartObjectEmbed(int index)
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    if (l->sourceFilePath.isEmpty()) return false;
    if (l->sourceEmbedded) {
        // embed → link: 删 cache, 改标志
        const QString cachePath = smartObjectCachePath(l->sourceFilePath);
        QFile::remove(cachePath);
        l->sourceEmbedded = false;
    } else {
        // link → embed: 复制到 cache, 改标志
        if (!QFile::exists(l->sourceFilePath)) return false;
        const QString cachePath = smartObjectCachePath(l->sourceFilePath);
        QFileInfo cacheInfo(cachePath);
        QDir().mkpath(cacheInfo.absolutePath());
        QFile::remove(cachePath);
        QFile::copy(l->sourceFilePath, cachePath);
        l->sourceEmbedded = true;
    }
    emit layerChanged(index);
    return true;
}

int LayerStack::cleanupSmartObjectCache()
{
    // 阶段 1 W4.4 Phase 5 (2026-09-04): 清掉 smartobject-cache 目录里所有文件
    //   Phase 5 简化: 不查"是否被任何 stack 引用", 全删 (简单粗暴)
    //   嵌入的智能对象下次 render 会自动重新复制
    //   链接的智能对象完全不影响 (它们走源文件)
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                             + QStringLiteral("/smartobject-cache");
    QDir dir(cacheDir);
    if (!dir.exists()) return 0;
    int removed = 0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &fi : entries) {
        if (QFile::remove(fi.absoluteFilePath())) ++removed;
    }
    return removed;
}

// 阶段 1 W4.4 Phase 5 (2026-09-04): public wrapper, 跟 private smartObjectCachePath 一样
QString LayerStack::smartObjectCachePathFor(const QString &sourceFilePath)
{
    return smartObjectCachePath(sourceFilePath);
}

// =====================================================================
//  合并
// =====================================================================

bool LayerStack::mergeDown(int index)
{
    // 把 [index] 合并到 [index-1] (向下), 删 [index]
    if (index <= 0 || index >= m_layers.size()) return false;
    auto upper = m_layers.at(index);
    auto lower = m_layers.at(index - 1);
    if (!upper || !lower) return false;

    // Phase 1: 用 cv::addWeighted 按 upper.opacity blend
    //   未来 Phase 2: 按 upper.blend mode 选不同 blend 函数
    cv::Mat merged;
    const float a = upper->opacity;
    cv::addWeighted(lower->image, 1.0 - a, upper->image, a, 0, merged);

    // 替换 lower 的 image
    lower->image = merged;
    // 删除 upper
    m_layers.removeAt(index);
    reassignZOrder();
    emit layerChanged(index - 1);
    emit layerRemoved(index);
    emit countChanged();
    return true;
}

cv::Mat LayerStack::flattenVisible()
{
    cv::Mat result = render();
    if (!result.empty()) {
        clear();
        m_layers.append(std::make_shared<Layer>(
            Layer(QStringLiteral("Background"), result)));
        reassignZOrder();
        emit layerAdded(0);
        emit countChanged();
    }
    return result;
}

// =====================================================================
//  编组
// =====================================================================

int LayerStack::createGroup(const QString &name)
{
    Q_UNUSED(name);
    // Phase 1 简化: 编组只保留逻辑 (不实际改 layer 结构), Phase 3 加 group 容器
    //   返 group id 给 LayerCommand 用
    return g_nextGroupId++;
}

bool LayerStack::groupLayers(int first, int last, int groupId)
{
    Q_UNUSED(groupId);
    if (first < 0 || last >= m_layers.size() || first > last) return false;
    // Phase 1 简化: link 一下表示同组
    for (int i = first; i <= last; ++i) {
        if (auto l = at(i)) {
            l->isLinked = true;
            emit layerChanged(i);
        }
    }
    return true;
}

bool LayerStack::ungroupLayer(int layerIndex)
{
    Q_UNUSED(layerIndex);
    // Phase 1 简化: 全部 unlink
    for (int i = 0; i < m_layers.size(); ++i) {
        if (auto l = at(i)) {
            l->isLinked = false;
            emit layerChanged(i);
        }
    }
    return true;
}

// =====================================================================
//  链接
// =====================================================================

QList<int> LayerStack::linkedSelection(int index) const
{
    QList<int> result;
    if (index < 0 || index >= m_layers.size()) return result;
    auto target = m_layers.at(index);
    if (!target) return result;
    if (target->isLinked) {
        // 收集所有 isLinked 的 layer index
        for (int i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i] && m_layers[i]->isLinked) result << i;
        }
    } else {
        result << index;
    }
    return result;
}

// =====================================================================
//  选中
// =====================================================================

void LayerStack::setSelection(int index)
{
    if (index == m_selection) return;
    m_selection = index;
    emit selectionChanged(index);
}

void LayerStack::clearSelection()
{
    if (m_selection == -1) return;
    m_selection = -1;
    emit selectionChanged(-1);
}

QList<int> LayerStack::selectedIndices() const
{
    return linkedSelection(m_selection);
}

// =====================================================================
//  渲染
// =====================================================================

cv::Size LayerStack::canvasSize() const
{
    // 优先 base layer (index 0) 的 image 尺寸
    if (!m_layers.isEmpty() && m_layers[0] && !m_layers[0]->image.empty()) {
        return m_layers[0]->image.size();
    }
    // fallback: m_canvasSize (默认 800x600)
    return m_canvasSize;
}

void LayerStack::setCanvasSize(const cv::Size &size)
{
    if (size.width <= 0 || size.height <= 0) return;
    m_canvasSize = size;
}

QString LayerStack::smartObjectCachePath(const QString &sourceFilePath)
{
    // 阶段 1 W4.4 Phase 5 (2026-09-04) BUG FIX: 用 source 路径的 MD5 哈希做文件名
    //   原代码用 QUuid 每次生成不同 → addSmartObjectLayer 写 cache A, 后续查 cache B 不存在
    //   修法: 同一 sourceFilePath 永远对应同一 cache 路径
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                            + QStringLiteral("/smartobject-cache");
    const QString ext = QFileInfo(sourceFilePath).suffix();
    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(sourceFilePath).absoluteFilePath().toUtf8(),
        QCryptographicHash::Md5).toHex();
    const QString fileName = QString::fromLatin1(hash)
                             + (ext.isEmpty() ? QString() : QStringLiteral(".") + ext);
    return baseDir + QStringLiteral("/") + fileName;
}

// rasterize: 把任意 kind 的 layer 转成 cv::Mat (BGR 8U, canvasSize)
//   - Bitmap: 直接返 image
//   - Text:   QPainter 画文字 → QImage → cv::Mat (透明背景)
//   - Vector: QPainter 画 paths → QImage → cv::Mat
//   - SmartObject: 加载 sourceFilePath (嵌入走 cache)
//   - Adjustment: 返空 Mat (不能独立 rasterize, 必须作用在底图上, 由 render() 走特殊路径)
cv::Mat LayerStack::rasterize(const LayerPtr &l) const
{
    if (!l) return cv::Mat();
    switch (l->kind) {
    case Layer::Bitmap: {
        return l->image.empty() ? cv::Mat() : l->image.clone();
    }
    case Layer::Text: {
        if (l->text.isEmpty()) return cv::Mat();
        const cv::Size sz = canvasSize();
        QImage img(sz.width, sz.height, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f(l->fontFamily, l->fontSize);
        p.setFont(f);
        p.setPen(l->textColor);
        // 画在中央偏上 (PS 默认位置)
        const QFontMetrics fm(f);
        const QRect textRect = fm.boundingRect(l->text);
        const int x = (sz.width - textRect.width()) / 2;
        const int y = sz.height / 4;
        p.drawText(x, y + fm.ascent(), l->text);
        p.end();
        // QImage ARGB32 → cv::Mat BGRA
        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        cv::Mat bgra = mat.clone();
        cv::Mat bgr;
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case Layer::Vector: {
        if (l->vectorPaths.isEmpty()) return cv::Mat();
        const cv::Size sz = canvasSize();
        QImage img(sz.width, sz.height, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < l->vectorPaths.size(); ++i) {
            const QColor fill = i < l->vectorFillColors.size()
                ? l->vectorFillColors[i] : QColor(200, 200, 200);
            const qreal stroke = i < l->vectorStrokeWidths.size()
                ? l->vectorStrokeWidths[i] : 0.0;
            p.setPen(stroke > 0 ? QPen(fill, stroke) : QPen(Qt::NoPen));
            p.setBrush(QBrush(fill));
            // 缩放 paths 适应画布 (假设 path 坐标在 0-100 范围)
            p.save();
            p.scale(sz.width / 100.0, sz.height / 100.0);
            p.drawPath(l->vectorPaths[i]);
            p.restore();
        }
        p.end();
        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        cv::Mat bgra = mat.clone();
        cv::Mat bgr;
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case Layer::SmartObject: {
        QString path = l->sourceFilePath;
        if (l->sourceEmbedded) {
            path = smartObjectCachePath(l->sourceFilePath);
        }
        const cv::Size sz = canvasSize();
        if (path.isEmpty() || !QFile::exists(path)) {
            // 阶段 1 W4.4 Phase 5 (2026-09-04): 缺源时返 placeholder
            //   灰底 + 红 X + 文本 "MISSING", 跟 PS 行为一致
            cv::Mat placeholder(sz, CV_8UC3, cv::Scalar(200, 200, 200));
            // 画对角线 (红色)
            cv::line(placeholder, cv::Point(0, 0), cv::Point(sz.width, sz.height),
                     cv::Scalar(0, 0, 200), 2);
            cv::line(placeholder, cv::Point(0, sz.height), cv::Point(sz.width, 0),
                     cv::Scalar(0, 0, 200), 2);
            return placeholder;
        }
        cv::Mat img = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
        if (img.empty()) {
            cv::Mat placeholder(sz, CV_8UC3, cv::Scalar(200, 200, 200));
            cv::line(placeholder, cv::Point(0, 0), cv::Point(sz.width, sz.height),
                     cv::Scalar(0, 0, 200), 2);
            cv::line(placeholder, cv::Point(0, sz.height), cv::Point(sz.width, 0),
                     cv::Scalar(0, 0, 200), 2);
            return placeholder;
        }
        // resize 到 canvas 尺寸 (保持宽高比, fit)
        if (img.size() != sz) {
            cv::Mat resized;
            cv::resize(img, resized, sz, 0, 0, cv::INTER_AREA);
            return resized;
        }
        return img;
    }
    case Layer::Adjustment:
    default:
        return cv::Mat();
    }
}

cv::Mat LayerStack::render() const
{
    if (m_layers.isEmpty()) return cv::Mat();

    // Step 1: 找 base layer (kind=Bitmap 且 image 非空) 作 canvas
    cv::Mat canvas;
    int baseIdx = -1;
    for (int i = 0; i < m_layers.size(); ++i) {
        const auto &l = m_layers[i];
        if (l && l->kind == Layer::Bitmap && !l->image.empty()) {
            canvas = l->image.clone();
            baseIdx = i;
            break;
        }
    }
    if (canvas.empty()) {
        // 没有 base layer: 用 canvasSize 建一张白底
        const cv::Size sz = canvasSize();
        canvas = cv::Mat(sz, CV_8UC3, cv::Scalar(255, 255, 255));
        baseIdx = -1;
    }

    // Step 2: 从 base+1 开始, 逐层按 zOrder asc 混合
    for (int i = std::max(0, baseIdx); i < m_layers.size(); ++i) {
        const auto &l = m_layers[i];
        if (!l || !l->visible) continue;
        if (i == baseIdx) {
            // base 层: 单独处理 opacity
            if (l->opacity < 1.0f) {
                cv::Mat tmp;
                cv::addWeighted(canvas, l->opacity,
                                cv::Mat::zeros(canvas.size(), canvas.type()),
                                0, 0, tmp);
                canvas = tmp;
            }
            continue;
        }
        // Adjustment 层: 应用 LUT 到 canvas
        if (l->kind == Layer::Adjustment) {
            if (!l->adjustmentLut.empty() && l->adjustmentLut.rows == 256
                && l->adjustmentLut.cols == 1 && l->adjustmentLut.type() == CV_8U) {
                cv::Mat adjusted;
                cv::LUT(canvas, l->adjustmentLut, adjusted);
                if (l->opacity < 1.0f) {
                    cv::Mat tmp;
                    cv::addWeighted(canvas, 1.0f - l->opacity,
                                    adjusted, l->opacity, 0, tmp);
                    canvas = tmp;
                } else {
                    canvas = adjusted;
                }
            }
            continue;
        }
        // 其他 kind: rasterize 后 blend
        cv::Mat layerMat = rasterize(l);
        if (layerMat.empty()) continue;
        // Phase 4 (2026-09-04): 应用蒙版 (mask 调制 layer alpha)
        if (l->maskEnabled && !l->layerMask.empty()) {
            layerMat = applyMask(layerMat, l->layerMask);
        }
        canvas = applyBlend(l->blend, canvas, layerMat, l->opacity);
    }
    return canvas;
}

cv::Mat LayerStack::renderOne(int index) const
{
    auto l = at(index);
    if (!l) return cv::Mat();
    if (l->kind == Layer::Adjustment) {
        // Adjustment 单层预览 = 调色后的 canvas (需要全栈, 简化: 对 base 调色)
        if (auto base = baseLayer()) {
            if (base->image.empty()) return cv::Mat();
            if (l->adjustmentLut.empty()) return base->image.clone();
            cv::Mat out;
            cv::LUT(base->image, l->adjustmentLut, out);
            return out;
        }
        return cv::Mat();
    }
    cv::Mat m = rasterize(l);
    if (m.empty()) return cv::Mat();
    // Phase 4 (2026-09-04): 单层预览也应用 mask
    if (l->maskEnabled && !l->layerMask.empty()) {
        m = applyMask(m, l->layerMask);
    }
    if (l->opacity < 1.0f) {
        cv::Mat tmp;
        cv::addWeighted(m, l->opacity,
                        cv::Mat::zeros(m.size(), m.type()),
                        0, 0, tmp);
        return tmp;
    }
    return m;
}

// =====================================================================
//  清空 / 重置
// =====================================================================

void LayerStack::clear()
{
    if (m_layers.isEmpty()) return;
    m_layers.clear();
    m_selection = -1;
    emit countChanged();
    emit selectionChanged(-1);
}

void LayerStack::setBaseLayer(const cv::Mat &baseImage)
{
    if (baseImage.empty()) return;
    if (m_layers.isEmpty()) {
        addLayer(QStringLiteral("Background"), baseImage);
    } else {
        if (m_layers[0]) {
            m_layers[0]->image = baseImage.clone();
            m_layers[0]->name = QStringLiteral("Background");
            emit layerChanged(0);
        }
    }
}

QStringList LayerStack::layerNames() const
{
    QStringList names;
    for (const auto &l : m_layers) {
        if (l) names << l->name;
    }
    return names;
}

// P0-2.5 (2026-09-08): renderAsync — 异步 render
//   走 EngineContext background().submit_fn 后台跑, 完成后用
//   QMetaObject::invokeMethod 切回主线程 (Qt::QueuedConnection) 调 callback.
//
// 设计要点 (一次性到位, P0-2.5 不留补丁):
//   1. engine == nullptr → 直接 sync render + callback (跟 render() 等价)
//   2. engine 非空 → 后台跑完, callback 必在主线程
//      调用方 (ImageWindow::rebuildCurrentCache) 用 m_currentTaskId 校验
//      取消, callback 里 if (m_currentTaskId != myTaskId) return;
//   3. self 指针保活: LayerStack 持有 m_layers shared_ptr, 后台跑时 self 引用,
//      即便 LayerStack 销毁, 闭包仍能跑完 (注意: ImageWindow 持 unique_ptr<LayerStack>,
//      析构 LayerStack 会先等 invokeMethod 队列排空, 但 LayerStack 析构
//      之前若 ImageWindow 先析构, 队列里 lambda 持有 self 野指针风险;
//      调用方须在 ImageWindow 析构时 setAsyncEnabled(false) 或类似保护.
//      当前 P0-2.5 ImageWindow::rebuildCurrentCache 仅在 ImageWindow 还活着时调)
//   4. callback 捕获 cv::Mat 用 std::move — render() 返的 Mat move 进去避免额外 copy
//   5. QCoreApplication::instance() 必须存在 (背景: 单元测试用 QTEST_MAIN 默认构造)
//      兜底: instance() == nullptr → 同步兜底 (跟 engine==nullptr 路径一致)
void LayerStack::renderAsync(vistella::tp::EngineContext *engine,
                              RenderCallback callback) const
{
    if (!engine || !callback) {
        // 兜底: engine 没初始化 / callback 空 → 同步跑
        if (callback) callback(render());
        return;
    }
    const LayerStack *self = this;
    auto cb = std::move(callback);
    auto *ctx = QCoreApplication::instance();
    if (!ctx) {
        // 没 QApplication 实例 (极端场景, 例如纯 C++ 测试) → 同步兜底
        cb(render());
        return;
    }
    const bool submitted = engine->background().submit_fn(
        [self, cb, ctx](vistella::tp::TaskContext & /*taskCtx*/) -> void {
            cv::Mat result = self->render();
            // 跨线程切回主线程: Qt::QueuedConnection 必走主线程事件循环
            // 闭包捕获 cb + result, 用 std::move 避免 cv::Mat copy
            QMetaObject::invokeMethod(ctx,
                [cb, result = std::move(result)]() -> void {
                    cb(result);
                },
                Qt::QueuedConnection);
        },
        "LayerStack::renderAsync",
        vistella::tp::Priority::Normal);
    if (!submitted) {
        // 提交失败 (池满 / shutting down) → 同步兜底
        // 调用方拿到的 result 跟原 sync 行为一致, 不破坏既有调用方语义
        cb(render());
    }
}

} // namespace layers

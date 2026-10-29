// =============================================================
// LayerStack 实现
//  详见 LayerStack.h
// =============================================================

#include "LayerStack.h"

#include "LayerMaskRenderer.h"
#include "PixelFormat.h"
#include "BlendMode.h"

// compositor-backed render path
#include "../../compositor/TileGrid.h"
#include "../../compositor/RenderGraph.h"
#include "../../compositor/nodes/LayerStackAdapter.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QPainter>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QPainterPath>
#include <QStandardPaths>

#include "logger.h"
#include <QDir>
#include <QUuid>
#include <QCryptographicHash>
#include <QDateTime>
#include <QMetaObject>
#include <QCoreApplication>

// EngineContext 完整 include (renderAsync 用)
#include "../../../core/ThreadPool/engine_context.h"
#include "../../../core/ThreadPool/executor.hpp"

#include <algorithm>
#include <atomic>

namespace layers {

namespace {
int g_nextGroupId = 1;   // 旧逻辑 group id 计数器 (未使用)

// 图层稳定 id 分配器 (LYR-06/12/16).
//   全局单调递增, 跨 LayerStack 也唯一 —— 这样把图层快照搬到另一个栈
//   不会撞 id. 0 保留给"还没分配".
std::atomic<quint64> g_nextStableId{1};
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
//  SmartFilter dispatcher (5 hardcoded filters inlined)
// =====================================================================
//   Apply a single filter to a cv::Mat and return the result.
//   Supported: "GaussianBlur" / "Sharpen" / "Brightness" / "Contrast" /
//   "Emboss". FilterStrategy/FilterFactory integration is deferred to P1.4.6
//   to avoid FeatureFilterKind -> algorithm dispatch complexity. strength in 0..2.
namespace {
cv::Mat applySmartFilter(const QString &filterType, double strength, const cv::Mat &in)
{
    if (in.empty() || filterType.isEmpty()) return cv::Mat();
    if (filterType == QStringLiteral("GaussianBlur")) {
        // strength 1.0 -> 5x5, 0.0 -> 1x1 (no-op), 2.0 -> 9x9
        int ksize = std::max(1, static_cast<int>(std::lround(1.0 + strength * 4.0)));
        if (ksize % 2 == 0) ++ksize;
        cv::Mat out;
        cv::GaussianBlur(in, out, cv::Size(ksize, ksize), 0, 0);
        return out;
    }
    if (filterType == QStringLiteral("Sharpen")) {
        // strength 1.0 -> mild sharpen (center 5, surrounding -1). 0..2 scales.
        cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
            0.0f, -1.0f, 0.0f,
           -1.0f,  5.0f, -1.0f,
            0.0f, -1.0f, 0.0f);
        const float s = static_cast<float>(strength);
        const float center = 1.0f + 4.0f * s;
        kernel.at<float>(1, 1) = center;
        cv::Mat out;
        cv::filter2D(in, out, -1, kernel);
        return out;
    }
    if (filterType == QStringLiteral("Brightness")) {
        // strength 1.0 -> no change, 0.0 -> black, 2.0 -> ~2x brighter
        const double alpha = strength;
        const double beta = (1.0 - strength) * 128.0;
        cv::Mat out;
        in.convertTo(out, -1, alpha, beta);
        return out;
    }
    if (filterType == QStringLiteral("Contrast")) {
        cv::Mat out;
        in.convertTo(out, -1, strength, 0);
        return out;
    }
    if (filterType == QStringLiteral("Emboss")) {
        cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
           -2.0f, -1.0f, 0.0f,
           -1.0f,  1.0f, 1.0f,
            0.0f,  1.0f, 2.0f);
        cv::Mat out;
        cv::filter2D(in, out, -1, kernel);
        return out;
    }
    return in.clone();
}
} // anonymous namespace

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
    return l->stableId;
}

LayerPtr LayerStack::byId(LayerId id) const
{
    if (id == 0) return nullptr;
    for (const auto &l : m_layers) {
        if (l && l->stableId == id) return l;
    }
    return nullptr;
}

// 给还没分配 stableId 的图层发一个.
//   值拷贝带过来的 id 原样保留 —— 撤销时把快照放回去, 身份不变.
void LayerStack::assignStableId(Layer &layer)
{
    if (layer.stableId != 0) return;
    layer.stableId = g_nextStableId.fetch_add(1, std::memory_order_relaxed);
}

// 把某个 id 从所有智能滤镜链里摘掉 (删图层时调用).
//   链上存的是 id, 删掉一层以后链里不能留下查不到的条目 (LYR-16).
void LayerStack::dropFromSmartFilterChains(LayerId id)
{
    if (id == 0) return;
    for (auto it = m_smartFilterChain.begin(); it != m_smartFilterChain.end(); ++it) {
        it.value().removeAll(id);
    }
    // 顺手清掉空链
    for (auto it = m_smartFilterChain.begin(); it != m_smartFilterChain.end(); ) {
        if (it.value().isEmpty()) {
            it = m_smartFilterChain.erase(it);
        } else {
            ++it;
        }
    }
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
    return appendLayerPtr(std::make_shared<Layer>(layer));
}

// append an already-allocated layer, keeping ITS
// address. addLayer(const Layer&) necessarily allocates a fresh LayerPtr,
// which would break m_groups lookups keyed on that address — so anything
// that has to preserve a Group's identity (duplicateLayer) goes through
// here instead.
// append an already-allocated layer.
//   addLayer(const Layer&) 一定要新分配一个 LayerPtr, 所以 anything
//   that has to preserve a Group's identity (duplicateLayer) goes
//   through here instead.
int LayerStack::appendLayerPtr(LayerPtr layer)
{
    if (!layer) return -1;
    // id 冲突保护: 同一个 id 同时活在 m_layers 里, 按 id 定位的撤销命令
    //   / 智能滤镜链就会认错层. 正常路径不会撞 (id 来自唯一分配器),
    //   这里兜底.
    if (layer->stableId != 0 && indexById(layer->stableId) >= 0) {
        layer->stableId = 0;
    }
    assignStableId(*layer);
    // Pixel contract: the layers in the stack are always 4-channel BGRA
    // with straight alpha (LYR-01 / LYR-35).
    //   A 3-channel layer created outside is normalised here, so the
    //   render layer only ever sees one format.
    layer->image = toBgra(layer->image);
    m_layers.append(std::move(layer));
    const int newIndex = m_layers.size() - 1;
    reassignZOrder();
    emit layerAdded(newIndex);
    emit countChanged();
    return newIndex;
}

int LayerStack::addLayer(const QString &name, const cv::Mat &image)
{
    // 第一层定义文档画布尺寸 (LYR-05):
    //   之后就算这层被编进组里, canvasSize() 也还有文档级的值可用.
    if (m_layers.isEmpty() && !image.empty()) {
        m_canvasSize = image.size();
    }
    return addLayer(Layer(name, image));
}

//  在指定位置插入图层.
//    addLayer 只能追加到末尾; 撤销一次"删除中间图层"时必须把图层放回
//    原来的位置, 否则撤销之后层序就变了.
int LayerStack::insertLayer(int index, const Layer &layer)
{
    if (index < 0 || index > m_layers.size()) return -1;
    auto l = std::make_shared<Layer>(layer);
    // id 冲突保护 (见 appendLayerPtr)
    if (l->stableId != 0 && indexById(l->stableId) >= 0) {
        l->stableId = 0;
    }
    assignStableId(*l);
    l->image = toBgra(l->image);
    m_layers.insert(index, l);
    reassignZOrder();
    emit layerAdded(index);
    emit countChanged();
    return index;
}
//  补发 layerChanged.
//    setter 路径自带 emit; 直接改 Layer 字段的路径 (command 还原快照 /
//    蒙版恢复 / 类型转换还原) 绕过 setter, 用这个让宿主知道内容变了.
void LayerStack::notifyLayerChanged(int index)
{
    emit layerChanged(index);
}

// =====================================================================
//  4 种 per-kind 工厂方法
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

// clone a layer, keeping its stable identity.
//
//   m_groups 按 Group 的 stableId 存 children. 拷贝出来的图层 stableId
//   原样保留, 所以编组时被编走的原层和它的克隆是同一个身份 ——
//   m_groups 的条目不用搬家, 嵌套编组也不会把内组的子层弄丢.
//
//   语义上"复制出新图层"的地方 (duplicateLayer) 要另外发新 id,
//   并把 children 条目复制到新 id 下.
LayerPtr LayerStack::cloneLayerWithGroupChildren(const LayerPtr& src)
{
    if (!src) return LayerPtr();
    return std::make_shared<Layer>(*src);
}

// drop a group layer's children entry when the layer
//   itself goes away. Without this, removeLayer() left the vector of
//   child LayerPtrs alive in m_groups forever.
void LayerStack::releaseGroupChildren(const LayerPtr& group)
{
    if (!group) return;
    m_groups.remove(group->stableId);
}
//   2. Capture each layer's full copy (LayerPtr deep clone via std::make_shared<Layer>).
//   3. Remove originals (high idx to low idx to avoid index shift).
//   4. Build Group layer (kind=Group) + store clones in m_groups[newGroupIdx].
//   5. Return the new Group index (the group lands at the
//      original position of the topmost selected layer, PS behaviour).
int LayerStack::mergeIntoGroup(const QList<int>& indices)
{
    if (indices.size() < 2) return -1;
    // Validate bounds
    for (int idx : indices) {
        if (idx < 0 || idx >= m_layers.size()) return -1;
    }
    // Sort ascending for contiguity check + removal order
    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    // Contiguity: consecutive integers
    for (int i = 1; i < sorted.size(); ++i) {
        if (sorted[i] != sorted[i - 1] + 1) return -1;
    }
    // Capture clones BEFORE removing originals (so indices stay valid).
    //   cloneLayerWithGroupChildren keeps the stableId, so the clone and
    //   the original it replaces are the same identity.
    std::vector<LayerPtr> clones;
    clones.reserve(sorted.size());
    // Group children live in m_groups keyed by the Group's stableId. The
    //   clone keeps that id, but removing the original releases the entry
    //   -- so snapshot the entries and put them back after the removal.
    std::vector<LayerId> groupIds;
    std::vector<std::vector<LayerPtr>> groupChildren;
    for (int idx : sorted) {
        clones.push_back(cloneLayerWithGroupChildren(m_layers[idx]));
        if (m_layers[idx] && m_layers[idx]->kind == Layer::Group) {
            const LayerId gid = m_layers[idx]->stableId;
            auto it = m_groups.constFind(gid);
            if (it != m_groups.constEnd() && !it.value().empty()) {
                groupIds.push_back(gid);
                groupChildren.push_back(it.value());
            }
        }
    }
    // Remove originals in reverse order (high -> low) to keep indices stable
    for (int i = sorted.size() - 1; i >= 0; --i) {
        if (!removeLayer(sorted[i])) return -1;   // shouldn't fail given validation
    }
    // 还原刚被 removeLayer 释放掉的编组子层条目
    for (size_t i = 0; i < groupIds.size(); ++i) {
        m_groups.insert(groupIds[i], groupChildren[i]);
    }
    // Build Group layer
    Layer grp;
    grp.kind = Layer::Group;
    grp.name = QStringLiteral("Group 1");
    // The group lands at the ORIGINAL POSITION of the topmost
    // selected layer (PS behaviour).
    //   The insert point is the number of UNSELECTED layers below it --
    //   the selected layers have already been removed, the unselected
    //   ones are still in place.
    const int topSelected = sorted.back();
    int insertAt = 0;
    for (int i = 0; i < topSelected; ++i) {
        if (!sorted.contains(i)) ++insertAt;
    }
    const int groupIdx = insertLayer(insertAt, grp);
    if (groupIdx < 0) return -1;
    // key m_groups by the Group's stableId, not int layer index
    //   so subsequent m_layers mutations (moveUp / removeLayer / flattenGroup)
    //   don't invalidate this lookup.
    m_groups.insert(idOf(groupIdx), std::move(clones));
    return groupIdx;
}

// Replace Group at groupIdx with its children in order
//   1. Validate: index in range, kind == Group.
//   2. Snapshot children (move m_groups[LayerId] to local vector).
//   3. Rebuild m_layers: insert children at groupIdx, drop Group entry.
//   4. emit countChanged + layerAdded signals.
//   m_groups keyed by LayerId (stable), so we
//   look up by LayerId and DON'T need to re-key other entries after flatten.
//   Returns number of children inserted, or -1 on failure.
int LayerStack::flattenGroup(int groupIdx)
{
    if (groupIdx < 0 || groupIdx >= m_layers.size()) return -1;
    auto grp = m_layers[groupIdx];
    if (!grp || grp->kind != Layer::Group) return -1;
    const LayerId gid = idOf(groupIdx);
    auto it = m_groups.find(gid);
    std::vector<LayerPtr> children;
    if (it != m_groups.end()) {
        children = std::move(it.value());
        m_groups.erase(it);
    }
    if (children.empty()) {
        if (!removeLayer(groupIdx)) return -1;
        return 0;
    }
    // Rebuild m_layers: insert children at groupIdx, drop Group entry
    QList<LayerPtr> rebuilt;
    rebuilt.reserve(m_layers.size() - 1 + static_cast<int>(children.size()));
    for (int i = 0; i < m_layers.size(); ++i) {
        if (i < groupIdx) rebuilt.append(m_layers[i]);
        else if (i == groupIdx) {
            for (auto& ch : children) rebuilt.append(ch);
        } else {
            rebuilt.append(m_layers[i]);
        }
    }
    m_layers = rebuilt;
    emit countChanged();
    for (size_t i = 0; i < children.size(); ++i) {
        emit layerAdded(groupIdx + static_cast<int>(i));
    }
    return static_cast<int>(children.size());
}

const std::vector<LayerPtr>& LayerStack::groupChildrenOf(LayerId groupId) const
{
    static const std::vector<LayerPtr> kEmpty;
    auto it = m_groups.find(groupId);
    if (it == m_groups.end()) return kEmpty;
    return it.value();
}

const std::vector<LayerPtr>& LayerStack::groupChildrenOf(const LayerPtr& group) const
{
    if (!group) {
        static const std::vector<LayerPtr> kEmpty;
        return kEmpty;
    }
    // 用 stableId 查 (不是 LayerPtr 地址): 地址会随值拷贝漂移,
    //   编组 / 复制以后按旧地址就查不到子层了 (LYR-12).
    return groupChildrenOf(group->stableId);
}

// inverse of flattenGroup, used by LayerCommand
//   to undo a flatten. Builds a fresh Group layer, inserts it at atIndex,
//   and registers the cloned children under the new Group's LayerId.
//   Caller is responsible for capturing the children snapshot before
//   calling flattenGroup (typically via std::move on groupChildrenOf).
int LayerStack::reinsertGroup(int atIndex, std::vector<LayerPtr> children)
{
    if (atIndex < 0 || atIndex > m_layers.size()) return -1;
    Layer grp;
    grp.kind = Layer::Group;
    grp.name = QStringLiteral("Group 1");
    // 走 insertLayer: 新 Group 要领一个 stableId.
    //   (直接 m_layers.insert 会漏掉 id 分配, 组的身份就成了 0)
    const int newGroupIdx = insertLayer(atIndex, grp);
    if (newGroupIdx < 0) return -1;
    // Wire children into m_groups keyed by new Group's stableId.
    m_groups.insert(idOf(newGroupIdx), std::move(children));
    return newGroupIdx;
}

bool LayerStack::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size()) return false;
    // capture the LayerPtr BEFORE removal. Reading
    //   m_layers[index] after removeAt() is out of range whenever index
    //   was the last element — which is exactly what happens when
    //   mergeIntoGroup() removes its originals from the top down.
    LayerPtr removed = m_layers.at(index);
    m_layers.removeAt(index);
    // A removed Group's children entry used to stay in m_groups forever,
    // keeping its child LayerPtrs alive.
    releaseGroupChildren(removed);
    // 被删掉的层如果挂在智能滤镜链上, 链上的条目也要摘掉 (LYR-16).
    //   链上存的是 id, 不摘的话链里会留下一个查不到的条目, 渲染时
    //   智能滤镜会挂到别的图层上.
    dropFromSmartFilterChains(removed->stableId);
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
    auto l = at(index);
    if (!l) return false;
    Layer copy = *l;   // 拷贝构造 (cv::Mat clone, QString 值语义)
    copy.name = l->name + QStringLiteral(" copy");
    // 复制出来的是**另一个**图层, 必须领新的 stableId (LYR-06/16):
    //   否则它跟原件共享身份, 撤销命令 / 编组表 / 智能滤镜链会认错层.
    copy.stableId = 0;   // 0 = 待分配, 入栈时领新 id
    if (l->kind == Layer::Group) {
        // Group 的 children 条目按 Group 的 id 存, 换 id 后要跟着搬一份.
        const auto children = groupChildrenOf(l->stableId);
        auto clone = std::make_shared<Layer>(copy);
        const int newIdx = appendLayerPtr(std::move(clone));
        if (newIdx < 0) return false;
        if (!children.empty()) {
            m_groups.insert(at(newIdx)->stableId, children);
        }
        return true;
    }
    return addLayer(copy) >= 0;
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
//  5 个 per-kind setter
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
//  蒙版
// =====================================================================

// =====================================================================
//  扩展 LayerMask struct 接口
// =====================================================================

bool LayerStack::addPixelMask(int index, const cv::Mat& grayMask)
{
    auto l = at(index);
    if (!l || grayMask.empty()) return false;
    l->mask.kind = LayerMask::Pixel;
    l->mask.pixel = grayMask.clone();
    l->mask.enabled = true;
    l->mask.density = 1.0;
    l->mask.feather = 0.0;
    l->mask.invert = false;
    emit layerChanged(index);
    return true;
}

bool LayerStack::addVectorMask(int index, const QVector<QPainterPath>& paths)
{
    auto l = at(index);
    if (!l || paths.isEmpty()) return false;
    l->mask.kind = LayerMask::Vector;
    l->mask.vectorPaths = paths;
    l->mask.enabled = true;
    l->mask.density = 1.0;
    l->mask.feather = 0.0;
    l->mask.invert = false;
    emit layerChanged(index);
    return true;
}

bool LayerStack::clearMaskFull(int index)
{
    auto l = at(index);
    if (!l) return false;
    const bool had = l->mask.isActive() || l->mask.hasData();
    l->mask.clear();
    if (had) emit layerChanged(index);
    return had;
}

bool LayerStack::setMaskEnabled(int index, bool enabled)
{
    auto l = at(index);
    if (!l || !l->mask.hasData()) return false;
    if (l->mask.enabled == enabled) return false;
    l->mask.enabled = enabled;
    emit layerChanged(index);
    return true;
}

bool LayerStack::setMaskDensity(int index, qreal density)
{
    auto l = at(index);
    if (!l) return false;
    density = qBound(qreal(0.0), density, qreal(1.0));
    if (qFuzzyCompare(l->mask.density, density)) return false;
    l->mask.density = density;
    emit layerChanged(index);
    return true;
}

bool LayerStack::setMaskFeather(int index, qreal featherPx)
{
    auto l = at(index);
    if (!l) return false;
    featherPx = qMax(qreal(0.0), featherPx);
    if (qFuzzyCompare(l->mask.feather, featherPx)) return false;
    l->mask.feather = featherPx;
    emit layerChanged(index);
    return true;
}

bool LayerStack::setMaskInvert(int index, bool invert)
{
    auto l = at(index);
    if (!l) return false;
    if (l->mask.invert == invert) return false;
    l->mask.invert = invert;
    emit layerChanged(index);
    return true;
}

const LayerMask* LayerStack::maskAt(int index) const
{
    auto l = at(index);
    return l ? &l->mask : nullptr;
}

// =====================================================================
//   智能对象扩展
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
        // convertToSmartObject sets sourceFilePath to the
        //   cache path directly; if that file exists, treat as present.
        if (QFile::exists(l->sourceFilePath)) return false;
        // Otherwise it's a link→embedded smart object: MD5-derive the cache.
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

// =====================================================================
//  智能对象 Convert / Rasterize / Transform
// =====================================================================

// Helper to allocate a fresh cache path for convertToSmartObject. The path is
// derived from MD5(currentMSecsSinceEpoch) so consecutive converts from
// the same Bitmap layer still yield distinct cache files.
static QString generateConvertSmartObjectCachePath()
{
    const QString baseDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/smartobject-cache");
    QDir().mkpath(baseDir);
    const QByteArray hash = QCryptographicHash::hash(
        QByteArray::number(QDateTime::currentMSecsSinceEpoch()),
        QCryptographicHash::Md5).toHex();
    return baseDir + QStringLiteral("/") + QString::fromLatin1(hash)
           + QStringLiteral(".png");
}

bool LayerStack::convertToSmartObject(int index, bool embed)
{
    auto l = at(index);
    if (!l) return false;
    // Only valid for Bitmap kind (the canonical PS "Convert to Smart Object"
    //   works on bitmap / text / shape layers; P1.4.1 starts with Bitmap).
    if (l->kind != Layer::Bitmap) return false;

    if (embed) {
        if (l->image.empty()) return false;
        const QString cachePath = generateConvertSmartObjectCachePath();
        if (!cv::imwrite(cachePath.toStdString(), l->image)) return false;
        l->sourceFilePath = cachePath;
        l->sourceEmbedded = true;
    } else {
        l->sourceFilePath.clear();
        l->sourceEmbedded = false;
    }
    l->kind = Layer::SmartObject;
    l->transform = QTransform();
    l->hasTransform = false;
    emit layerChanged(index);
    return true;
}

bool LayerStack::rasterizeSmartObject(int index)
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    // rasterize fails when source is missing on disk
    //   (matches Photoshop behavior: rasterizing a broken smart object surfaces
    //    "Could not rasterize..." instead of committing a placeholder).
    if (isSmartObjectSourceMissing(index)) return false;
    // Capture the cache path before rasterize() so we can clean up the orphan
    //   cache file (embedded) after the kind flip.
    QString oldCache;
    if (l->sourceEmbedded && !l->sourceFilePath.isEmpty()) {
        oldCache = QFile::exists(l->sourceFilePath)
                       ? l->sourceFilePath
                       : smartObjectCachePath(l->sourceFilePath);
    }
    // Drop SmartFilter sub-layers first (reverse order
    //   so the indices in m_smartFilterChain[index] stay valid during removal).
    // Drop SmartFilter sub-layers first (reverse order
    //   so the chain bookkeeping stays consistent during removal).
    const QList<LayerId> childIds = smartFilterIdsFor(index);
    for (int i = childIds.size() - 1; i >= 0; --i) {
        const int idx = indexById(childIds[i]);
        if (idx >= 0) removeLayer(idx);
    }
    cv::Mat result = rasterize(l);
    if (result.empty()) return false;
    l->image = result;
    l->kind = Layer::Bitmap;
    l->sourceFilePath.clear();
    l->sourceEmbedded = false;
    l->transform = QTransform();
    m_smartFilterChain.remove(l->stableId);
    l->hasTransform = false;
    emit layerChanged(index);
    // Remove the now-orphan embedded cache file.
    if (!oldCache.isEmpty()) QFile::remove(oldCache);
    return true;
}

bool LayerStack::setSmartObjectTransform(int index, const QTransform &t)
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    // Identity input => turn off (matches PS semantics: setting identity clears).
    if (t.isIdentity()) {
        const bool was = l->hasTransform;
        l->transform = QTransform();
        l->hasTransform = false;
        if (was) emit layerChanged(index);
        return was;
    }
    if (l->transform == t && l->hasTransform) return false;
    l->transform = t;
    l->hasTransform = true;
    emit layerChanged(index);
    return true;
}

bool LayerStack::clearSmartObjectTransform(int index)
{
    auto l = at(index);
    if (!l || l->kind != Layer::SmartObject) return false;
    if (!l->hasTransform) return false;
    l->transform = QTransform();
    l->hasTransform = false;
    emit layerChanged(index);
    return true;
}

QTransform LayerStack::smartObjectTransform(int index) const
{
    auto l = at(index);
    if (!l) return QTransform();
    return l->transform;
}

int LayerStack::cleanupSmartObjectCache()
{
    //  清掉 smartobject-cache 目录里所有文件
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

//  public wrapper, 跟 private smartObjectCachePath 一样
QString LayerStack::smartObjectCachePathFor(const QString &sourceFilePath)
{
    return smartObjectCachePath(sourceFilePath);
}

// =====================================================================
//  SmartFilter chain management
// =====================================================================
//   Each SmartObject at stack index `smartIdx` owns a QList<int> of filter
//   sub-layer indices in chain order. Filter sub-layers occupy indices in
//   the main stack (so they show up in LayerPanel) but carry parentSmartIndex
//   so the renderer skips them in the main loop and applies them as a chain
//   to their parent SmartObject.

QList<LayerId> LayerStack::smartFilterIdsFor(int smartIdx) const
{
    auto l = at(smartIdx);
    if (!l) return QList<LayerId>();
    auto it = m_smartFilterChain.constFind(l->stableId);
    if (it == m_smartFilterChain.constEnd()) return QList<LayerId>();
    return it.value();
}

QList<int> LayerStack::smartFiltersFor(int smartIdx) const
{
    // 每次调用都按 id 重新解析成当下有效的下标 (LYR-16):
    //   链上存的是 id, 图层增删 / 移动以后下标会变, 缓存下来的下标就过期了.
    QList<int> out;
    const QList<LayerId> ids = smartFilterIdsFor(smartIdx);
    out.reserve(ids.size());
    for (LayerId fid : ids) {
        const int idx = indexById(fid);
        if (idx >= 0) out.append(idx);
    }
    return out;
}

int LayerStack::smartFilterCount(int smartIdx) const
{
    return smartFilterIdsFor(smartIdx).size();
}

int LayerStack::appendSmartFilter(int smartIdx, const QString &filterType,
                                  double strength, const cv::Mat &imageBefore)
{
    auto sl = at(smartIdx);
    if (!sl || sl->kind != Layer::SmartObject) return -1;
    if (filterType.isEmpty() || imageBefore.empty()) return -1;
    // Apply filter now so the layer caches a filtered snapshot for previews.
    //   The render loop also re-applies fresh on every render (so reorder /
    //   strength changes take effect without re-running append).
    cv::Mat filtered = applySmartFilter(filterType, strength, imageBefore);
    if (filtered.empty()) return -1;
    Layer l;
    l.kind = Layer::SmartFilter;
    l.name = QStringLiteral("Filter: %1").arg(filterType);
    l.filterType = filterType;
    l.filterStrength = strength;
    // 父层用 stableId 记, 不用下标 (LYR-16)
    const LayerId parentId = sl->stableId;
    l.parentSmartId = parentId;
    l.image = filtered;
    // Stamp the slot BEFORE addLayer so the stored layer carries the
    //   pre-append size (which equals the new 0-based slot index).
    l.filterSlotIndex = m_smartFilterChain.value(parentId).size();
    const int newIdx = addLayer(l);
    if (newIdx < 0) return -1;
    m_smartFilterChain[parentId].append(at(newIdx)->stableId);
    return newIdx;
}

bool LayerStack::removeSmartFilter(LayerId parentId, LayerId filterId)
{
    {
        auto it = m_smartFilterChain.find(parentId);
        if (it == m_smartFilterChain.end()) return false;
        const int pos = it.value().indexOf(filterId);
        if (pos < 0) return false;
        // 先从链上摘掉, 再删层: removeLayer 内部也会清理链 (幂等).
        it.value().removeAt(pos);
    }
    const int filterIdx = indexById(filterId);
    if (filterIdx >= 0) removeLayer(filterIdx);
    // 重排 slot
    auto it = m_smartFilterChain.find(parentId);
    if (it != m_smartFilterChain.end()) {
        const QList<LayerId> ids = it.value();
        for (int i = 0; i < ids.size(); ++i) {
            const int idx = indexById(ids[i]);
            if (idx >= 0) {
                if (auto l = at(idx)) l->filterSlotIndex = i;
            }
        }
        if (ids.isEmpty()) m_smartFilterChain.remove(parentId);
    }
    return true;
}

bool LayerStack::moveSmartFilter(LayerId parentId, LayerId filterId, int newSlot)
{
    auto it = m_smartFilterChain.find(parentId);
    if (it == m_smartFilterChain.end()) return false;
    QList<LayerId> &chain = it.value();
    const int oldPos = chain.indexOf(filterId);
    if (oldPos < 0) return false;
    if (newSlot < 0 || newSlot >= chain.size()) return false;
    if (oldPos == newSlot) return true;
    chain.move(oldPos, newSlot);
    for (int i = 0; i < chain.size(); ++i) {
        const int idx = indexById(chain[i]);
        if (idx >= 0) {
            if (auto l = at(idx)) l->filterSlotIndex = i;
        }
    }
    return true;
}

bool LayerStack::setSmartFilterEnabled(LayerId parentId, LayerId filterId, bool enabled)
{
    const int idx = indexById(filterId);
    auto l = at(idx);
    if (!l || l->kind != Layer::SmartFilter) return false;
    if (l->parentSmartId != parentId) return false;
    // PS: opacity 0 = hidden, opacity 1 = enabled. Keep the layer in chain.
    l->opacity = enabled ? 1.0f : 0.0f;
    emit layerChanged(idx);
    return true;
}

bool LayerStack::setSmartFilterStrength(LayerId parentId, LayerId filterId, double strength)
{
    const int idx = indexById(filterId);
    auto l = at(idx);
    if (!l || l->kind != Layer::SmartFilter) return false;
    if (l->parentSmartId != parentId) return false;
    l->filterStrength = strength;
    emit layerChanged(idx);
    return true;
}

// Public accessor returning the SmartObject's rasterized
//   base image (after transform + resize). Used by UI to obtain `imageBefore`
//   when appending a new SmartFilter. Empty on failure / wrong kind.
cv::Mat LayerStack::rasterizeForRender(int idx) const
{
    auto l = at(idx);
    if (!l || l->kind != Layer::SmartObject) return cv::Mat();
    return rasterize(l);
}

// public rasterize for the compositor's
//   source nodes. Takes a LayerPtr rather than an index because group
//   children are only reachable through groupChildrenOf().
cv::Mat LayerStack::rasterizeLayer(const LayerPtr &l) const
{
    if (!l) return cv::Mat();
    return rasterize(l);
}

// SmartFilter chain application, factored out of
//   render() so both render paths use one implementation.
//
//   The chain runs on the FULL rasterized image rather than per tile,
//   and that is deliberate: several filters here (GaussianBlur) sample
//   neighbouring pixels, so filtering a tile in isolation would darken
//   every tile seam. Correct tile-boundary handling would need a halo
//   of ceil(kernel/2) pixels per side, re-fetched from upstream and
//   discarded after the filter. Doing that per filter node is only worth
//   it once the DAG can express it; see LayerRasterNode.
cv::Mat LayerStack::applySmartFilterChain(const cv::Mat &src, int smartIdx) const
{
    if (src.empty()) return cv::Mat();
    const QList<LayerId> chain = smartFilterIdsFor(smartIdx);
    cv::Mat out = src;
    for (LayerId fid : chain) {
        const int fIdx = indexById(fid);
        auto fL = at(fIdx);
        if (!fL || !fL->visible || fL->opacity <= 0.0f) continue;
        cv::Mat filtered = applySmartFilter(fL->filterType, fL->filterStrength, out);
        if (!filtered.empty()) out = filtered;
    }
    return out;
}

// Re-insert a SmartFilter (used by undo)
//   The layer is appended at end of m_layers and the new index is inserted
//   into the parent chain at `atSlot` (clamped to [0..chain.size()]). Returns
//   the new layer index or -1 on failure.
int LayerStack::reinsertSmartFilter(const Layer &filterLayer, int atSlot)
{
    if (filterLayer.kind != Layer::SmartFilter || filterLayer.parentSmartId == 0) return -1;
    auto sl = byId(filterLayer.parentSmartId);
    if (!sl || sl->kind != Layer::SmartObject) return -1;
    const LayerId parentId = filterLayer.parentSmartId;
    // 复原时要拿回原来的 stableId (命令快照里带着), 不能发新 id.
    const int newIdx = addLayer(filterLayer);
    if (newIdx < 0) return -1;
    const LayerId filterId = at(newIdx)->stableId;
    int slot = atSlot;
    if (slot < 0) slot = 0;
    {
        const int chainSize = m_smartFilterChain.value(parentId).size();
        if (slot > chainSize) slot = chainSize;
    }
    m_smartFilterChain[parentId].insert(slot, filterId);
    const QList<LayerId> ids = m_smartFilterChain.value(parentId);
    for (int i = 0; i < ids.size(); ++i) {
        const int idx = indexById(ids[i]);
        if (idx >= 0) {
            if (auto l = at(idx)) l->filterSlotIndex = i;
        }
    }
    return newIdx;
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
        // 走 addLayer: 新底图要领一个 stableId.
        //   (直接 append 到 m_layers 会漏掉 id 分配, 身份就成了 0)
        addLayer(QStringLiteral("Background"), result);
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

//  工具编辑落在哪一层.
//    有选中层就用选中层; 没有选中时取最上面的 Bitmap 图层 ——
//    新打开的文档只有一层, 那层就是活动层 (跟 PS 一致).
int LayerStack::activeLayerIndex() const
{
    if (m_selection >= 0 && m_selection < m_layers.size()) return m_selection;
    for (int i = m_layers.size() - 1; i >= 0; --i) {
        if (m_layers[i] && m_layers[i]->kind == Layer::Bitmap) return i;
    }
    return -1;
}

//  活动图层的可写像素缓冲 (工具就地涂抹用).
//    返回 nullptr = 当前没有可写的 Bitmap 图层, 工具应放弃这次编辑.
cv::Mat* LayerStack::activeLayerImage()
{
    const int idx = activeLayerIndex();
    if (idx < 0) return nullptr;
    auto l = m_layers[idx];
    if (!l || l->kind != Layer::Bitmap || l->image.empty()) return nullptr;
    return &l->image;
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
    // 文档级画布尺寸 (LYR-05).
    //   画布是文档的属性: 由 setBaseLayer / setCanvasSize 写入 m_canvasSize,
    //   跟图层结构解耦.
    //   栈顶图层的 image 尺寸优先 —— 独立使用 LayerStack 时 (测试 / 工具代码)
    //   没人显式设过画布尺寸, 以第一层为准.
    //   编组把底图吞进组里以后 index 0 变成 Group (没有 image), 这时走
    //   m_canvasSize, 画布不会退化成 800x600 白底.
    if (!m_layers.isEmpty() && m_layers[0] && !m_layers[0]->image.empty()) {
        return m_layers[0]->image.size();
    }
    return m_canvasSize;
}

void LayerStack::setCanvasSize(const cv::Size &size)
{
    if (size.width <= 0 || size.height <= 0) return;
    m_canvasSize = size;
}

QString LayerStack::smartObjectCachePath(const QString &sourceFilePath)
{
    //  BUG FIX: 用 source 路径的 MD5 哈希做文件名
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
        // Normalised to 4-channel BGRA straight alpha (pixel contract).
        //
        // The result must be an OWNED buffer: toBgra() returns an
        // already-4-channel input WITHOUT copying, so handing it back
        // directly would alias the layer's stored pixels. The render
        // paths then write into the rasterized result (the mask is
        // applied to its alpha), which would rewrite the layer's own
        // alpha on every render -- and the damage compounds, because
        // each render applies the mask to the already-masked alpha.
        // The compositor reads the layer image, so it would then see a
        // layer that is masked twice.
        if (l->image.empty()) return cv::Mat();
        return toBgra(l->image).clone();
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
        // QImage ARGB32 -> cv::Mat BGRA.
        //   It must NOT be converted to BGR -- dropping the alpha is
        //   LYR-01: the transparent background of the text layer becomes
        //   opaque black and the whole canvas is covered in black.
        cv::Mat mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        return mat.clone();
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
            // convertToSmartObject writes the cache path
            //   directly into sourceFilePath (no MD5 derivation). For such
            //   layers the path IS the cache file, so use it as-is when it
            //   exists. For link→embedded toggles, sourceFilePath is still
            //   the external path and we MD5-derive the cache location.
            if (!QFile::exists(path)) {
                path = smartObjectCachePath(l->sourceFilePath);
            }
        }
        const cv::Size sz = canvasSize();
        if (path.isEmpty() || !QFile::exists(path)) {
            //  缺源时返 placeholder
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
        // apply non-destructive affine transform if set
        //   Identity = no-op. cv::warpAffine in-place semantics on a clone.
        if (l->hasTransform && !l->transform.isIdentity() && l->transform.isAffine()) {
            cv::Mat M(2, 3, CV_64F);
            M.at<double>(0, 0) = l->transform.m11();
            M.at<double>(0, 1) = l->transform.m12();
            M.at<double>(0, 2) = l->transform.dx();
            M.at<double>(1, 0) = l->transform.m21();
            M.at<double>(1, 1) = l->transform.m22();
            M.at<double>(1, 2) = l->transform.dy();
            cv::Mat warped;
            cv::warpAffine(img, warped, M, img.size(),
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                           cv::Scalar(0, 0, 0));
            img = warped;
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

// composite a group's children onto `target`, in
//   order, PS-style: each child blends with its own blendMode, and a
//   nested Group composites like any other layer but contributes its own
//   subtree first and then applies its opacity.
//
//   Extracted out of render() so nesting works by construction. The old
//   inline loop called rasterize(child) and skipped anything that came
//   back empty — which is every nested Group, since rasterize() has no
//   case for Layer::Group. That silently dropped the entire inner subtree
//   from the render while leaving it intact in the model.
//
//   Note the group children path deliberately does NOT apply a
//   SmartFilter chain (neither did the original loop, and the chain is
//   keyed on a m_layers index a group child does not have).
void LayerStack::compositeChildrenInto(const std::vector<LayerPtr> &children,
                                       cv::Mat &target) const
{
    for (const auto &child : children) {
        if (!child || !child->visible) continue;

        if (child->kind == Layer::SmartFilter) continue;

        if (child->kind == Layer::Group) {
            const auto &grandChildren = groupChildrenOf(child);
            if (grandChildren.empty()) continue;
            // Nested group: the same model as the top-level group branch
            //   (and as GroupNode on the compositor side) -- the children
            //   are composited onto a TRANSPARENT group canvas, which is
            //   then blended over the incoming canvas.
            cv::Mat groupMat(target.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));
            compositeChildrenInto(grandChildren, groupMat);
            target = applyBlend(Layer::Normal, target, groupMat, child->opacity);
            continue;
        }

        cv::Mat cMat = rasterize(child);
        if (cMat.empty()) continue;
        // Mask -> coverage written into the alpha (CP-07 / CP-18).
        //   The single mask model: Layer::mask, and the render only
        //   honours it.
        LayerMaskRenderer::applyAlpha(cMat, child->mask);
        target = applyBlend(child->blend, target, cMat, child->opacity);
    }
}

cv::Mat LayerStack::render() const
{
    // compositor-backed render path
    //   Default on since, once every LayerKind had a
    //   node type. setCompositorEnabled(false) falls back to the legacy
    //   per-layer loop below.
    if (m_compositorEnabled) return renderViaCompositor();
    return renderLegacy();
}

// legacy per-layer compositing: start from an opaque white document
// canvas, then blend every visible layer over it in zOrder.
//
//   Pixel contract (CP-07 / CP-18): the canvas is CV_8UC4 BGRA with
//   straight alpha throughout. Every layer -- the base image included --
//   contributes coverage through Porter-Duff "over". A layer's own alpha
//   (pixel transparency) and its mask-derived coverage are folded into the
//   source alpha by applyAlpha() before the blend.
//
//   This path is the fallback for the compositor (see renderViaCompositor)
//   and the reference implementation the compositor is checked against
//   (tst_P8_RenderParity). The two must agree pixel for pixel.
cv::Mat LayerStack::renderLegacy() const
{
    if (m_layers.isEmpty()) return cv::Mat();

    // Step 1: the document canvas is an opaque WHITE layer.
    //   Every layer -- the base image included -- is blended over it,
    //   so hiding the base image really reveals what is below it
    //   (LYR-13 / LYR-14).
    //   The alpha is 255 explicitly: cv::Scalar(255, 255, 255) leaves
    //   the 4th channel at 0, which would make the whole document
    //   transparent.
    //
    //   This mirrors the compositor, which roots the graph on a
    //   SolidCanvasNode and blends the whole stack over it. The base
    //   image is an ordinary layer there, and it has to be an ordinary
    //   layer here too, or the two paths diverge.
    const cv::Size sz = canvasSize();
    cv::Mat canvas(sz, CV_8UC4, cv::Scalar(255, 255, 255, 255));

    // Step 2: blend every visible layer in zOrder.
    for (int i = 0; i < m_layers.size(); ++i) {
        const auto &l = m_layers[i];
        if (!l || !l->visible) continue;
        // SmartFilter sub-layers render only as part of
        //   their parent SmartObject's chain; skip them in the main loop.
        if (l->kind == Layer::SmartFilter) continue;

        // Group: composite the children into a transparent group canvas,
        //   then composite that over the canvas with Group.opacity.
        //   Group's own blendMode is intentionally ignored (PS does the same).
        if (l->kind == Layer::Group) {
            const auto &children = groupChildrenOf(idOf(i));
            if (children.empty()) continue;
            // The group's children are composited onto a TRANSPARENT
            //   group canvas -- they must NOT see the layers below the
            //   group, or a child with a blend mode would blend against
            //   the backdrop instead of against the group itself.
            //   That group canvas is then blended over the incoming
            //   canvas with the group's opacity, which is exactly what
            //   GroupNode does on the compositor side.
            cv::Mat groupMat(canvas.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));
            compositeChildrenInto(children, groupMat);
            canvas = applyBlend(Layer::Normal, canvas, groupMat, l->opacity);
            continue;
        }

        // The base image is NOT special: it is an ordinary layer that
        //   reaches the canvas through the same blend path as every other
        //   layer, so its opacity scales its coverage (not its colour)
        //   exactly like the rest of the stack.

        // Adjustment layer: apply the LUT to the canvas.
        //
        //   The LUT touches the COLOUR channels only -- running the alpha
        //   through a colour LUT would scramble the canvas transparency.
        //   The adjustment's opacity scales the COVERAGE (Porter-Duff
        //   over), the same as every other layer.
        if (l->kind == Layer::Adjustment) {
            if (!l->adjustmentLut.empty() && l->adjustmentLut.rows == 256
                && l->adjustmentLut.cols == 1 && l->adjustmentLut.type() == CV_8U) {
                cv::Mat bgr, alpha, adjustedBgr, adjusted;
                splitBgra(canvas, bgr, alpha);
                cv::LUT(bgr, l->adjustmentLut, adjustedBgr);
                std::vector<cv::Mat> ch;
                cv::split(adjustedBgr, ch);
                ch.push_back(alpha);
                cv::merge(ch, adjusted);
                if (l->opacity < 1.0f) {
                    canvas = compositeOver(canvas, adjustedBgr, alpha, l->opacity);
                } else {
                    canvas = adjusted;
                }
            }
            continue;
        }

        // Other kinds: rasterize, then blend.
        cv::Mat layerMat = rasterize(l);
        if (layerMat.empty()) continue;
        // SmartObject chain application
        //   Walk the chain in order and re-apply each enabled filter
        //   fresh (so reorder / strength changes take effect without
        //   re-running append). Shared with the compositor path via
        //   applySmartFilterChain() -- P8.1.1.x.
        if (l->kind == Layer::SmartObject) {
            layerMat = applySmartFilterChain(layerMat, i);
        }
        // Mask -> coverage written into the source alpha
        //   (CP-07 / CP-18). Single mask model: Layer::mask.
        LayerMaskRenderer::applyAlpha(layerMat, l->mask);
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
    // 单层预览也应用 mask
    LayerMaskRenderer::applyAlpha(m, l->mask);
    if (l->opacity < 1.0f) {
        // Straight alpha semantics: the opacity scales the alpha, not
        //   the colour (scaling the colour would premultiply the layer,
        //   and the compositing would darken it a second time).
        cv::Mat bgr, alpha, out;
        splitBgra(m, bgr, alpha);
        alpha.convertTo(alpha, CV_8U, l->opacity);
        std::vector<cv::Mat> ch;
        cv::split(bgr, ch);
        ch.push_back(alpha);
        cv::merge(ch, out);
        return out;
    }
    return m;
}

// =====================================================================
//  清空 / 重置
// =====================================================================

void LayerStack::clear()
{
    // 编组子树不能跨文档残留 (LYR-12): m_groups 以前不在 clear 里清,
    //   旧组的子层会一直挂在内存里, 新建的 Group 甚至会继承旧 children.
    m_groups.clear();
    if (m_layers.isEmpty()) return;
    m_layers.clear();
    m_smartFilterChain.clear();
    m_selection = -1;
    emit countChanged();
    emit selectionChanged(-1);
}

void LayerStack::setBaseLayer(const cv::Mat &baseImage)
{
    if (baseImage.empty()) return;
    // 文档级画布尺寸跟着底图走 (LYR-05):
    //   canvasSize() 不再从 index 0 反推, 所以这里必须显式写.
    m_canvasSize = baseImage.size();
    if (m_layers.isEmpty()) {
        addLayer(QStringLiteral("Background"), baseImage);
    } else {
        if (m_layers[0]) {
            m_layers[0]->image = toBgra(baseImage);
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

// renderAsync — 异步 render
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
// =====================================================================
//  compositor-backed render
// =====================================================================
//
// Renders the stack through the P8.1 CompositorGraph instead of the
// legacy per-layer loop above. The DAG is rebuilt on every call
// (LayerStack is mutable: add/remove/reorder/blend edits all happen
// between renders), which is acceptable because the expensive part
// is the per-pixel blend work and the tile cache lives on the
// RenderGraph, not on the DAG.
//
// Feature flag: set via setCompositorEnabled. Off by default so
// the legacy path stays the default until the remaining layer kinds
// (Vector / Text / SmartObject / Adjustment) have their nodes. Tests
// in tst_P8_1_1 exercise this path directly.

cv::Mat LayerStack::renderViaCompositor() const
{
    if (m_layers.isEmpty()) return cv::Mat();

    try {
        compositor::LayerStackAdapter::Result built =
            compositor::LayerStackAdapter::build(const_cast<LayerStack&>(*this));
        // outputNode is never 0: the adapter always builds an explicit root
        // (the base bitmap, or a SolidCanvasNode for the no-base case), so
        // there is no "nothing renderable" branch to fall back on here.

        // Canvas geometry comes from the adapter, which already had to
        // resolve the compositing root — see LayerStackAdapter::Result.
        compositor::TileGrid grid(built.canvasSize.width, built.canvasSize.height);
        compositor::RenderGraph rg(&built.graph, built.outputNode, &grid);
        cv::Mat out = rg.execute();
        if (!out.empty()) return out;
        LOG_WARN("[LayerStack] compositor produced an empty canvas");
    } catch (const cv::Exception& e) {
        LOG_WARN("[LayerStack] compositor render failed: {}", e.what());
    } catch (const std::exception& e) {
        LOG_WARN("[LayerStack] compositor render failed: {}", e.what());
    }

    // 回退: 走 legacy 逐层合成, 宁可慢也不能让用户看到一张全黑的图
    if (m_compositorFallback) return renderLegacy();
    return cv::Mat();
}

bool LayerStack::compositorEnabled() const { return m_compositorEnabled; }
void LayerStack::setCompositorEnabled(bool on, bool allowFallback)
{
    m_compositorEnabled = on;
    m_compositorFallback = allowFallback;
}

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
        [self, cb](vistella::tp::TaskContext & /*taskCtx*/) -> void {
            cv::Mat result = self->render();
            // 跨线程切回主线程: Qt::QueuedConnection 必走主线程事件循环
            // 闭包捕获 cb + result, 用 std::move 避免 cv::Mat copy
            //
            // 接收者用 LayerStack 自己 (不是 QCoreApplication):
            //   队列事件挂在接收者上, LayerStack 销毁时 Qt 会自动撤掉这个事件,
            //   回调就不会落到已经销毁的窗口上.
            //   之前用 QCoreApplication::instance() 当接收者, 事件没人管,
            //   关掉 tab 之后回调照样跑 -> UAF.
            QMetaObject::invokeMethod(const_cast<LayerStack*>(self),
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

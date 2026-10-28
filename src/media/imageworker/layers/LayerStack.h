#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// =============================================================
// LayerStack — 完整 LayerStack
//
// 职责:
//   1. 维护多个 Layer (QList<LayerPtr>) + 编组 (QList<LayerGroup>)
//   2. 渲染时从底到顶按 zOrder + opacity + visible + blendMode 混合
//   3. add/remove/duplicate/moveUp/moveDown/mergeDown/flatten/group/ungroup API
//   4. 链接 (cross-group linking for move/scale/rotate 同步)
//   5. 选中跟踪 (current selection)
//   6. signals: layerAdded/Removed/Changed/countChanged + selectionChanged
//
// 设计原则 (成熟方案):
//   - 跟 Photoshop / GIMP 一致: zOrder 越大越上面
//   - render() 用 cv::addWeighted 按 opacity + blendMode 混合
//     Phase 1: 只用 Normal (addWeighted)
//     Phase 2: 实现 13 种 blend mode (Multiply/Screen/Overlay/...)
//   - flatten() 把所有可见层混合成一张 cv::Mat (用于保存/合并)
//   - moveUp / moveDown 调整 zOrder (list index 0 = 底, list 末尾 = 顶)
//   - 链接: 跨组时用 layerId (不是 index), 避免组增删时 index 漂移
// =============================================================

#include "Layer.h"

#include <QObject>
#include <QList>
#include <QHash>
#include <QStringList>

#include <opencv2/core.hpp>

#include <functional>

// forward decl for EngineContext
//   renderAsync(EngineContext*, callback) 走 background pool 后台跑 render()
//   LayerStack.cpp 实现时 #include 完整头, 这里只前向声明避免强制依赖
namespace vistella::tp { class EngineContext; }

namespace layers {

// Layer 唯一 id (用于链接跨组引用)
using LayerId = quintptr;   // reinterpret_cast<quintptr>(layerPtr.get())

class LayerStack : public QObject
{
    Q_OBJECT
public:
    explicit LayerStack(QObject *parent = nullptr);
    ~LayerStack() override;

    // ---- 基础查询 ----
    int  count() const { return m_layers.size(); }
    bool isEmpty() const { return m_layers.isEmpty(); }
    LayerPtr at(int index) const;
    LayerPtr baseLayer() const;   // zOrder 最小的层 (底图)
    LayerPtr topLayer() const;    // zOrder 最大的层 (顶层)
    int  indexOf(LayerPtr layer) const;
    int  indexById(LayerId id) const;   // 链接用

    // ---- 修改操作 (基础) ----
    int  addLayer(const Layer &layer);
    int  addLayer(const QString &name, const cv::Mat &image);
    // 在指定位置插入图层.
    //   addLayer 只能追加到末尾, 撤销一次"删除中间图层"时必须把图层放回
    //   原来的位置, 否则撤销之后层序就变了 (删除 [A,B,C] 的 B, 撤销得到
    //   [A,B,C] 而不是 [A,C,B]).
    //   index 取值 [0, count()], count() 表示追加到末尾.
    int  insertLayer(int index, const Layer &layer);
    // 4 种 per-kind add 工厂
    int  addTextLayer(const QString &name, const QString &text,
                      int fontSize = 24, const QColor &color = QColor(Qt::white),
                      const QString &fontFamily = QStringLiteral("Arial"));
    int  addVectorLayer(const QString &name,
                        const QVector<QPainterPath> &paths = {},
                        const QVector<QColor> &fills = {});
    int  addSmartObjectLayer(const QString &name, const QString &sourceFilePath,
                             bool embed = false);
    int  addAdjustmentLayer(const QString &name, const QString &adjustmentType,
                            const cv::Mat &lut = cv::Mat());

    // Group / Ungroup (LayerKind::Group based)
    //   mergeIntoGroup: pack N adjacent layers (>= 2) into a new Group container
    //     layer appended at end of stack. Returns new Group index or -1 on
    //     failure (e.g. non-adjacent indices, < 2 layers).
    //   flattenGroup: replace Group at `groupIdx` with its children in order.
    //     Returns count of layers inserted, or -1 if `groupIdx` is not Group.
    //   groupChildrenOf(groupIdx): read-only accessor for Group's children
    //     (vector of LayerPtr, in order). Returns empty if idx is not a Group.
    //   Note: these differ from the older createGroup/groupLayers/ungroupLayer
    //   pair (P0-1 stubs at lines 200-204) which used external group ids; the
    //   new approach uses LayerKind::Group + per-stack m_groups storage.
    int  mergeIntoGroup(const QList<int>& indices);
    int  flattenGroup(int groupIdx);
    // key by LayerId (stable pointer) not int index
    //   so render() can look up Group children without re-keying after every
    //   m_layers mutation (moveUp / removeLayer / duplicateLayer / flattenGroup).
    const std::vector<LayerPtr>& groupChildrenOf(LayerId groupId) const;
    //   same lookup from a LayerPtr. Needed to walk
    //   INTO a nested group — a group child is not in m_layers, so there
    //   is no index to call idOf() with, and the caller has no business
    //   reinterpreting a pointer as a LayerId itself.
    const std::vector<LayerPtr>& groupChildrenOf(const LayerPtr& group) const;

    // inverse of flattenGroup. Used by
    //   LayerCommand::makeFlattenGroup's undo path to restore a Group
    //   from a captured children snapshot without depending on the now-
    //   shifted m_layers indices. Replaces the Group at `atIndex` in
    //   m_layers with a new Group container holding the supplied cloned
    //   children. Returns new Group index or -1 on failure.
    int reinsertGroup(int atIndex, std::vector<LayerPtr> children);

    bool removeLayer(int index);
    bool duplicateLayer(int index);
    // 通知外部: 图层内容被直接改写, 需要发 layerChanged.
    //   LayerStack 自己的 setter 都带 emit, 但有几条路径是直接写 Layer
    //   字段的 (command 还原快照 / 蒙版恢复 / 类型转换还原), 这些路径
    //   绕过了 setter, 用这个补发一次, 让画布和图层面板知道内容变了.
    void notifyLayerChanged(int index);
    bool moveUp(int index);
    bool moveDown(int index);
    bool setVisible(int index, bool visible);
    bool setOpacity(int index, float opacity);
    bool setLocked(int index, bool locked);
    bool setLinked(int index, bool linked);
    bool setBlend(int index, Layer::BlendMode mode);
    bool rename(int index, const QString &newName);

    // per-kind 编辑 setter
    //   返 true 表示 layerChanged signal 已发
    bool setText(int index, const QString &text);
    bool setTextFont(int index, const QString &family, int size, const QColor &color);
    bool setVectorPaths(int index, const QVector<QPainterPath> &paths,
                        const QVector<QColor> &fills,
                        const QVector<qreal> &strokes = {});
    bool setSmartObjectSource(int index, const QString &path, bool embed);
    bool setAdjustmentType(int index, const QString &type);
    bool setAdjustmentLut(int index, const cv::Mat &lut);

    // 智能对象扩展 API
    //   refresh(index): 重新从源文件加载 (嵌入时也重新复制到 cache)
    //   isSmartObjectSourceMissing(index): 源文件是否存在
    //   toggleSmartObjectEmbed(index): link ↔ embed 切换
    bool refreshSmartObject(int index);
    bool isSmartObjectSourceMissing(int index) const;
    bool toggleSmartObjectEmbed(int index);

    // 智能对象 Convert / Rasterize / Transform 完整化
    //   convertToSmartObject(index, embed=true): in-place Bitmap -> SmartObject.
    //     embed=true: write current image to MD5 cache, sourceFilePath = cache path.
    //     embed=false: sourceFilePath empty (caller will provide later).
    //   rasterizeSmartObject(index): SmartObject -> Bitmap, in-place.
    //     Loads source via rasterize(), replaces image, clears SmartObject payload.
    //   setSmartObjectTransform / clearSmartObjectTransform / smartObjectTransform:
    //     Non-destructive affine transform applied at rasterize() time. Identity = off.
    bool convertToSmartObject(int index, bool embed = true);
    bool rasterizeSmartObject(int index);
    bool setSmartObjectTransform(int index, const QTransform &t);
    bool clearSmartObjectTransform(int index);
    QTransform smartObjectTransform(int index) const;

    // SmartFilter chain management (PS-style smart filters)
    //   Each SmartObject layer can have 0+ filter sub-layers stored in
    //   smartFiltersFor(smartIdx). Filter sub-layers occupy indices in the main
    //   stack (so they show up in LayerPanel) but are tagged with parentSmartIndex.
    //   The renderer skips them in the main loop and applies them as a chain to
    //   their parent SmartObject.
    const QList<int>& smartFiltersFor(int smartIdx) const;   // ordered indices
    int smartFilterCount(int smartIdx) const;
    // Append a filter sub-layer to a SmartObject's chain.
    //   Applies the filter to imageBefore via the inline filter dispatcher
    //   (currently 5 hardcoded filters: GaussianBlur / Sharpen / Brightness /
    //   Contrast / Emboss). Stores the filtered image in the new layer so the
    //   renderer can chain without re-running filters. Returns new layer index
    //   or -1 on failure.
    int appendSmartFilter(int smartIdx, const QString &filterType, double strength,
                          const cv::Mat &imageBefore);
    bool removeSmartFilter(int smartIdx, int filterIdx);
    bool moveSmartFilter(int smartIdx, int filterIdx, int newSlot);
    bool setSmartFilterEnabled(int smartIdx, int filterIdx, bool enabled);
    bool setSmartFilterStrength(int smartIdx, int filterIdx, double strength);

    // Re-insert a previously-removed filter sub-layer
    //   (used by undo). The layer is added at end of m_layers and inserted
    //   into the chain at `atSlot`. Used by LayerCommand::undo for the
    //   RemoveSmartFilter case.
    int reinsertSmartFilter(const Layer &filterLayer, int atSlot);

    // Public accessor for SmartObject base image
    //   Loads source from sourceFilePath, applies transform/resize to canvas
    //   (no filter chain). Returns cv::Mat or empty if source missing.
    //   Used by UI to obtain the input image for a new SmartFilter.
    cv::Mat rasterizeForRender(int idx) const;

    // compositor source access
    //   rasterizeLayer: public form of the private rasterize(), taking a
    //   LayerPtr rather than an index.
    //
    //   A LayerPtr (not an int) because group children are not in
    //   m_layers — they live in m_groups and are only reachable through
    //   groupChildrenOf(). Compositor source nodes must rasterize both
    //   kinds of layer, so they need a handle that works for either.
    //
    //   Adjustment deliberately returns an empty Mat: it is a filter
    //   over the accumulated canvas, not a standalone image — the
    //   compositor models it as LayerAdjustmentNode instead.
    //
    //   Exposed so compositor nodes reuse this single rasterization
    //   implementation instead of re-deriving it. Diverging copies are
    //   how the legacy path and the DAG path would silently drift apart.
    cv::Mat rasterizeLayer(const LayerPtr &l) const;

    //   applySmartFilterChain: walk smartFiltersFor(smartIdx) in order and
    //   apply each enabled filter to `src`, returning the result.
    //   Mirrors the chain block inside render(). Exposed for the same
    //   single-source-of-truth reason; the per-filter implementation stays
    //   file-local since it is stateless.
    cv::Mat applySmartFilterChain(const cv::Mat &src, int smartIdx) const;

    // 全局 cache 清理
    //   遍历 cache dir, 删掉不被任何 layer 引用的文件
    //   返删除的文件数
    static int cleanupSmartObjectCache();
    // 给 MainWindow 调, 拿到嵌入 cache 路径 (打开源用)
    static QString smartObjectCachePathFor(const QString &sourceFilePath);

    // 蒙版 setter
    //   addMask: 加载一张灰度图当蒙版, 同步 enable
    //   clearMask: 删蒙版 (但保留 enable 状态)
    //   enableMask: 开关蒙版 (不删图)
    //   loadMaskFromImage: 从任意彩色图生成灰度蒙版 (转灰度再二值化或保留)
    bool addMask(int index, const cv::Mat &grayMask);
    bool clearMask(int index);
    bool enableMask(int index, bool enabled);

    // 扩展 LayerMask 接口 (pixel/vector/density/feather/invert)
    //   这些方法直接操作 layer.mask struct, 与上面 3 个旧方法互不冲突
    bool addPixelMask(int index, const cv::Mat& grayMask);
    bool addVectorMask(int index, const QVector<QPainterPath>& paths);
    bool clearMaskFull(int index);  // clear full struct (vs old clearMask)
    bool setMaskEnabled(int index, bool enabled);
    bool setMaskDensity(int index, qreal density);
    bool setMaskFeather(int index, qreal featherPx);
    bool setMaskInvert(int index, bool invert);
    const LayerMask* maskAt(int index) const;

    // ---- 合并 ----
    // 向下合并: 把 index 上面的 layer 合并到 index 位置的 layer, 上面删
    //   例: [A, B, C] 调 mergeDown(1) → B 合并到 A → [A+C, C]
    //   返合并后 index 位置 (新 layer)
    bool mergeDown(int index);
    // 拼合所有可见层: 返 cv::Mat + 清空 stack (1 个新 base layer)
    cv::Mat flattenVisible();

    // ---- 编组 ----
    // 新建空 group (id 唯一), 返 group id
    int  createGroup(const QString &name);
    // 把 [first, last] 的 layer 编入指定 group (按 zOrder 顺序)
    bool groupLayers(int first, int last, int groupId);
    // 解散 group (group 内的 layer 移出, group 删除)
    bool ungroupLayer(int layerIndex);

    // ---- 链接 ----
    // 跟 index 链接的所有 layer index (含 index 自己)
    QList<int> linkedSelection(int index) const;
    // 选中一组 (把这一组都 mark 为 selected, 用于 panel 高亮)
    void setSelection(int index);
    void clearSelection();
    int  selection() const { return m_selection; }
    QList<int> selectedIndices() const;   // 当前 + 它的 linked

    // ---- 工具编辑目标 (活动图层) ----
    //   activeLayerIndex(): 工具编辑落在哪一层.
    //     有选中层就用选中层; 没有选中时取最上面的 Bitmap 图层
    //     (新打开的文档只有一层, 那层就是活动层).
    //   activeLayerImage(): 活动图层的可写像素缓冲, 工具就地涂抹用.
    //     返回 nullptr = 当前没有可写的 Bitmap 图层, 工具应放弃这次编辑.
    //   这两个接口是"编辑写图层而不是写合成缓存"的唯一入口:
    //   直接改 m_current 的编辑会被下一次 render() 冲掉.
    int   activeLayerIndex() const;
    cv::Mat* activeLayerImage();

    // ---- 渲染 ----
    cv::Mat render() const;                 // 所有可见层按 zOrder + blend 混合
    cv::Mat renderOne(int index) const;     // 单独预览某层 (含 opacity)

    // 异步 render — 走 EngineContext background 池
    //   1. engine == nullptr 走 sync (跟 render() 一致)
    //   2. engine 非空: submit 到 background().submit_fn, 跑完用
    //      QMetaObject::invokeMethod 切回主线程调 callback(cv::Mat)
    //   3. callback 可能在主线程执行 (Qt::QueuedConnection) — 调用方负责线程安全
    //   4. self 指针在闭包里保活 (LayerStack 销毁前 callback 不会被调, 因为
    //      invokeMethod 是 queued, 队列里 lambda 持有 self 引用)
    //   5. 大图 (>1024x1024) 用 renderAsync 走 background, 小图用 render 走 sync
    //      (ImageWindow::rebuildCurrentCache 决策; renderAsync 不做大小判断)
    using RenderCallback = std::function<void(cv::Mat)>;
    void renderAsync(vistella::tp::EngineContext *engine, RenderCallback callback) const;

    // compositor-backed render
    //   renderViaCompositor() builds a P8.1 CompositorGraph from this
    //   stack (LayerBitmapNode + LayerRasterNode + LayerBlendNode +
    //   LayerMaskNode + LayerAdjustmentNode + GroupNode) and renders it
    //   through RenderGraph + TileCache.
    //
    //   render() consults the compositorEnabled flag and dispatches
    //   here when it is on.
    //
    //   Default ON as of every LayerKind has a node
    //   (Text / Vector / SmartObject via LayerRasterNode, Adjustment via
    //   LayerAdjustmentNode), so the graph can render a whole stack.
    //   The switch is still there for A/B comparison and as the way out
    //   if a parity gap shows up in the field.
    //
    //   tst_P8_1_1 pins both defaults: the flag state, and that the two
    //   paths agree pixel-for-pixel on the matrices listed there.
    cv::Mat renderViaCompositor() const;
    bool    compositorEnabled() const;
    // 打开/关闭 compositor 渲染路径.
    //   allowFallback: compositor 路径出错 (节点抛异常 / 产出空图) 时是否
    //   自动回退到 legacy 逐层合成. 默认开着 —— 保守策略, 宁可慢也不能
    //   让用户看到一张全黑的图.
    // TODO(P8.4): 设置页没有暴露这个开关, 用户遇到 compositor 渲染异常时没法手动切回 legacy 路径 -- see docs/DEBT.md#A-SETCOMPOSITOR
    void    setCompositorEnabled(bool on, bool allowFallback = true);

    // 画布尺寸
    //   渲染非 Bitmap (text/vector/smart) 时需要知道输出尺寸
    //   默认 = base layer image 尺寸, 或 800x600 fallback
    cv::Size canvasSize() const;
    void    setCanvasSize(const cv::Size &size);   // 显式设 (新建空文件用)

    // ---- 清空 / 重置 ----
    void clear();
    void setBaseLayer(const cv::Mat &baseImage);
    QStringList layerNames() const;

    // ---- ID helpers (链接跨组用) ----
    LayerId idOf(int index) const;
    LayerPtr byId(LayerId id) const;

signals:
    void layerAdded(int index);
    void layerRemoved(int index);
    void layerChanged(int index);       // visible / opacity / zOrder / blend / locked / linked 变化
    void countChanged();
    void selectionChanged(int index);
    void groupChanged(int groupId);     // 编组变化

private:
    QList<LayerPtr> m_layers;

    // legacy 逐层合成 (compositor 路径的兜底实现)
    cv::Mat renderLegacy() const;

    // switch for renderViaCompositor. Default
    // true since P8.2; see the renderViaCompositor() declaration above.
    bool m_compositorEnabled = true;
    // compositor 失败时是否回退到 legacy 路径 (默认保守: 开)
    bool m_compositorFallback = true;
    int  m_selection = -1;
    // Phase 3: 画布尺寸 (无 base layer 时 rasterize text/vector 用)
    cv::Size m_canvasSize = cv::Size(800, 600);

    // SmartObject -> SmartFilter sub-layer indices
    //   Each SmartObject at index `smartIdx` has a QList of filter sub-layer
    //   indices in chain order (0..N-1). Filter sub-layers are full Layer
    //   entries in m_layers (so they show up in LayerPanel) but tagged with
    //   parentSmartIndex so the renderer knows to skip them as standalone.
    QHash<int, QList<int>> m_smartFilterChain;

    // Group children storage
    //   rekeyed by LayerId (stable LayerPtr.get()) instead of int layer index,
    //   so mutations to m_layers (moveUp / removeLayer / duplicateLayer /
    //   flattenGroup) do NOT invalidate m_groups lookups. Value is the vector
    //   of cloned child layers. Layer struct itself does NOT hold children
    //   (would break Layer's copy semantics; see Layer.h comment).
    QHash<LayerId, std::vector<LayerPtr>> m_groups;

    // m_groups is keyed by LayerId, which IS the
    // LayerPtr's address. So any operation that produces a NEW LayerPtr
    // for a Group orphans that group's children: the entry still exists
    // under the old address and the clone looks childless. Three
    // operations hit this — mergeIntoGroup() (nesting a group), and
    // duplicateLayer() — plus removeLayer(), which used to leak the
    // entry outright. These two helpers are the single place where a
    // layer's group children are carried across an address change or
    // released with their owner.
    LayerPtr cloneLayerWithGroupChildren(const LayerPtr& src);
    void    releaseGroupChildren(const LayerPtr& group);
    int     appendLayerPtr(LayerPtr layer);

    // composite a group's children onto `target` in
    //   order, recursing into nested Groups. Shared by the top-level
    //   render() loop and by nested groups themselves, so nesting works
    //   by construction instead of only at the first level.
    void compositeChildrenInto(const std::vector<LayerPtr> &children,
                               cv::Mat &target) const;

    void reassignZOrder();
    void disconnectLayer(LayerPtr l);

    // Phase 3: 5 种 kind 通用 rasterize (返回空 Mat = 不可渲染)
    cv::Mat rasterize(const LayerPtr &l) const;
    // smart object 嵌入时 cache 路径
    static QString smartObjectCachePath(const QString &sourceFilePath);
};

// Layer 组 (一组 layer 共享变换/可见性)
struct LayerGroup {
    int id = 0;
    QString name;
    QList<LayerId> memberIds;   // 成员 layer id 列表 (顺序 = zOrder asc)
    bool visible = true;        // 组可见性 (跟成员 AND)
    bool locked  = false;       // 组锁定
};

} // namespace layers

Q_DECLARE_METATYPE(layers::LayerId)

#endif // LAYERSTACK_H

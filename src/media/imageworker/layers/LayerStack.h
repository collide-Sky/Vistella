#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// =============================================================
// LayerStack — 阶段 1 W4.3 Phase 1 完整 LayerStack (2026-09-04)
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

// P0-2.5 (2026-09-08): forward decl for EngineContext
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
    // Phase 3 (2026-09-04): 4 种 per-kind add 工厂
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

    bool removeLayer(int index);
    bool duplicateLayer(int index);
    bool moveUp(int index);
    bool moveDown(int index);
    bool setVisible(int index, bool visible);
    bool setOpacity(int index, float opacity);
    bool setLocked(int index, bool locked);
    bool setLinked(int index, bool linked);
    bool setBlend(int index, Layer::BlendMode mode);
    bool rename(int index, const QString &newName);

    // Phase 3 (2026-09-04): per-kind 编辑 setter
    //   返 true 表示 layerChanged signal 已发
    bool setText(int index, const QString &text);
    bool setTextFont(int index, const QString &family, int size, const QColor &color);
    bool setVectorPaths(int index, const QVector<QPainterPath> &paths,
                        const QVector<QColor> &fills,
                        const QVector<qreal> &strokes = {});
    bool setSmartObjectSource(int index, const QString &path, bool embed);
    bool setAdjustmentType(int index, const QString &type);
    bool setAdjustmentLut(int index, const cv::Mat &lut);

    // Phase 5 (2026-09-04): 智能对象扩展 API
    //   refresh(index): 重新从源文件加载 (嵌入时也重新复制到 cache)
    //   isSmartObjectSourceMissing(index): 源文件是否存在
    //   toggleSmartObjectEmbed(index): link ↔ embed 切换
    bool refreshSmartObject(int index);
    bool isSmartObjectSourceMissing(int index) const;
    bool toggleSmartObjectEmbed(int index);

    // P1.4.1 (2026-09-17): 智能对象 Convert / Rasterize / Transform 完整化
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

    // P1.4.4 (2026-09-17): SmartFilter chain management (PS-style smart filters)
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

    // P1.4.4 (2026-09-17): Re-insert a previously-removed filter sub-layer
    //   (used by undo). The layer is added at end of m_layers and inserted
    //   into the chain at `atSlot`. Used by LayerCommand::undo for the
    //   RemoveSmartFilter case.
    int reinsertSmartFilter(const Layer &filterLayer, int atSlot);

    // P1.4.4 (2026-09-17): Public accessor for SmartObject base image.
    //   Loads source from sourceFilePath, applies transform/resize to canvas
    //   (no filter chain). Returns cv::Mat or empty if source missing.
    //   Used by UI to obtain the input image for a new SmartFilter.
    cv::Mat rasterizeForRender(int idx) const;

    // Phase 5 (2026-09-04): 全局 cache 清理
    //   遍历 cache dir, 删掉不被任何 layer 引用的文件
    //   返删除的文件数
    static int cleanupSmartObjectCache();
    // Phase 5 (2026-09-04): 给 MainWindow 调, 拿到嵌入 cache 路径 (打开源用)
    static QString smartObjectCachePathFor(const QString &sourceFilePath);

    // Phase 4 (2026-09-04): 蒙版 setter
    //   addMask: 加载一张灰度图当蒙版, 同步 enable
    //   clearMask: 删蒙版 (但保留 enable 状态)
    //   enableMask: 开关蒙版 (不删图)
    //   loadMaskFromImage: 从任意彩色图生成灰度蒙版 (转灰度再二值化或保留)
    bool addMask(int index, const cv::Mat &grayMask);
    bool clearMask(int index);
    bool enableMask(int index, bool enabled);

    // P1.3.2 (2026-09-16): 扩展 LayerMask 接口 (pixel/vector/density/feather/invert)
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

    // ---- 渲染 ----
    cv::Mat render() const;                 // 所有可见层按 zOrder + blend 混合
    cv::Mat renderOne(int index) const;     // 单独预览某层 (含 opacity)

    // P0-2.5 (2026-09-08): 异步 render — 走 EngineContext background 池
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

    // Phase 3 (2026-09-04): 画布尺寸
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
    int  m_selection = -1;
    // Phase 3: 画布尺寸 (无 base layer 时 rasterize text/vector 用)
    cv::Size m_canvasSize = cv::Size(800, 600);

    // P1.4.4 (2026-09-17): SmartObject -> SmartFilter sub-layer indices.
    //   Each SmartObject at index `smartIdx` has a QList of filter sub-layer
    //   indices in chain order (0..N-1). Filter sub-layers are full Layer
    //   entries in m_layers (so they show up in LayerPanel) but tagged with
    //   parentSmartIndex so the renderer knows to skip them as standalone.
    QHash<int, QList<int>> m_smartFilterChain;

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

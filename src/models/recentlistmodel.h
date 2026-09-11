#ifndef RECENTLISTMODEL_H
#define RECENTLISTMODEL_H

#include "recentmanager.h"

#include <QAbstractTableModel>

class RecentListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColFileName = 0,
        ColFilePath,
        ColFileType,
        ColLastEditTime,
        ColPin,        // 固定/取消固定
        ColDelete,     // 删除
        ColumnCount
    };

    enum Role {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        FileTypeRole,
        LastEditTimeRole,
        LastOpenTimeRole,
        PinnedRole,
    };

    explicit RecentListModel(QObject *parent = nullptr);

    // 设置当前显示的模式 (调用后会自动 refresh, 后续 RecentManager::changed 触发也按此模式过滤)
    // mode: 0=建模 1=多媒体  -1=全部
    void setMode(int mode);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 暴露给 view 的便利接口: 把视觉 index 映射到 source model 之后读 path/pinned
    QModelIndex sourceIndexForRow(int proxyRow) const;

    // 给 delegate 用: 在 source model 上根据 path 找行
    int sourceRowForPath(const QString &path) const;

public slots:
    void refresh();

signals:
    void pinClicked(const QString &path);
    void deleteClicked(const QString &path);

private:
    QList<RecentEntry> m_entries;
    // 默认多媒体 (3D 模块已移除, 2026-09-02)
    int m_mode = int(RecentModeMultimedia);
};

#endif // RECENTLISTMODEL_H

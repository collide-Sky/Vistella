#include "recentlistmodel.h"

#include <QLocale>

RecentListModel::RecentListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    connect(&RecentManager::instance(), &RecentManager::changed,
            this, &RecentListModel::refresh);
    refresh();
}

void RecentListModel::setMode(int mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    refresh();
}

void RecentListModel::refresh()
{
    beginResetModel();
    m_entries = RecentManager::instance().entriesForMode(RecentMode(m_mode));
    endResetModel();
}

int RecentListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

int RecentListModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant RecentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.row() < 0 || index.row() >= m_entries.size())
        return {};
    if (index.column() < 0 || index.column() >= ColumnCount)
        return {};

    const RecentEntry &e = m_entries.at(index.row());

    // 前 4 列的 DisplayRole: 把数据格式化成可显示字符串
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:
            return e.fileName;
        case ColFilePath:
            return e.filePath;
        case ColFileType:
            return e.fileType;
        case ColLastEditTime:
            if (!e.lastEditTime.isValid())
                return QStringLiteral("-");
            return e.lastEditTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        case ColPin:
        case ColDelete:
            return QString(); // 操作列, 由委托画
        }
    }

    // 自定义 role
    switch (role) {
    case FilePathRole:     return e.filePath;
    case FileNameRole:     return e.fileName;
    case FileTypeRole:     return e.fileType;
    case LastEditTimeRole: return e.lastEditTime;
    case LastOpenTimeRole: return e.lastOpenTime;
    case PinnedRole:       return e.pinned;
    }
    return {};
}

bool RecentListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(index);
    Q_UNUSED(value);
    Q_UNUSED(role);
    return false;
}

QVariant RecentListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation != Qt::Horizontal)
        return {};

    switch (section) {
    case ColFileName:      return tr("文件名");
    case ColFilePath:      return tr("文件路径");
    case ColFileType:      return tr("类型");
    case ColLastEditTime:  return tr("最后修改时间");
    case ColPin:           return tr("固定");
    case ColDelete:        return tr("删除");
    }
    return {};
}

QHash<int, QByteArray> RecentListModel::roleNames() const
{
    return {
        { FilePathRole,     "filePath"     },
        { FileNameRole,     "fileName"     },
        { FileTypeRole,     "fileType"     },
        { LastEditTimeRole, "lastEditTime" },
        { LastOpenTimeRole, "lastOpenTime" },
        { PinnedRole,       "pinned"       },
    };
}

QModelIndex RecentListModel::sourceIndexForRow(int proxyRow) const
{
    // 模型本身是 source, 直接返回 index(proxyRow, 0)
    return index(proxyRow, 0);
}

int RecentListModel::sourceRowForPath(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath == path)
            return i;
    }
    return -1;
}

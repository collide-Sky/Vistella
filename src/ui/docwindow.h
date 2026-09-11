#ifndef DOCWINDOW_H
#define DOCWINDOW_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class DocWindow; }
QT_END_NAMESPACE

class QPlainTextEdit;

class DocWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DocWindow(QWidget *parent = nullptr);
    ~DocWindow() override;

    // 加载 / 设置文件
    bool loadFile(const QString &path, QString *err = nullptr);
    bool saveFile(QString *err = nullptr);   // 存为原文件 (若没路径则另存为)
    bool saveAsFile(const QString &path, QString *err = nullptr);

    QString filePath() const { return m_filePath; }
    bool    isDirty()  const { return m_dirty; }

    // 给最近历史用的 "标题" - 带 * 标记脏
    QString displayTitle() const;

    QPlainTextEdit *editor() const;

public slots:
    void markDirty();   // 由外部连接 textChanged 触发
    void setClean();

signals:
    void dirtyChanged(bool dirty);
    void filePathChanged(const QString &path);
    void editTimeShouldUpdate(const QString &path);

private:
    Ui::DocWindow   *ui;
    QString          m_filePath;
    bool             m_dirty = false;
};

#endif // DOCWINDOW_H

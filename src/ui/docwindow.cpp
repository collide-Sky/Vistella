#include "docwindow.h"
#include "ui_docwindow.h"

#include "recentmanager.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTextStream>

DocWindow::DocWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DocWindow)
{
    ui->setupUi(this);

    ui->editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    connect(ui->editor, &QPlainTextEdit::textChanged,
            this, &DocWindow::markDirty);
}

DocWindow::~DocWindow()
{
    delete ui;
}

QString DocWindow::displayTitle() const
{
    // 默认只新建 txt 文本, 未保存时显示 "未命名.txt" 明确语义
    QString base = m_filePath.isEmpty()
        ? tr("未命名.txt")
        : QFileInfo(m_filePath).fileName();
    if (m_dirty)
        base.prepend(QLatin1String("*"));
    return base;
}

void DocWindow::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged(true);
    }
}

void DocWindow::setClean()
{
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged(false);
    }
}

bool DocWindow::loadFile(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    ui->editor->setPlainText(in.readAll());
    f.close();

    m_filePath = path;
    ui->editor->document()->setModified(false);
    setClean();
    emit filePathChanged(m_filePath);
    return true;
}

bool DocWindow::saveFile(QString *err)
{
    if (m_filePath.isEmpty())
        return saveAsFile(QString(), err);

    return saveAsFile(m_filePath, err);
}

bool DocWindow::saveAsFile(const QString &path, QString *err)
{
    QString target = path;
    if (target.isEmpty())
        return false;

    // Atomic write (M1-4).
    //   The old code used QFile + Truncate, which destroys the previous
    //   content the moment the file is opened. A failure after that point
    //   (disk full, permissions, a crash) left an empty or half-written
    //   file behind, and because the QTextStream flush result was never
    //   checked, saveAsFile could still return true -- marking a truncated
    //   document as saved.
    //   QSaveFile writes to a temporary file and only replaces the target
    //   on commit(), so the old content survives any failure.
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << ui->editor->toPlainText();
        // Flush the encoder and check it: a full disk fails HERE, not at
        // close time.
        out.flush();
        if (out.status() != QTextStream::Ok) {
            if (err) *err = f.errorString();
            f.cancelWriting();
            return false;
        }
    }
    if (!f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }

    const bool pathChanged = (m_filePath != target);
    m_filePath = target;
    ui->editor->document()->setModified(false);
    setClean();
    if (pathChanged)
        emit filePathChanged(m_filePath);

    // 通知 RecentManager 更新保存时间
    RecentManager::instance().touchEdit(m_filePath);
    emit editTimeShouldUpdate(m_filePath);
    return true;
}

QPlainTextEdit *DocWindow::editor() const
{
    return ui->editor;
}

#include "docwindow.h"
#include "ui_docwindow.h"

#include "recentmanager.h"

#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
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
    // 阶段 0 第 5 步: 默认只新建 txt 文本, 未保存时显示 "未命名.txt" 明确语义
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

    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << ui->editor->toPlainText();
    f.close();

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

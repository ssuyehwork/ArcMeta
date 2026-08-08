#include "BatchCreateDialog.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QFile>

namespace ArcMeta {

BatchCreateDialog::BatchCreateDialog(const QString& currentDirectory, QWidget* parent)
    : FramelessDialog("批量创建", parent), m_currentDir(currentDirectory) {
    setFixedWidth(450);
    initContent();
}

void BatchCreateDialog::initContent() {
    QWidget* content = getContentArea();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    QLabel* hintLabel = new QLabel("请输入要创建的项目名称（每行一个）。若带有后缀（如 .txt）则创建文件，否则创建文件夹：", this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet("color: #AAAAAA; font-size: 12px;");
    layout->addWidget(hintLabel);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText("新建文件夹1\n新建文件2.txt\n文档3.md");
    m_textEdit->setFixedHeight(180);
    m_textEdit->setStyleSheet(
        "QPlainTextEdit { "
        "  background-color: #252526; "
        "  color: #F1F1F1; "
        "  border: 1px solid #3E3E42; "
        "  border-radius: 4px; "
        "  padding: 8px; "
        "  font-family: 'Segoe UI', Microsoft YaHei; "
        "  font-size: 12px; "
        "}"
        "QPlainTextEdit:focus { border: 1px solid #007ACC; }"
    );
    layout->addWidget(m_textEdit);

    QHBoxLayout* bottomL = new QHBoxLayout();
    bottomL->addStretch();

    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { "
        "  background-color: #3E3E42; "
        "  color: #F1F1F1; "
        "  border: 1px solid #555555; "
        "  border-radius: 4px; "
        "  padding: 6px 16px; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { background-color: #4E4E52; }"
    );
    bottomL->addWidget(btnCancel);

    QPushButton* btnOk = new QPushButton("创建", this);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet(
        "QPushButton { "
        "  background-color: #007ACC; "
        "  color: #FFFFFF; "
        "  border: none; "
        "  border-radius: 4px; "
        "  padding: 6px 20px; "
        "  font-weight: bold; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { background-color: #1C97EA; }"
    );
    bottomL->addWidget(btnOk);
    layout->addLayout(bottomL);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &BatchCreateDialog::onExecute);
}

void BatchCreateDialog::onExecute() {
    QString text = m_textEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "输入内容不能为空！", 1500, QColor("#E81123"));
        return;
    }

    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    int folderCreated = 0;
    int fileCreated = 0;

    QDir dir(m_currentDir);

    for (QString line : lines) {
        QString name = line.trimmed();
        if (name.isEmpty()) continue;

        // 识别是否具有后缀名
        bool hasSuffix = false;
        int dotIdx = name.lastIndexOf('.');
        if (dotIdx > 0 && dotIdx < name.length() - 1) {
            hasSuffix = true;
        }

        QString targetPath = dir.absoluteFilePath(name);

        if (hasSuffix) {
            // 安全避让生成物理空白文件
            QFileInfo fi(targetPath);
            QString base = fi.completeBaseName();
            QString ext = fi.suffix();
            int counter = 1;
            while (QFile::exists(targetPath)) {
                targetPath = dir.absoluteFilePath(QString("%1(%2).%3").arg(base).arg(counter++).arg(ext));
            }
            QFile file(targetPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                fileCreated++;
            }
        } else {
            // 安全避让生成物理子目录
            int counter = 1;
            QString baseName = name;
            while (QDir(targetPath).exists()) {
                targetPath = dir.absoluteFilePath(QString("%1(%2)").arg(baseName).arg(counter++));
            }
            if (QDir().mkpath(targetPath)) {
                folderCreated++;
            }
        }
    }

    QString finishMsg = QString("批量创建成功：文件夹 %1，文件 %2").arg(folderCreated).arg(fileCreated);
    ToolTipOverlay::instance()->showText(QCursor::pos(), finishMsg, 2000, Style::SuccessGreen);
    accept();
}

} // namespace ArcMeta

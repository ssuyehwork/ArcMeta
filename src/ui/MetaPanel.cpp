#include "MetaPanel.h"
#include "SvgIcons.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QRegularExpressionValidator>
#include <QDir>
#include <QFile>

namespace ArcMeta {

MetaPanel::MetaPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("MetadataContainer"); 
    setAttribute(Qt::WA_StyledBackground, true); 
    setMinimumWidth(230); 
    setStyleSheet("color: #EEEEEE;");
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0);
    
    m_adjustTimer = new QTimer(this);
    m_adjustTimer->setSingleShot(true);
    m_adjustTimer->setInterval(50);
    connect(m_adjustTimer, &QTimer::timeout, this, &MetaPanel::adjustFlowHeights);

    setContextMenuPolicy(Qt::CustomContextMenu);
    initUi();
}

void MetaPanel::initUi() {
    QWidget* header = new QWidget(this); 
    header->setObjectName("ContainerHeader"); 
    header->setFixedHeight(32);
    header->setStyleSheet("QWidget#ContainerHeader { background-color: #252526; border-bottom: 1px solid #333; }");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0);
    headerLayout->setSpacing(5);
    QLabel* iconLabel = new QLabel(header); 
    iconLabel->setPixmap(UiHelper::getIcon("all_data", QColor("#4a90e2"), 18).pixmap(18, 18)); 
    headerLayout->addWidget(iconLabel);
    QLabel* titleLabel = new QLabel("元数据", header); 
    titleLabel->setStyleSheet("font-size: 12px; color: #4a90e2; background: transparent; border: none;"); 
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    m_mainLayout->addWidget(header);

    m_scrollArea = new QScrollArea(this); 
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidgetResizable(true); 
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    
    m_container = new QWidget(m_scrollArea); 
    m_containerLayout = new QVBoxLayout(m_container); 
    m_containerLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_containerLayout->setContentsMargins(10, 10, 10, 10); 
    m_containerLayout->setSpacing(8);
    
    m_paletteBox = new QWidget(m_container);
    m_paletteBox->setObjectName("PaletteBox");
    m_paletteBox->setMinimumHeight(28);
    m_paletteBox->setStyleSheet("QWidget#PaletteBox { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; }");
    
    m_paletteFlowLayout = new FlowLayout(m_paletteBox, 6, 6, 6);
    m_paletteFlowLayout->setContentsMargins(10, 6, 10, 6);
    m_containerLayout->addWidget(m_paletteBox);

    m_nameEdit = new ElasticEdit(m_container);
    m_nameEdit->setPlaceholderText("文件名称...");
    m_nameEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #EEEEEE; font-weight: normal; }");
    m_nameEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_nameEdit);

    m_noteEdit = new ElasticEdit(m_container);
    m_noteEdit->setPlaceholderText("添加备注说明...");
    m_noteEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #AAAAAA; font-weight: normal; }");
    m_noteEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_noteEdit);

    m_linkEdit = new ElasticEdit(m_container);
    m_linkEdit->setPlaceholderText("添加链接...");
    m_linkEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #4a90e2; font-weight: normal; }");
    m_linkEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_linkEdit);

    m_tagBox = new QWidget(m_container); 
    QVBoxLayout* tagL = new QVBoxLayout(m_tagBox); 
    tagL->setContentsMargins(0, 0, 0, 0); 
    tagL->setSpacing(6); 
 
    // 1. 上半部分：输入框 
    m_tagEdit = new ElasticEdit(m_tagBox); 
    m_tagEdit->setPlaceholderText("输入标签..."); 
    m_tagEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #AAAAAA; font-weight: normal; }"); 
    connect(m_tagEdit, &ElasticEdit::returnPressed, this, &MetaPanel::onTagAdded); 
    m_tagEdit->installEventFilter(this); 
    tagL->addWidget(m_tagEdit); 
 
    // 2. 下半部分：已打上的标签展示区 
    m_tagContainer = new QWidget(m_tagBox); 
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 4, 4); 
    tagL->addWidget(m_tagContainer); 
 
    m_containerLayout->addWidget(m_tagBox); 

    m_categoryEdit = new ElasticEdit(m_container);
    m_categoryEdit->setReadOnly(true);
    m_categoryEdit->setPlaceholderText("所属分类...");
    m_categoryEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 8px; font-size: 12px; color: #EEEEEE; font-weight: normal; }");
    m_containerLayout->addWidget(m_categoryEdit);

    m_categoryBox = new QWidget(m_container);
    QVBoxLayout* catBoxL = new QVBoxLayout(m_categoryBox);
    catBoxL->setContentsMargins(0, 0, 0, 0);
    catBoxL->setSpacing(4);

    m_categoryContainer = new QWidget(m_categoryBox);
    m_categoryFlowLayout = new FlowLayout(m_categoryContainer, 0, 4, 4);
    catBoxL->addWidget(m_categoryContainer);

    m_categoryLayoutBox = catBoxL;
    m_containerLayout->addWidget(m_categoryBox);

    m_containerLayout->addWidget(createSeparator());

    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("尺寸", lblDimensions);
    addInfoRow("创建时间", lblCtime); 
    addInfoRow("修改时间", lblMtime); 
    addInfoRow("访问时间", lblAtime);
    
    QWidget* pathRow = new QWidget(m_container); 
    QHBoxLayout* pathL = new QHBoxLayout(pathRow);
    pathL->setContentsMargins(0, 2, 0, 2); 
    pathL->setSpacing(8);
    QLabel* pathKey = new QLabel("物理路径", pathRow);
    pathKey->setFixedWidth(80);
    pathKey->setStyleSheet("font-size: 12px; color: #888888;");
    pathL->addWidget(pathKey, 0, Qt::AlignTop);
    
    m_pathEdit = new ElasticEdit(pathRow);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setStyleSheet("QTextEdit { background: transparent; border: none; padding: 0; font-size: 12px; color: #CCCCCC; }");
    pathL->addWidget(m_pathEdit, 1);
    m_containerLayout->addWidget(pathRow);

    addInfoRow("加密状态", lblEncrypted);

    m_containerLayout->addStretch(1);
    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea);
}

void MetaPanel::addInfoRow(const QString& label, QLabel*& valueLabel) {
    QWidget* row = new QWidget(m_container); 
    QHBoxLayout* rl = new QHBoxLayout(row); 
    rl->setContentsMargins(0, 2, 0, 2); 
    rl->setSpacing(8); 
    
    QLabel* kl = new QLabel(label, row); 
    kl->setFixedWidth(80);
    kl->setStyleSheet("font-size: 12px; color: #888888;"); 
    rl->addWidget(kl, 0, Qt::AlignTop);

    valueLabel = new QLabel("-", row); 
    valueLabel->setWordWrap(true); 
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setStyleSheet("font-size: 12px; color: #CCCCCC; line-height: 1.5;");
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop); 
    rl->addWidget(valueLabel, 1); 
    
    m_containerLayout->addWidget(row);
}

QFrame* MetaPanel::createSeparator() {
    QFrame* l = new QFrame(this); 
    l->setFrameShape(QFrame::HLine); 
    l->setFixedHeight(1); 
    l->setStyleSheet("background-color: #333333; border: none; margin: 4px 0;"); 
    return l; 
}

void MetaPanel::onTagAdded() { 
    QString text = m_tagEdit->toPlainText().trimmed(); 
    if (text.isEmpty() || m_selectedPaths.isEmpty()) return; 
 
    // 查重 
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) { 
        TagPill* pill = qobject_cast<TagPill*>(m_tagFlowLayout->itemAt(i)->widget()); 
        if (pill && pill->property("tagText").toString() == text) { 
            m_tagEdit->clear(); 
            m_tagEdit->adjustHeight(); 
            return; 
        } 
    } 
 
    // 本地 0 毫秒瞬时生成 2px 胶囊 
    TagPill* pill = new TagPill(text, m_tagContainer); 
    pill->setProperty("tagText", text); 
    pill->setStyleSheet("QFrame { background-color: #2D2D30; border: 1px solid #3E3E42; border-radius: 2px; }"); 
    connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted); 
    m_tagFlowLayout->addWidget(pill); 
 
    m_tagEdit->clear(); 
    m_tagEdit->adjustHeight(); 
    adjustFlowHeights(); 
    if (m_container) m_container->adjustSize(); 
 
    emit tagAddRequested(m_selectedPaths, text); 
} 
 
void MetaPanel::onTagDeleted(const QString& text) { 
    if (m_selectedPaths.isEmpty()) return; 
 
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) { 
        QLayoutItem* item = m_tagFlowLayout->itemAt(i); 
        TagPill* pill = qobject_cast<TagPill*>(item->widget()); 
        if (pill && pill->property("tagText").toString() == text) { 
            m_tagFlowLayout->takeAt(i); 
            pill->deleteLater(); 
            delete item; 
            break; 
        } 
    } 
 
    adjustFlowHeights(); 
    if (m_container) m_container->adjustSize(); 
 
    emit tagRemoveRequested(m_selectedPaths, text); 
} 

void MetaPanel::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    QTimer::singleShot(0, this, [this]() {
        if (!m_scrollArea || !m_container) return;
        int viewportW = m_scrollArea->viewport()->width();
        if (viewportW < 100) return;

        if (m_container->width() != viewportW) {
            m_container->setFixedWidth(viewportW);
        }
        
        int maxW = viewportW - 20; 
        if (maxW > 50) {
            auto syncWidthAndHeight = [maxW](ElasticEdit* edit) {
                if (edit && edit->width() != maxW) {
                    edit->setFixedWidth(maxW);
                    edit->adjustHeight();
                }
            };

            syncWidthAndHeight(m_nameEdit);
            syncWidthAndHeight(m_noteEdit);
            syncWidthAndHeight(m_linkEdit);
            syncWidthAndHeight(m_tagEdit);
            syncWidthAndHeight(m_categoryEdit);
            
            int pathW = maxW - 88;
            if (m_pathEdit && pathW > 0) {
                m_pathEdit->setFixedWidth(pathW);
                m_pathEdit->adjustHeight();
            }
            
            if (m_paletteBox) m_paletteBox->setFixedWidth(maxW);
            if (m_tagBox) m_tagBox->setFixedWidth(maxW);
            if (m_tagContainer) m_tagContainer->setFixedWidth(maxW);
            if (m_categoryBox) m_categoryBox->setFixedWidth(maxW);
            if (m_categoryContainer) m_categoryContainer->setFixedWidth(maxW);
            
            adjustFlowHeights();
            m_container->adjustSize();
        }
    });
}

void MetaPanel::adjustFlowHeights() {
    if (m_paletteBox && m_paletteFlowLayout) {
        int contentH = m_paletteFlowLayout->heightForWidth(m_paletteBox->width());
        int newH = qMax(28, contentH);
        if (m_paletteBox->height() != newH) {
            m_paletteBox->setFixedHeight(newH);
        }
        m_paletteFlowLayout->activate();
    }
    if (m_tagContainer && m_tagFlowLayout) {
        int contentH = m_tagFlowLayout->heightForWidth(m_tagContainer->width());
        if (m_tagContainer->height() != contentH) {
            m_tagContainer->setFixedHeight(contentH);
        }
        m_tagFlowLayout->activate();
    }
    if (m_categoryContainer && m_categoryFlowLayout) {
        int contentH = m_categoryFlowLayout->heightForWidth(m_categoryContainer->width());
        if (m_categoryContainer->height() != contentH) {
            m_categoryContainer->setFixedHeight(contentH);
        }
        m_categoryFlowLayout->activate();
    }
}

void MetaPanel::showEvent(QShowEvent* event) {
    QFrame::showEvent(event);
    QResizeEvent e(size(), size());
    MetaPanel::resizeEvent(&e);
}

void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, 
                            const QString& ct, const QString& mt, const QString& at, 
                            const QString& p, bool e, int width, int height) {
    if (m_isUserEditing) return;

    m_isInternalUpdating = true;
    
    QFileInfo info(n);
    m_nameEdit->setPlainText(info.completeBaseName());
    m_nameEdit->adjustHeight();
    m_nameEdit->setProperty("oldPath", p);
    m_nameEdit->setProperty("suffix", info.suffix());
    
    lblType->setText(t); 
    lblSize->setText(s); 
    lblCtime->setText(ct); 
    lblMtime->setText(mt); 
    lblAtime->setText(at); 
    
    m_pathEdit->setPlainText(p);
    m_pathEdit->adjustHeight();

    lblEncrypted->setText(e ? "已加密" : "未加密");
    
    if (width > 0 && height > 0) {
        lblDimensions->setText(QString("%1 x %2 像素").arg(width).arg(height));
        if (lblDimensions->parentWidget()) lblDimensions->parentWidget()->show();
    } else {
        lblDimensions->setText("-");
        if (lblDimensions->parentWidget()) lblDimensions->parentWidget()->hide();
    }

    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setDiskPathMode(bool isDiskMode, const QString& rawPath) {
    m_isDiskNavMode = isDiskMode;
    if (m_isDiskNavMode) {
        // 磁盘导航模式：清空胶囊，直接渲染纯文本物理路径
        while (QLayoutItem* item = m_categoryFlowLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        m_categoryEdit->setVisible(true);
        m_categoryEdit->setPlainText(rawPath);
        m_categoryEdit->adjustHeight();
        m_categoryBox->setVisible(false);
    } else {
        // 托管库模式：隐藏原始路径框，展示胶囊布局
        m_categoryEdit->setVisible(false);
        m_categoryBox->setVisible(true);
    }
}

void MetaPanel::setCategoryPills(const std::vector<std::pair<int, QString>>& categories) {
    if (m_isDiskNavMode) return;

    while (QLayoutItem* item = m_categoryFlowLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    for (const auto& cat : categories) {
        int catId = cat.first;
        QString catName = cat.second;

        TagPill* pill = new TagPill(catName, m_categoryBox);
        pill->setStyleSheet("background: #2D2D30; border: 1px solid #3E3E42; color: #EEE; border-radius: 4px;");
        
        connect(pill, &TagPill::deleteRequested, [this, catId]() {
            if (!m_selectedPaths.isEmpty()) {
                emit unbindCategoryRequested(m_selectedPaths.first(), catId);
            }
        });
        m_categoryFlowLayout->addWidget(pill);
    }

    // 追加 "+" 绑定按钮
    QPushButton* btnAddCat = new QPushButton("+", m_categoryBox);
    btnAddCat->setFixedSize(20, 20);
    btnAddCat->setCursor(Qt::PointingHandCursor);
    btnAddCat->setStyleSheet(
        "QPushButton { background: #3E3E42; color: #888; border: none; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #4E4E52; color: #FFF; }"
    );
    connect(btnAddCat, &QPushButton::clicked, [this]() {
        if (!m_selectedPaths.isEmpty()) {
            emit bindCategoryRequested(m_selectedPaths.first());
        }
    });
    m_categoryFlowLayout->addWidget(btnAddCat);
    m_adjustTimer->start();
}

void MetaPanel::setTags(const QStringList& tags) {
    while (QLayoutItem* item = m_tagFlowLayout->takeAt(0)) {
        TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill) {
            pill->hide();
            m_tagPool.append(pill);
        }
        delete item;
    }

    for (const QString& tag : tags) {
        TagPill* pill = nullptr;
        if (!m_tagPool.isEmpty()) {
            pill = m_tagPool.takeFirst();
            pill->setData(tag);
        } else {
            pill = new TagPill(tag, m_tagContainer);
            connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        }
        pill->setProperty("tagText", tag);
        pill->setStyleSheet("QFrame { background-color: #2D2D30; border: 1px solid #3E3E42; border-radius: 2px; }");
        pill->show();
        m_tagFlowLayout->addWidget(pill);
    }
    m_adjustTimer->start();
}

void MetaPanel::setNote(const QString& note) { 
    m_isInternalUpdating = true;
    m_noteEdit->setPlainText(note); 
    m_noteEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setNote(const std::wstring& note) {
    setNote(QString::fromStdWString(note));
}

void MetaPanel::setURL(const QString& url) { 
    m_isInternalUpdating = true;
    m_linkEdit->setPlainText(url); 
    m_linkEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setURL(const std::wstring& url) {
    setURL(QString::fromStdWString(url));
}

void MetaPanel::setCategory(const QString& category) { 
    m_isInternalUpdating = true;
    m_categoryEdit->setPlainText(category); 
    m_categoryEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setPalettes(const QVector<QPair<QColor, float>>& palette) {
    if (!m_paletteFlowLayout) return;

    while (QLayoutItem* item = m_paletteFlowLayout->takeAt(0)) {
        ColorPill* pill = qobject_cast<ColorPill*>(item->widget());
        if (pill) {
            pill->hide();
            m_colorPool.append(pill);
        }
        delete item;
    }

    for (const auto& entry : palette) {
        ColorPill* pill = nullptr;
        if (!m_colorPool.isEmpty()) {
            pill = m_colorPool.takeFirst();
            pill->setData(entry.first, entry.second);
        } else {
            pill = new ColorPill(entry.first, entry.second, m_paletteBox);
            pill->setStyleSheet("background: transparent; border: none;");
            connect(pill, &ColorPill::colorSelected, [this](const QColor& c){ emit searchByColor(c); });
            connect(pill, &ColorPill::requestSetAsPrimary, this, &MetaPanel::setAsPrimaryColor);
        }
        pill->show();
        m_paletteFlowLayout->addWidget(pill);
    }

    m_paletteFlowLayout->invalidate();
    m_paletteBox->update();
    m_adjustTimer->start();
}

bool MetaPanel::eventFilter(QObject* watched, QEvent* event) {
    if (m_isInternalUpdating) return QFrame::eventFilter(watched, event);

    if (event->type() == QEvent::FocusIn) {
        if (watched == m_noteEdit || watched == m_linkEdit || watched == m_nameEdit || watched == m_tagEdit) {
            m_isUserEditing = true;
        }
    } else if (event->type() == QEvent::FocusOut) {
        if (watched == m_noteEdit || watched == m_linkEdit || watched == m_nameEdit || watched == m_tagEdit) {
            m_isUserEditing = false;
        }
    }

    if (watched == m_noteEdit && event->type() == QEvent::FocusOut) {
        if (!m_selectedPaths.isEmpty()) {
            QString newNote = m_noteEdit->toPlainText();
            emit noteEdited(m_selectedPaths, newNote);
        }
    } else if (watched == m_linkEdit && event->type() == QEvent::FocusOut) {
        if (!m_selectedPaths.isEmpty()) {
            QString newUrl = m_linkEdit->toPlainText();
            emit linkEdited(m_selectedPaths, newUrl);
        }
    } else if (watched == m_nameEdit && event->type() == QEvent::FocusOut) {
        QString oldPath = m_nameEdit->property("oldPath").toString();
        QString newName = m_nameEdit->toPlainText().trimmed();
        
        static const QRegularExpression illegalRegex("[\\\\/:*?\"<>|]");
        newName.remove(illegalRegex);
        m_nameEdit->setPlainText(newName);

        QString suffix = m_nameEdit->property("suffix").toString();
        if (!oldPath.isEmpty() && !newName.isEmpty()) {
            QFileInfo oldInfo(oldPath);
            if (newName != oldInfo.completeBaseName()) {
                QString newPath = oldInfo.absolutePath() + "/" + newName + (suffix.isEmpty() ? "" : "." + suffix);
                newPath = QDir::toNativeSeparators(newPath);
                
                if (QFile::exists(newPath)) {
                    m_nameEdit->setPlainText(oldInfo.completeBaseName());
                    return true;
                }

                emit renameRequested(oldPath, newPath);
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MetaPanel::setAsPrimaryColor(const QColor& color) {
    QString currentPath = m_pathEdit->toPlainText().trimmed();
    if (!currentPath.isEmpty()) {
        emit primaryColorChanged(currentPath, color);
    }
}

} // namespace ArcMeta

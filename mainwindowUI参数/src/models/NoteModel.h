#ifndef NOTEMODEL_H
#define NOTEMODEL_H

#include <QAbstractListModel>
#include <QVariantMap>
#include <QList>
#include <QMimeData>

class NoteModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum NoteRoles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ContentRole,
        TagsRole,
        TimeRole,
        PinnedRole,
        LockedRole,
        FavoriteRole,
        TypeRole,
        RatingRole,
        CategoryIdRole,
        CategoryNameRole,
        ColorRole,
        SourceAppRole,
        SourceTitleRole,
        BlobRole,
        RemarkRole,
        PlainContentRole // [PERF] 预处理后的纯文本角色，用于 Delegate 零计算渲染
    };

    explicit NoteModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    // 全量重置
    void setNotes(const QList<QVariantMap>& notes);
    
    // 【新增】增量插入 (这就是报错缺失的函数！)
    void prependNote(const QVariantMap& note);
    void updateCategoryMap();

private:
    QList<QVariantMap> m_notes;
    QMap<int, QString> m_categoryMap;
    mutable QMap<int, QIcon> m_thumbnailCache;
    mutable QMap<int, QString> m_tooltipCache;
    mutable QMap<int, QString> m_plainContentCache;
};

#endif // NOTEMODEL_H
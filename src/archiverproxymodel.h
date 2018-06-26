#ifndef ARCHIVERPROXYMODEL_H
#define ARCHIVERPROXYMODEL_H

#include <QSortFilterProxyModel>

class ArchiverProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ArchiverProxyModel(QObject *parent = nullptr);

    bool folderFirst() const;

    void setFolderFirst(bool folderFirst);

protected:
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;

private:
    bool folderFirst_;
};

#endif // ARCHIVERPROXYMODEL_H

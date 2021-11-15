#include "archiverproxymodel.h"

// for MainWindow::ArchiverItemRole
// FIXME: better to move this to its own module outside main window
#include "mainwindow.h"
#include "archiveritem.h"


ArchiverProxyModel::ArchiverProxyModel(QObject *parent):
    QSortFilterProxyModel (parent), folderFirst_{true} {
    folderFirst_ = true;
    collator_.setNumericMode(true);
}

bool ArchiverProxyModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const {
    auto leftFirstCol = source_left.sibling(source_left.row(), 0);
    auto rightFirstCol = source_right.sibling(source_right.row(), 0);
    if(leftFirstCol.isValid() && rightFirstCol.isValid()) {
        auto leftItem = leftFirstCol.data(MainWindow::ArchiverItemRole).value<const ArchiverItem*>();
        auto rightItem = rightFirstCol.data(MainWindow::ArchiverItemRole).value<const ArchiverItem*>();
        if(leftItem) {
            if(leftItem->isDir()) {
                if(folderFirst_ && !(rightItem && rightItem->isDir())) {
                    // put folder before non-folder items
                    return (sortOrder() == Qt::AscendingOrder);
                }
                else if(leftFirstCol.data().toString() == QStringLiteral("..")) {
                    // the item ".." should always be the first
                    return (sortOrder() == Qt::AscendingOrder);
                }
            }
        }
        if(rightItem) {
            if(rightItem->isDir()) {
                if(folderFirst_ && !(leftItem && leftItem->isDir())) {
                    return (sortOrder() != Qt::AscendingOrder);
                }
                else if(rightFirstCol.data().toString() == QStringLiteral("..")) {
                    return (sortOrder() != Qt::AscendingOrder);
                }
            }
        }
        // apply other sorting criteria, especially for the columns other than the first one
        if(sortColumn() != 0) {
            auto leftSortColumn = source_left.sibling(source_left.row(), sortColumn());
            auto rightSortColumn = source_right.sibling(source_right.row(), sortColumn());
            if(leftSortColumn.isValid() && rightSortColumn.isValid()) {
                if(leftItem && rightItem) {
                    auto str = leftSortColumn.data(MainWindow::ArchiverItemRole).toString();
                    if(str == QStringLiteral("size")) {
                        if(leftItem->size() != rightItem->size()) {
                            return (leftItem->size() < rightItem->size());
                        }
                    }
                    else if(str == QStringLiteral("mTime")) {
                        if(leftItem->modifiedTime() != rightItem->modifiedTime()) {
                            return (leftItem->modifiedTime() < rightItem->modifiedTime());
                        }
                    }
                    else {
                        // as the last resort, compare texts (e.g., types for the file type column)
                        int comp = collator_.compare(leftSortColumn.data().toString(),
                                                     rightSortColumn.data().toString());
                        if(comp != 0) {
                            return comp < 0;
                        }
                    }
                }
            }
        }
        // fall back to the texts of the first column (usually, file names)
        return collator_.compare(leftFirstCol.data().toString(), rightFirstCol.data().toString()) < 0;
    }
    return QSortFilterProxyModel::lessThan(source_left, source_right);
}

bool ArchiverProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    if(filterStr_.isEmpty()) {
        return true;
    }
    if(auto model = sourceModel()) {
        auto indx = model->index(source_row, 0, source_parent);
        if(indx.isValid()) {
            auto firstCol = indx.sibling(source_row, 0);
            if(const ArchiverItem* item = firstCol.data(MainWindow::ArchiverItemRole).value<const ArchiverItem*>()) {
                if(QString::fromUtf8(item->name()).contains(filterStr_, Qt::CaseInsensitive)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ArchiverProxyModel::setFilterStr(QString str) {
    if(filterStr_ != str) {
        filterStr_ = str;
        invalidate();
    }
}

bool ArchiverProxyModel::folderFirst() const {
    return folderFirst_;
}

void ArchiverProxyModel::setFolderFirst(bool folderFirst) {
    folderFirst_ = folderFirst;
    invalidate();
}

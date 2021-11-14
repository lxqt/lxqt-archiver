#include <QHeaderView>
#include <QScrollBar>
#include <QApplication>

#include "dirtreeView.h"

DirTreeView::DirTreeView(QWidget* parent) : QTreeView(parent) {
//    setIconSize(QSize(24, 24));
    header()->setOffset(0);
    setHeaderHidden(true);
    // show the horizontal scrollbar if needed
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

// Ensure that the item is visible horizontally too (Qt's default behavior is buggy).
void DirTreeView::scrollTo(const QModelIndex &index, QAbstractItemView::ScrollHint hint) {
    QTreeView::scrollTo (index, hint);
    if (index.isValid())
    {
        int viewportWidth = viewport()->width();
        QRect vr = visualRect (index);
        int itemWidth = vr.width() + indentation();
        int hPos;
        if (QApplication::layoutDirection() == Qt::RightToLeft) {
            hPos = viewportWidth - vr.x() - vr.width() - indentation(); // horizontally mirrored
        }
        else {
            hPos = vr.x() - indentation();
        }
        if (hPos < 0 || itemWidth > viewportWidth) {
            horizontalScrollBar()->setValue (hPos);
        }
        else if (hPos + itemWidth > viewportWidth) {
            horizontalScrollBar()->setValue (hPos + itemWidth - viewportWidth);
        }
    }
}

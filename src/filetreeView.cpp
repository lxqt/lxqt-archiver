#include <QHeaderView>
#include <QMouseEvent>
#include <QApplication>

#include "filetreeView.h"

FileTreeView::FileTreeView(QWidget* parent) : QTreeView(parent) {
    dragStarted_ = false;
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setRootIsDecorated(false);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSortingEnabled(true);
    header()->setSortIndicatorShown(true);
    header()->setStretchLastSection(false);
    header()->setSortIndicatorShown(true);
    header()->setSortIndicator(0, Qt::AscendingOrder);
}

void FileTreeView::mousePressEvent(QMouseEvent* event) {
    QTreeView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && indexAt(event->pos()).isValid()) {
        dragStartPosition_ = event->pos();
    }
    else {
        dragStartPosition_ = QPoint();
    }
    dragStarted_ = false;
}

void FileTreeView::mouseReleaseEvent(QMouseEvent* event)
{
    // Do not activate by right clicking because
    // context menus may be shown on releasing right mouse button.
    if(event->button() == Qt::RightButton) {
        event->ignore();
        return;
    }

    QTreeView::mouseReleaseEvent(event);
}

void FileTreeView::mouseMoveEvent(QMouseEvent* event) {
    if(dragStartPosition_.isNull()) {
        QTreeView::mouseMoveEvent(event);
        return;
    }
    if(!dragStarted_
       && (event->buttons() & Qt::LeftButton)
       && (event->pos() - dragStartPosition_).manhattanLength() >= qMax(16, QApplication::startDragDistance())) {
        dragStarted_ = true;
        if(selectionModel() && !selectionModel()->selectedRows().isEmpty()) {
            Q_EMIT dragStarted();
        }
        event->accept();
    }
}

void FileTreeView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if(currentIndex().isValid()) {
            // instead of AbstractItemView::activated(), emit our signal,
            // which works with Enter and Return alike
            Q_EMIT enterPressed();
            event->accept();
            return;
        }
    }
    QTreeView::keyPressEvent(event);
}

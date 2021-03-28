#ifndef DIRTREEVIEW_H
#define DIRTREEVIEW_H

#include <QTreeView>

class DirTreeView : public QTreeView {
  Q_OBJECT

public:
    explicit DirTreeView(QWidget* parent = nullptr);

    void scrollTo(const QModelIndex &index, QAbstractItemView::ScrollHint hint = QAbstractItemView::EnsureVisible) override;
};

#endif // DIRTREEVIEW_H

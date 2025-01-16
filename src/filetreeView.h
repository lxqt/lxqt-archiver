#ifndef FILETREEVIEW_H
#define FILETREEVIEW_H

#include <QTreeView>

class FileTreeView : public QTreeView {
  Q_OBJECT

public:
    explicit FileTreeView(QWidget* parent = nullptr);

Q_SIGNALS:
    void dragStarted();
    void enterPressed();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPoint dragStartPosition_;
    bool dragStarted_;
};

#endif // FILETREEVIEW_H

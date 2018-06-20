#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <memory>

#include "archiver.h"

namespace Ui {
class MainWindow;
}

class QProgressBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    ~MainWindow();

    std::shared_ptr<Archiver> archiver() const;

private Q_SLOTS:
    // action slots
    void on_actionCreateNew_triggered(bool checked);

    void on_actionOpen_triggered(bool checked);

    void on_actionAddFiles_triggered(bool checked);

    void on_actionAddFolder_triggered(bool checked);

    void on_actionDelete_triggered(bool checked);

    void on_actionSelectAll_triggered(bool checked);

    void on_actionExtract_triggered(bool checked);

    void on_actionTest_triggered(bool checked);

    void on_actionReload_triggered(bool checked);

    void on_actionAbout_triggered(bool checked);

private Q_SLOTS:

    void onActionStarted(FrAction action);

    void onActionProgress(double fraction);

    void onActionFinished(FrAction action, ArchiverError err);

    void onMessage(QString message);

    void onStoppableChanged(bool stoppable);

private:
    void setFileName(const QString& fileName);

    void showFlatFileList();

    void setBusyState(bool busy);

    void updateUiStates();

    std::vector<const FileData*> selectedFiles();

private:
    std::unique_ptr<Ui::MainWindow> ui_;
    std::shared_ptr<Archiver> archiver_;
    QProgressBar* progressBar_;
};

#endif // MAINWINDOW_H

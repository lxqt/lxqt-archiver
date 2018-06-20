#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QUrl>
#include <QStandardItemModel>
#include <QIcon>
#include <QCursor>
#include <QHeaderView>
#include <QTreeView>
#include <QMessageBox>
#include <QDateTime>
#include <QProgressBar>
#include <QDebug>

#include <libfm-qt/core/mimetype.h>
#include <libfm-qt/core/iconinfo.h>
#include <libfm-qt/utilities.h>

#include "archive.h"


MainWindow::MainWindow(QWidget* parent):
    QMainWindow(parent),
    ui_{new Ui::MainWindow()},
    archiver_{std::make_shared<Archiver>()} {

    ui_->setupUi(this);
    progressBar_ = new QProgressBar{ui_->statusBar};
    ui_->statusBar->addPermanentWidget(progressBar_);
    progressBar_->hide();

    connect(archiver_.get(), &Archiver::start, this, &MainWindow::onActionStarted);
    connect(archiver_.get(), &Archiver::finish, this, &MainWindow::onActionFinished);
    connect(archiver_.get(), &Archiver::progress, this, &MainWindow::onActionProgress);
    connect(archiver_.get(), &Archiver::message, this, &MainWindow::onMessage);

    // we don't support dir tree yet.
    ui_->dirTreeView->hide();

    updateUiStates();
}

MainWindow::~MainWindow() {
}

void MainWindow::setFileName(const QString &fileName) {
    QString title = tr("File Archiver");
    if(!fileName.isEmpty()) {
        title = fileName + " - " + title;
    }
    setWindowTitle(title);
}

void MainWindow::on_actionCreateNew_triggered(bool checked) {
    QFileDialog dlg;
    QUrl url = QFileDialog::getSaveFileUrl(this);
    if(!url.isEmpty()) {
        archiver_->createNewArchive(url);
    }
}

void MainWindow::on_actionOpen_triggered(bool checked) {
    qDebug("open");
    QFileDialog dlg;
    QUrl url = QFileDialog::getOpenFileUrl(this);
    if(!url.isEmpty()) {
        auto uri = url.toString().toUtf8();
        archiver_->openArchive(uri.constData(), nullptr);
    }
}

void MainWindow::on_actionAddFiles_triggered(bool checked) {
    auto fileUrls = QFileDialog::getOpenFileUrls(this);
    if(!fileUrls.isEmpty()) {
        auto srcPaths = Fm::pathListFromQUrls(fileUrls);
        archiver_->addFiles(srcPaths, "/",
                            false, nullptr, false, FR_COMPRESSION_NORMAL, 0);
    }
}

void MainWindow::on_actionAddFolder_triggered(bool checked) {
    QFileDialog dlg;
    QUrl dirUrl = QFileDialog::getExistingDirectoryUrl(this, QString(), QUrl(), (QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog));
    if(!dirUrl.isEmpty()) {
        auto path = Fm::FilePath::fromUri(dirUrl.toEncoded().constData());
        archiver_->addDirectory(path, "/",
                                false, nullptr, false, FR_COMPRESSION_NORMAL, 0);
    }
}

void MainWindow::on_actionDelete_triggered(bool checked) {
    qDebug("delete");
    auto files = selectedFiles();
    if(!files.empty()) {
        archiver_->removeFiles(files, FR_COMPRESSION_NORMAL);
    }
}

void MainWindow::on_actionSelectAll_triggered(bool checked) {
    ui_->fileListView->selectAll();
}

void MainWindow::on_actionExtract_triggered(bool checked) {
    qDebug("extract");
    QFileDialog dlg;
    QUrl dirUrl = QFileDialog::getExistingDirectoryUrl(this, QString(), QUrl(), (QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog));
    if(!dirUrl.isEmpty()) {
        auto files = selectedFiles();
        if(files.empty()) {
            archiver_->extractAll(dirUrl.toEncoded().constData(), false, false, false, nullptr);
        }
        else {
            auto destDir = Fm::FilePath::fromUri(dirUrl.toEncoded().constData());
            archiver_->extractFiles(files, destDir, false, false, false, nullptr);
        }
    }
}

void MainWindow::on_actionTest_triggered(bool checked) {
    if(archiver_->isLoaded()) {
        archiver_->testArchiveIntegrity(nullptr);
    }
}

void MainWindow::on_actionReload_triggered(bool checked) {
    if(archiver_->isLoaded()) {
        archiver_->reloadArchive(nullptr);
    }
}

void MainWindow::on_actionAbout_triggered(bool checked) {
    QMessageBox::about(this, tr("About LXQt Archiver"), tr("File Archiver for LXQt.\n\nCopyright (C) 2018 LXQt team."));
}

void MainWindow::onActionStarted(FrAction action) {
    setBusyState(true);
    progressBar_->setValue(0);
    progressBar_->show();
    progressBar_->setRange(0, 100);
    progressBar_->setFormat(tr("%p %%"));

    qDebug("action start: %d", action);

    switch(action) {
    case FR_ACTION_CREATING_NEW_ARCHIVE:
        qDebug("new archive");
        setFileName(archiver_->archiveDisplayName());
        break;
    case FR_ACTION_LOADING_ARCHIVE:            /* loading the archive from a remote location */
        setFileName(archiver_->archiveDisplayName());
        break;
    case FR_ACTION_LISTING_CONTENT:            /* listing the content of the archive */
        setFileName(archiver_->archiveDisplayName());
        break;
    case FR_ACTION_DELETING_FILES:             /* deleting files from the archive */
        break;
    case FR_ACTION_TESTING_ARCHIVE:            /* testing the archive integrity */
        break;
    case FR_ACTION_GETTING_FILE_LIST:          /* getting the file list (when fr_archive_add_with_wildcard or
                         fr_archive_add_directory are used, we need to scan a directory
                         and collect the files to add to the archive, this
                         may require some time to complete, so the operation
                         is asynchronous) */
        break;
    case FR_ACTION_COPYING_FILES_FROM_REMOTE:  /* copying files to be added to the archive from a remote location */
        break;
    case FR_ACTION_ADDING_FILES:    /* adding files to an archive */
        break;
    case FR_ACTION_EXTRACTING_FILES:           /* extracting files */
        break;
    case FR_ACTION_COPYING_FILES_TO_REMOTE:    /* copying extracted files to a remote location */
        break;
    case FR_ACTION_CREATING_ARCHIVE:           /* creating a local archive */
        break;
    case FR_ACTION_SAVING_REMOTE_ARCHIVE:       /* copying the archive to a remote location */
        break;
    default:
        break;
    }
}

void MainWindow::onActionProgress(double fraction) {
    progressBar_->setValue(int(100 * fraction));
}

void MainWindow::onActionFinished(FrAction action, ArchiverError err) {
    setBusyState(false);
    progressBar_->hide();

    qDebug("action finished: %d", action);

    switch(action) {
    case FR_ACTION_LOADING_ARCHIVE:            /* loading the archive from a remote location */
        qDebug("finish! %d", action);
        break;
    case FR_ACTION_CREATING_NEW_ARCHIVE:  // same as listing empty content
    case FR_ACTION_CREATING_ARCHIVE:           /* creating a local archive */
    case FR_ACTION_LISTING_CONTENT:            /* listing the content of the archive */
        qDebug("content listed");
        showFlatFileList();
        break;
    case FR_ACTION_DELETING_FILES:             /* deleting files from the archive */
        archiver_->reloadArchive(nullptr);
        break;
    case FR_ACTION_TESTING_ARCHIVE:            /* testing the archive integrity */
        if(!err.hasError()) {
            QMessageBox::information(this, tr("Success"), tr("No errors are found."));
        }
        break;
    case FR_ACTION_GETTING_FILE_LIST:          /* getting the file list (when fr_archive_add_with_wildcard or
                         fr_archive_add_directory are used, we need to scan a directory
                         and collect the files to add to the archive, this
                         may require some time to complete, so the operation
                         is asynchronous) */
        break;
    case FR_ACTION_COPYING_FILES_FROM_REMOTE:  /* copying files to be added to the archive from a remote location */
        break;
    case FR_ACTION_ADDING_FILES:    /* adding files to an archive */
        archiver_->reloadArchive(nullptr);
        break;
    case FR_ACTION_EXTRACTING_FILES:           /* extracting files */
        break;
    case FR_ACTION_COPYING_FILES_TO_REMOTE:    /* copying extracted files to a remote location */
        break;
    case FR_ACTION_SAVING_REMOTE_ARCHIVE:       /* copying the archive to a remote location */
        break;
    default:
        break;
    }

    if(err.hasError()) {
        QMessageBox::critical(this, tr("Error"), err.message());
    }
}

void MainWindow::onMessage(QString message) {
    ui_->statusBar->showMessage(message);
}

void MainWindow::onStoppableChanged(bool stoppable) {
    ui_->actionStop->setEnabled(stoppable);
}

// show all files including files in subdirs in a flat list
void MainWindow::showFlatFileList() {
    auto oldModel = ui_->fileListView->model();
    QStandardItemModel* model = new QStandardItemModel{this};
    model->setHorizontalHeaderLabels(QStringList()
                                     << tr("File name")
                                     << tr("File Type")
                                     << tr("File Size")
                                     << tr("Modified")
                                     << tr("*")
    );

    auto n_files = archiver_->fileCount();
    for(int i = 0; i < n_files; ++i) {
        auto file = archiver_->file(i);
        QIcon icon;
        QString desc;
        // get mime type, icon, and description
        auto typeName = file->dir ? "inode/directory" : file->content_type;
        auto mimeType = Fm::MimeType::fromName(typeName);
        if(mimeType) {
            auto iconInfo = mimeType->icon();
            desc = QString::fromUtf8(mimeType->desc());
            if(iconInfo) {
                icon = iconInfo->qicon();
            }
        }

        // mtime
        auto mtime = QDateTime::fromMSecsSinceEpoch(file->modified * 1000);

        // FIXME: filename might not be UTF-8
        QString fullPath = file->full_path;
        model->appendRow(QList<QStandardItem*>()
                         << new QStandardItem{icon, fullPath}
                         << new QStandardItem{desc}
                         << new QStandardItem{Fm::formatFileSize(file->size)}
                         << new QStandardItem{mtime.toString(Qt::SystemLocaleShortDate)}
                         << new QStandardItem{file->encrypted ? QStringLiteral("*") : QString{}}
        );
        qDebug("file: %s\t%s", file->full_path, file->content_type);
    }

    ui_->fileListView->setModel(model);

    if(oldModel) {
        delete oldModel;
    }

    ui_->statusBar->showMessage(tr("%1 files are loaded").arg(n_files));
    ui_->fileListView->header()->setSectionResizeMode(0, QHeaderView::Stretch);

}

void MainWindow::setBusyState(bool busy) {
    if(busy) {
        setCursor(Qt::WaitCursor);
    }
    else {
        setCursor(Qt::ArrowCursor);
    }
    updateUiStates();
}

void MainWindow::updateUiStates() {
    bool hasArchive = archiver_->isLoaded();
    bool inProgress = !archiver_->isActionInProgress();

    bool canLoad = !hasArchive || !inProgress;
    ui_->actionCreateNew->setEnabled(canLoad);
    ui_->actionOpen->setEnabled(canLoad);

    bool canEdit = hasArchive && !inProgress;
    ui_->fileListView->setEnabled(canEdit);
    ui_->actionSaveAs->setEnabled(canEdit);

    ui_->actionSelectAll->setEnabled(canEdit);

    ui_->actionAddFiles->setEnabled(canEdit);
    ui_->actionAddFolder->setEnabled(canEdit);
    ui_->actionDelete->setEnabled(canEdit);

    ui_->actionExtract->setEnabled(canEdit);
}

std::vector<const FileData*> MainWindow::selectedFiles() {
    std::vector<const FileData*> files;
    auto selModel = ui_->fileListView->selectionModel();
    if(selModel) {
        auto selIndexes = selModel->selectedIndexes();
        for(const auto& idx: selIndexes) {
            auto row = idx.row();
            auto file = archiver_->file(row);
            if(file) {
                files.emplace_back(file);
            }
        }
    }
    return files;
}

std::shared_ptr<Archiver> MainWindow::archiver() const {
    return archiver_;
}

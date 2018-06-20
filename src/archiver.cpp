#include "archiver.h"
#include <glib.h>
#include <gobject/gobject.h>

#include "core/fr-command.h"

Archiver::Archiver(QObject* parent):
    QObject(parent),
    impl_{fr_archive_new()} {

    g_signal_connect(impl_, "start", G_CALLBACK(&onStart), this);
    g_signal_connect(impl_, "done", G_CALLBACK(&onDone), this);
    g_signal_connect(impl_, "progress", G_CALLBACK(&onProgress), this);
    g_signal_connect(impl_, "message", G_CALLBACK(&onMessage), this);
    g_signal_connect(impl_, "stoppable", G_CALLBACK(&onStoppable), this);
    g_signal_connect(impl_, "working-archive", G_CALLBACK(&onWorkingArchive), this);
}

Archiver::~Archiver() {
    if(impl_) {
        g_signal_handlers_disconnect_by_data(impl_, this);
        g_object_unref(impl_);
    }
}

bool Archiver::createNewArchive(const char* uri) {
    if(!fr_archive_create(impl_, uri)) {
        // this will trigger a queued finished() signal
        fr_archive_action_completed(impl_,
                         FR_ACTION_CREATING_NEW_ARCHIVE,
                         FR_PROC_ERROR_GENERIC,
                         tr("Archive type not supported.").toUtf8().constData());
        return false;
    }

    // this will trigger a queued finished() signal
    fr_archive_action_completed (impl_,
                     FR_ACTION_CREATING_NEW_ARCHIVE,
                     FR_PROC_ERROR_NONE,
                     NULL);
    return true;
}

bool Archiver::createNewArchive(const QUrl &uri) {
    return createNewArchive(uri.toEncoded().constData());
}

bool Archiver::openArchive(const char* uri, const char* password) {
    return fr_archive_load(impl_, uri, password);
}

bool Archiver::openArchive(const QUrl &uri, const char *password) {
    return openArchive(uri.toEncoded().constData(), password);
}

void Archiver::reloadArchive(const char* password) {
    fr_archive_reload(impl_, password);
}

bool Archiver::isLoaded() const {
    return impl_->file != nullptr;
}

void Archiver::addFiles(GList* relativefileNames, const char* srcDirUri, const char* destDirPath, bool onlyIfNewer, const char* password, bool encrypt_header, FrCompression compression, unsigned int volume_size) {
    fr_archive_add_files(impl_, relativefileNames, srcDirUri, destDirPath, onlyIfNewer, password, encrypt_header, compression, volume_size);
}

void Archiver::addFiles(const Fm::FilePathList &srcPaths, const char* destDirPath, bool onlyIfNewer, const char *password, bool encrypt_header, FrCompression compression, unsigned int volume_size) {
    if(srcPaths.empty()) {
        return;
    }

    bool hasError = false;
    GList* relNames = nullptr;
    auto srcParent = srcPaths.front().parent();
    for(int i = srcPaths.size() - 1; i >=0; --i) {
        const auto& path = srcPaths[i];
        // ensure that all source files are children of the same base dir
        if(!srcParent.isPrefixOf(path)) {
            hasError = true;
            break;
        }
        // get relative paths of the src files
        relNames = g_list_prepend(relNames,
                                  g_strdup(srcParent.relativePathStr(path).get()));
    }

    if(hasError) {
        // TODO: report errors properly
    }
    else {
        addFiles(relNames, srcParent.uri().get(), destDirPath, onlyIfNewer, password, encrypt_header, compression, volume_size);
    }

    freeStrsGList(relNames);
}

void Archiver::addDirectory(const char* directoryUri, const char* baseDirUri, const char* destDirPath, bool onlyIfNewer, const char* password, bool encrypt_header, FrCompression compression, unsigned int volume_size) {
    fr_archive_add_directory(impl_, directoryUri, baseDirUri, destDirPath, onlyIfNewer, password, encrypt_header, compression, volume_size);
}

void Archiver::addDirectory(const Fm::FilePath &directory, const char* destDirPath, bool onlyIfNewer, const char *password, bool encrypt_header, FrCompression compression, unsigned int volume_size) {
    auto parent = directory.parent();
    addDirectory(directory.uri().get(), parent.uri().get(), destDirPath,
                 onlyIfNewer, password, encrypt_header, compression, volume_size);
}

/*
void Archive::addWithWildcard(const char* include_files, const char* exclude_files, const char* exclude_folders, const char* base_dir, const char* dest_dir, bool update, bool follow_links, const char* password, bool encrypt_header, FrCompression compression, unsigned int volume_size) {
    return fr_archive_add_with_wildcard(impl_, include_files, exclude_files, exclude_folders, base_dir, dest_dir, update, follow_links, password, encrypt_header, compression, volume_size);
}
*/

void Archiver::removeFiles(GList* fileNames, FrCompression compression) {
    fr_process_clear (impl_->process);
    fr_archive_remove(impl_, fileNames, compression);
    fr_process_start (impl_->process);
}

void Archiver::removeFiles(const std::vector<const FileData*>& files, FrCompression compression) {
    GList* glist = nullptr;
    for(int i = files.size() - 1; i >= 0; --i) {
        glist = g_list_prepend(glist, g_strdup(files[i]->original_path));
    }
    removeFiles(glist, compression);
    freeStrsGList(glist);
}

void Archiver::extractFiles(GList* fileNames, const char* destDirUri, const char* baseDirPath, bool skip_older, bool overwrite, bool junk_path, const char* password) {
    fr_process_clear (impl_->process);
    fr_archive_extract(impl_, fileNames, destDirUri, baseDirPath, skip_older, overwrite, junk_path, password);
    fr_process_start(impl_->process);
}

void Archiver::extractFiles(const std::vector<const FileData*>& files, const Fm::FilePath& destDir, bool skip_older, bool overwrite, bool junk_path, const char* password) {
    GList* glist = nullptr;
    for(int i = files.size() - 1; i >= 0; --i) {
        glist = g_list_prepend(glist, g_strdup(files[i]->original_path));
    }
    extractFiles(glist, destDir.uri().get(), "/", skip_older, overwrite, junk_path, password);
    freeStrsGList(glist);
}

void Archiver::extractAll(const char* destDirUri, bool skip_older, bool overwrite, bool junk_path, const char* password) {
    extractFiles(nullptr, destDirUri, nullptr, skip_older, overwrite, junk_path, password);
}

bool Archiver::extractHere(bool skip_older, bool overwrite, bool junk_path, const char* password) {
    fr_process_clear (impl_->process);
    if(fr_archive_extract_here(impl_, skip_older, overwrite, junk_path, password)) {
        fr_process_start(impl_->process);
        return true;
    }
    return false;
}

const char* Archiver::lastExtractionDestination() const {
    return fr_archive_get_last_extraction_destination(impl_);
}

void Archiver::testArchiveIntegrity(const char* password) {
    fr_archive_test(impl_, password);
}

void Archiver::freeStrsGList(GList *strs) {
    g_list_foreach(strs, (GFunc)g_free, nullptr);
}

void Archiver::stopCurrentAction() {
    fr_archive_stop(impl_);
}

QUrl Archiver::archiveUrl() const {
    QUrl ret;
    if(impl_->file) {
        char* uriStr = g_file_get_uri(impl_->file);
        ret = QUrl::fromEncoded(uriStr);
        g_free(uriStr);
    }
    return ret;
}

QString Archiver::archiveDisplayName() const {
    QString ret;
    if(impl_->file) {
        char* name = g_file_get_parse_name(impl_->file);
        ret = QString::fromUtf8(name);
        g_free(name);
    }
    return ret;
}

bool Archiver::isActionInProgress() const {
    return currentAction() != FR_ACTION_NONE;
}

FrAction Archiver::currentAction() const {
    if(impl_->command) {
        return impl_->command->action;
    }
    return FR_ACTION_NONE;
}

ArchiverError Archiver::lastError() const {
    return ArchiverError{&impl_->error};
}

unsigned int Archiver::fileCount() const {
    if(impl_->command && impl_->command->files) {
        return impl_->command->files->len;
    }
    return 0;
}

const FileData* Archiver::file(unsigned int index) const {
    if(index < fileCount()) {
        return reinterpret_cast<FileData*>(g_ptr_array_index(impl_->command->files, index));
    }
    return nullptr;
}


// GObject signal callbacks

// NOTE: emitting Qt signals within glib signal callbacks does not work in some cases.
// We use the workaround provided here: https://bugreports.qt.io/browse/QTBUG-18434

void Archiver::onStart(FrArchive*, FrAction action, Archiver* _this) {
    qDebug("start");
    QMetaObject::invokeMethod(_this, "start", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(FrAction, action));
}

void Archiver::onDone(FrArchive*, FrAction action, FrProcError* error, Archiver* _this) {
    qDebug("done: %s", error && error->gerror ? error->gerror->message : "");
    // FIXME: error might become dangling pointer for queued connections. :-(
    QMetaObject::invokeMethod(_this, "finish", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(FrAction, action), Q_ARG(ArchiverError, error));
}

void Archiver::onProgress(FrArchive*, double fraction, Archiver* _this) {
    QMetaObject::invokeMethod(_this, "progress", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(double, fraction));
    qDebug("progress: %lf", fraction);
}

void Archiver::onMessage(FrArchive*, const char* msg, Archiver* _this) {
    QMetaObject::invokeMethod(_this, "message", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(QString, QString::fromUtf8(msg)));
}

void Archiver::onStoppable(FrArchive*, gboolean value, Archiver* _this) {
    QMetaObject::invokeMethod(_this, "stoppableChanged", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(bool, bool(value)));
}

void Archiver::onWorkingArchive(FrCommand* comm, const char* filename, Archiver* _this) {
    // FIXME: why the first param is comm?
    // Q_EMIT _this->workingArchive(comm, filename);
}

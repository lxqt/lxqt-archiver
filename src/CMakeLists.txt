if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(
    # copy-n-paste
    ${GLIB_INCLUDE_DIRS}
    -DSHDIR=""
)

add_definitions(
    # FIXME: how to set these dirs
    -DSHDIR=\"?\"
    -DPRIVEXECDIR=\"?\"
)

set(lxqt-archiver_HDRS
)

set(CORE_SRCS
    # actions.c
    # dlg-add-files.c
    # dlg-add-folder.c
    # dlg-ask-password.c
    # dlg-batch-add.c
    # dlg-delete.c
    # dlg-extract.c
    # dlg-new.c
    # dlg-open-with.c
    # dlg-package-installer.c
    # dlg-password.c
    # dlg-prop.c
    # dlg-update.c
    # eggfileformatchooser.c
    # eggtreemultidnd.c

    file-data.c
    file-utils.c
    fr-archive.c
    fr-command-7z.c
    fr-command-ace.c
    fr-command-alz.c
    fr-command-ar.c
    fr-command-arj.c
    fr-command.c
    fr-command-cfile.c
    fr-command-cpio.c
    fr-command-dpkg.c
    fr-command-iso.c
    fr-command-jar.c
    fr-command-lha.c
    fr-command-lrzip.c
    fr-command-rar.c
    fr-command-rpm.c
    fr-command-tar.c
    fr-command-unarchiver.c
    fr-command-unstuff.c
    fr-command-zip.c
    fr-command-zoo.c
    fr-enum-types.c
    fr-error.c
    fr-init.c
    fr-marshal.c
    fr-proc-error.c
    fr-process.c
    fr-resources.c
    # fr-stock.c
    gio-utils.c
    glib-utils.c
    # gtk-utils.c
    java-utils.c
    open-file.c
    # preferences.c
    rar-utils.c
    # server.c
)

set(lxqt-archiver_SRCS
    main.cpp
    ${CORE_SRCS}
    fr-window.cpp
)

set(lxqt-archiver_UI
)

# Translations
lxqt_translate_ts(lxqt-archiver_QM_FILES
    UPDATE_TRANSLATIONS
        ${UPDATE_TRANSLATIONS}
    SOURCES
        ${lxqt-archiver_HDRS}
        ${lxqt-archiver_SRCS}
        ${lxqt-archiver_UI}
    INSTALL_DIR
        "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations"
)

lxqt_translate_desktop(DESKTOP_FILES
    SOURCES
    lxqt-archiver.desktop.in
)

add_executable(lxqt-archiver
    ${lxqt-archiver_SRCS}
    ${lxqt-archiver_QM_FILES}
    ${lxqt-archiver_QM_LOADER}
    ${DESKTOP_FILES}
)
set_property(
     TARGET lxqt-archiver APPEND
     PROPERTY COMPILE_DEFINITIONS
     PAVUCONTROL_QT_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}"
)

target_link_libraries(lxqt-archiver
    Qt5::Widgets
    Qt5::DBus
    ${GLIB_LDFLAGS}
)

install(TARGETS
    lxqt-archiver
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

install(FILES
    ${DESKTOP_FILES}
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
    COMPONENT Runtime
)

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

    # copy-n-paste/eggdesktopfile.c
    # copy-n-paste/eggdesktopfile.h
    # copy-n-paste/eggsmclient.c
    # copy-n-paste/eggsmclient.h
    # copy-n-paste/eggsmclient-private.h
    # copy-n-paste/eggsmclient-xsmp.c
)

set(lxqt-archiver_SRCS
    main.cpp
    # fr-window.cpp
    ${CORE_SRCS}
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

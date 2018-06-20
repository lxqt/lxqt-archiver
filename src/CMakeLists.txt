add_subdirectory(core)

include_directories(
    core
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_definitions(
    -DQT_NO_KEYWORDS
)

set(lxqt-archiver_HDRS
)

set(lxqt-archiver_SRCS
    main.cpp
    mainwindow.cpp
    archiver.cpp
    archivererror.cpp
)

set(lxqt-archiver_UI
    mainwindow.ui
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
    ${lxqt-archiver_UI}
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
    fm-qt
    lxqt-archiver-core
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

cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)
project(lxqt-archiver)

option(UPDATE_TRANSLATIONS "Update source translation translations/*.ts files" OFF)

set(LXQTBT_MINIMUM_VERSION "0.5.0")
set(LIBFM_QT_MINIMUM_VERSION "0.14.0")


list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

find_package(Qt5Widgets REQUIRED)
find_package(Qt5LinguistTools REQUIRED)
find_package(Qt5DBus REQUIRED)

find_package(Qt5X11Extras REQUIRED)  # seems to be required by fm-qt?
find_package(fm-qt ${LIBFM_QT_MINIMUM_VERSION} REQUIRED)

find_package(lxqt-build-tools ${LXQTBT_MINIMUM_VERSION} REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
    GLIB REQUIRED
    glib-2.0
    gio-2.0
    gio-unix-2.0
    json-glib-1.0  # what's this?
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LXQT_ARCHIVER_MAJOR_VERSION 0)
set(LXQT_ARCHIVER_MINOR_VERSION 1)
set(LXQT_ARCHIVER_PATCH_VERSION 0)
set(LXQT_ARCHIVER_VERSION ${LXQT_ARCHIVER_MAJOR_VERSION}.${LXQT_ARCHIVER_MINOR_VERSION}.${LXQT_ARCHIVER_PATCH_VERSION})
add_definitions("-DLXQT_ARCHIVER_VERSION=\"${LXQT_ARCHIVER_VERSION}\"")

include(LXQtPreventInSourceBuilds)
include(LXQtCompilerSettings NO_POLICY_SCOPE)
include(LXQtTranslate)

add_subdirectory(src)

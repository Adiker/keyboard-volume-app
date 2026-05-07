cmake_minimum_required(VERSION 3.20)
project(keyboard-volume-app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui DBus)

# System libraries via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV  REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBPULSE  REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(LIBPIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(XCB       REQUIRED IMPORTED_TARGET xcb)

# Sources
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/i18n.cpp
    src/volumecontroller.cpp
    src/inputhandler.cpp
    src/osdwindow.cpp
    src/trayapp.cpp
    src/deviceselector.cpp
    src/settingsdialog.cpp
    src/firstrunwizard.cpp
    src/dbusinterface.cpp
    src/mprisinterface.cpp
    src/evdevdevice.cpp
    src/pwutils.cpp
    src/applistwidget.cpp
    src/appselectordialog.cpp
    src/profileeditdialog.cpp
    src/windowtracker.cpp
    resources.qrc
)

add_executable(keyboard-volume-app ${SOURCES})

target_compile_definitions(keyboard-volume-app PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(keyboard-volume-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(keyboard-volume-app PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::DBus
    PkgConfig::LIBEVDEV
    PkgConfig::LIBPULSE
    PkgConfig::LIBPIPEWIRE
    PkgConfig::XCB
)

# Optional: wayland-client for pre-QApplication zwlr_layer_shell_v1 probe
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
if(WAYLAND_CLIENT_FOUND)
    target_compile_definitions(keyboard-volume-app PRIVATE HAVE_WAYLAND_CLIENT)
    target_link_libraries(keyboard-volume-app PRIVATE PkgConfig::WAYLAND_CLIENT)
    message(STATUS "wayland-client: Wayland protocol probe enabled")
else()
    message(STATUS "wayland-client: NOT found — XCB fallback always used on Wayland sessions")
endif()

# Optional: LayerShellQt for native Wayland OSD positioning (wlr-layer-shell).
# Requires ≥6.6: setScreen() and setActivateOnShow() were added in that release
# (setScreen() replaced the deprecated ScreenConfiguration API).
find_package(LayerShellQt 6.6 QUIET)
if(LayerShellQt_FOUND)
    target_compile_definitions(keyboard-volume-app PRIVATE HAVE_LAYER_SHELL_QT)
    target_link_libraries(keyboard-volume-app PRIVATE LayerShellQt::Interface)
    message(STATUS "LayerShellQt ${LayerShellQt_VERSION}: native Wayland OSD enabled")
else()
    message(STATUS "LayerShellQt ≥6.6: NOT found — OSD will use XCB fallback on Wayland")
endif()

add_executable(kv-ctl
    src/kvctl.cpp
    src/kvctlcommand.cpp
)

target_compile_definitions(kv-ctl PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(kv-ctl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kv-ctl PRIVATE
    Qt6::Core
    Qt6::DBus
)

install(TARGETS keyboard-volume-app kv-ctl DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../resources/keyboard-volume-app.desktop"
        DESTINATION share/applications)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icon.png"
        DESTINATION share/pixmaps
        RENAME keyboard-volume-app.png)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../deploy/keyboard-volume-app.service"
        DESTINATION lib/systemd/user)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

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
)

install(TARGETS keyboard-volume-app DESTINATION bin)
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

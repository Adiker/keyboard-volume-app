cmake_minimum_required(VERSION 3.20)
project(keyboard-volume-app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

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
)

add_executable(keyboard-volume-app ${SOURCES})

target_include_directories(keyboard-volume-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(keyboard-volume-app PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    PkgConfig::LIBEVDEV
    PkgConfig::LIBPULSE
)

# Copy the icon resource next to the binary at build time
add_custom_command(TARGET keyboard-volume-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icon.png"
        "$<TARGET_FILE_DIR:keyboard-volume-app>/icon.png"
    COMMENT "Copying icon.png"
)

install(TARGETS keyboard-volume-app DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icon.png"
        DESTINATION share/keyboard-volume-app)

cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Test Widgets)

# For smoke tests that link against full source files:
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPULSE  REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(LIBEVDEV  REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBPIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(TAGLIB    REQUIRED IMPORTED_TARGET taglib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ─── test_config ────────────────────────────────────────────────────────────────
add_executable(test_config
    test_config.cpp
    ../src/config.cpp
    ../src/appaudiofilters.cpp
)
target_include_directories(test_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_config PRIVATE
    GTest::gtest_main
    Qt6::Core
)
add_test(NAME test_config COMMAND test_config)

# ─── test_i18n ──────────────────────────────────────────────────────────────────
add_executable(test_i18n
    test_i18n.cpp
    ../src/i18n.cpp
)
target_include_directories(test_i18n PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_i18n PRIVATE
    GTest::gtest_main
    Qt6::Core
)
add_test(NAME test_i18n COMMAND test_i18n)

# ─── test_aboutdialog ───────────────────────────────────────────────────────────
add_executable(test_aboutdialog
    test_aboutdialog.cpp
    ../src/aboutdialog.cpp
    ../src/i18n.cpp
    ../resources.qrc
)
target_compile_definitions(test_aboutdialog PRIVATE
    PROJECT_LICENSE_PATH="${CMAKE_SOURCE_DIR}/../LICENSE"
)
target_include_directories(test_aboutdialog PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_aboutdialog PRIVATE
    GTest::gtest
    Qt6::Core
    Qt6::Widgets
)
add_test(NAME test_aboutdialog COMMAND test_aboutdialog)

# ─── test_kvctlcommand ────────────────────────────────────────────────────────
add_executable(test_kvctlcommand
    test_kvctlcommand.cpp
    ../src/kvctlcommand.cpp
)
target_include_directories(test_kvctlcommand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_kvctlcommand PRIVATE
    GTest::gtest_main
    Qt6::Core
)
add_test(NAME test_kvctlcommand COMMAND test_kvctlcommand)

# ─── test_kvctloutput ─────────────────────────────────────────────────────────
add_executable(test_kvctloutput
    test_kvctloutput.cpp
    ../src/kvctloutput.cpp
)
target_include_directories(test_kvctloutput PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_kvctloutput PRIVATE
    GTest::gtest_main
    Qt6::Core
    Qt6::DBus
)
add_test(NAME test_kvctloutput COMMAND test_kvctloutput)

# ─── test_volumecontroller (smoke) ──────────────────────────────────────────────
add_executable(test_volumecontroller
    test_volumecontroller.cpp
    ../src/volumecontroller.cpp
    ../src/config.cpp
    ../src/appaudiofilters.cpp
    ../src/pwutils.cpp
)
target_include_directories(test_volumecontroller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_volumecontroller PRIVATE
    GTest::gtest_main
    Qt6::Core
    Qt6::Test
    PkgConfig::LIBPULSE
    PkgConfig::LIBPIPEWIRE
)
add_test(NAME test_volumecontroller COMMAND test_volumecontroller)

# ─── test_appmatcher ──────────────────────────────────────────────────────────
add_executable(test_appmatcher
    test_appmatcher.cpp
)
target_include_directories(test_appmatcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_appmatcher PRIVATE
    GTest::gtest_main
    Qt6::Core
)
add_test(NAME test_appmatcher COMMAND test_appmatcher)

# ─── test_pwutils ──────────────────────────────────────────────────────────────
add_executable(test_pwutils
    test_pwutils.cpp
    ../src/appaudiofilters.cpp
    ../src/pwutils.cpp
)
target_include_directories(test_pwutils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_pwutils PRIVATE
    GTest::gtest_main
    Qt6::Core
    PkgConfig::LIBPIPEWIRE
)
add_test(NAME test_pwutils COMMAND test_pwutils)

# Full PipeWire integration is opt-in because it starts private PipeWire,
# pipewire-pulse, WirePlumber and D-Bus daemons and requires their CLI tools.
option(ENABLE_PIPEWIRE_INTEGRATION_TESTS
       "Run isolated PipeWire visible-volume integration regression" OFF)
if(ENABLE_PIPEWIRE_INTEGRATION_TESTS)
    # Small non-installed helper that exposes the exact values read through the
    # production libpipewire parser.
    add_executable(pw_volume_test_helper
        pw_volume_test_helper.cpp
        ../src/appaudiofilters.cpp
        ../src/pwutils.cpp
    )
    target_include_directories(pw_volume_test_helper PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    target_link_libraries(pw_volume_test_helper PRIVATE
        Qt6::Core
        PkgConfig::LIBPIPEWIRE
    )
endif()

# ─── test_inputhandler (API-only, no device needed) ─────────────────────────────
add_executable(test_inputhandler
    test_inputhandler.cpp
    ../src/inputhandler.cpp
    ../src/evdevdevice.cpp
)
target_include_directories(test_inputhandler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_inputhandler PRIVATE
    GTest::gtest_main
    Qt6::Core
    Qt6::Test
    PkgConfig::LIBEVDEV
)
add_test(NAME test_inputhandler COMMAND test_inputhandler)

# ─── test_osdwindow ────────────────────────────────────────────────────────────
add_executable(test_osdwindow
    test_osdwindow.cpp
    ../src/osdwindow.cpp
    ../src/osdlabelformat.cpp
    ../src/config.cpp
    ../src/appaudiofilters.cpp
    ../src/i18n.cpp
)
target_include_directories(test_osdwindow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_osdwindow PRIVATE
    GTest::gtest
    Qt6::Core
    Qt6::Widgets
)
add_test(NAME test_osdwindow COMMAND test_osdwindow)

# ─── test_osdlabelformat ─────────────────────────────────────────────────────
add_executable(test_osdlabelformat
    test_osdlabelformat.cpp
    ../src/osdlabelformat.cpp
)
target_include_directories(test_osdlabelformat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_osdlabelformat PRIVATE
    GTest::gtest_main
    Qt6::Core
)
add_test(NAME test_osdlabelformat COMMAND test_osdlabelformat)

# ─── test_dbusinterface ──────────────────────────────────────────────────────
add_executable(test_dbusinterface
    test_dbusinterface.cpp
    ../src/dbusinterface.cpp
    ../src/config.cpp
    ../src/appaudiofilters.cpp
    ../src/volumecontroller.cpp
    ../src/pwutils.cpp
)
target_include_directories(test_dbusinterface PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_dbusinterface PRIVATE
    GTest::gtest_main
    Qt6::Core
    Qt6::DBus
    Qt6::Test
    PkgConfig::LIBPULSE
    PkgConfig::LIBPIPEWIRE
)
add_test(NAME test_dbusinterface COMMAND test_dbusinterface)

if(ENABLE_PIPEWIRE_INTEGRATION_TESTS)
    add_test(
        NAME integration_pipewire_visible_volume
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration_pipewire_visible_volume.sh
                ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(integration_pipewire_visible_volume PROPERTIES
        LABELS "integration;pipewire"
        TIMEOUT 60
        RUN_SERIAL TRUE
    )
endif()

# ─── test_mprisclient (requires session D-Bus; skipped automatically otherwise) ─
find_package(Qt6 QUIET COMPONENTS DBus)
if(Qt6DBus_FOUND)
    add_executable(test_mprisclient
        test_mprisclient.cpp
        ../src/mprisclient.cpp
        ../src/config.cpp
        ../src/appaudiofilters.cpp
    )
    target_include_directories(test_mprisclient PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    target_link_libraries(test_mprisclient PRIVATE
        GTest::gtest
        Qt6::Core
        Qt6::DBus
        Qt6::Test
        PkgConfig::TAGLIB
    )
    # Tests skip themselves via GTEST_SKIP() when D-Bus is unavailable.
    # In CI, wrap with: dbus-run-session -- ctest -R test_mprisclient
    add_test(NAME test_mprisclient COMMAND test_mprisclient)
    message(STATUS "test_mprisclient: enabled (Qt6::DBus found)")
else()
    message(STATUS "test_mprisclient: SKIPPED (Qt6::DBus not found)")
endif()

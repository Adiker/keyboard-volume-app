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

# ─── test_volumecontroller (smoke) ──────────────────────────────────────────────
add_executable(test_volumecontroller
    test_volumecontroller.cpp
    ../src/volumecontroller.cpp
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

# ─── test_pwutils ──────────────────────────────────────────────────────────────
add_executable(test_pwutils
    test_pwutils.cpp
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
    ../src/config.cpp
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

# ─── test_mprisclient (requires session D-Bus; skipped automatically otherwise) ─
find_package(Qt6 QUIET COMPONENTS DBus)
if(Qt6DBus_FOUND)
    add_executable(test_mprisclient
        test_mprisclient.cpp
        ../src/mprisclient.cpp
        ../src/config.cpp
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

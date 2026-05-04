cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Test)

# For smoke tests that link against full source files:
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPULSE  REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(LIBEVDEV  REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBPIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

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

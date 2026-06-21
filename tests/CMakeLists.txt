# tests/CMakeLists.txt
# Builds one test executable per test file.
# Each executable links only to the sources it needs — not the full kdock
# binary — so tests stay fast and isolated.
#
# Enable with:  cmake -DBUILD_TESTS=ON ...

cmake_minimum_required(VERSION 3.20)
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Test Core Gui DBus)

# Helper macro: one test file → one binary → registered with ctest
macro(kdock_test NAME)
    add_executable(${NAME} ${ARGN})
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${Qt6Gui_PRIVATE_INCLUDE_DIRS}
    )
    target_compile_definitions(${NAME} PRIVATE
        QML_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/share/kdock/qml"
    )
    target_link_libraries(${NAME} PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::DBus)
    add_test(NAME ${NAME} COMMAND ${NAME})
    # Use offscreen platform so tests run in headless CI environments
    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endmacro()

kdock_test(tst_ConfigWatcher
    tst_ConfigWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigWatcher.cpp
)

kdock_test(tst_DockModel
    tst_DockModel.cpp
    ${CMAKE_SOURCE_DIR}/src/DockModel.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/IconThemeDetector.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Quick)
kdock_test(tst_IconProvider
    tst_IconProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/IconProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/IconThemeDetector.cpp
)
target_link_libraries(tst_IconProvider PRIVATE Qt6::Quick)

kdock_test(tst_IconThemeDetector
    tst_IconThemeDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/IconThemeDetector.cpp
)

kdock_test(tst_TaskTracker
    tst_TaskTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTracker.cpp
)

kdock_test(tst_SettingsController
    tst_SettingsController.cpp
    ${CMAKE_SOURCE_DIR}/src/SettingsController.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/IconThemeDetector.cpp
)

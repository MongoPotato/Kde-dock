# tests/CMakeLists.txt
# Builds one test executable per test file.
# Each executable links only to the sources it needs — not the full kdock
# binary — so tests stay fast and isolated.
#
# Enable with:  cmake -DBUILD_TESTS=ON ...

cmake_minimum_required(VERSION 3.20)
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Test Core Gui DBus WaylandClient)

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

# Any test target that compiles TaskTracker.cpp needs the generated
# plasma-window-management Wayland protocol client sources, mirroring the
# generation step in the top-level CMakeLists.txt.
#
# qt6_generate_wayland_protocol_client_sources() registers custom commands
# keyed by output path within CMAKE_CURRENT_BINARY_DIR — calling it more than
# once for the same protocol file in this same directory (i.e. from more than
# one test target here) collides ("Attempt to add a custom rule to output
# ... which already has a custom rule"). So generate it only once, for the
# first target that needs it, and have any later targets just reuse those
# already-generated files.
set(_plasma_wm_test_target "")

macro(kdock_test_link_wayland NAME)
    if(_plasma_wm_test_target STREQUAL "")
        qt6_generate_wayland_protocol_client_sources(${NAME}
            FILES ${CMAKE_SOURCE_DIR}/data/plasma-window-management.xml
        )
        set(_plasma_wm_protocol_c_file "${CMAKE_CURRENT_BINARY_DIR}/wayland-plasma-window-management-protocol.c")
        set_source_files_properties(${_plasma_wm_protocol_c_file} PROPERTIES GENERATED TRUE)
        set(_plasma_wm_test_target ${NAME})
    else()
        target_sources(${NAME} PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/wayland-plasma-window-management-protocol.c"
            "${CMAKE_CURRENT_BINARY_DIR}/qwayland-plasma-window-management.cpp"
        )
        target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        add_dependencies(${NAME} ${_plasma_wm_test_target})
    endif()
    target_link_libraries(${NAME} PRIVATE Qt6::WaylandClient)
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
kdock_test_link_wayland(tst_DockModel)

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
kdock_test_link_wayland(tst_TaskTracker)

kdock_test(tst_SettingsController
    tst_SettingsController.cpp
    ${CMAKE_SOURCE_DIR}/src/SettingsController.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/IconThemeDetector.cpp
)

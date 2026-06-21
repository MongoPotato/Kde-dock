cmake_minimum_required(VERSION 3.20)
project(kdock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Default user install; override with -DCMAKE_INSTALL_PREFIX=...
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick WaylandClient DBus)
# GuiPrivate provides qpa/qplatformnativeinterface.h for Wayland surface access.
# It is REQUIRED at link time but some distros (Ubuntu 22.04) omit the CMake
# config file from their qt6-base-private-dev package.  We request it as an
# optional component and fall back to the raw include-directory variable which
# is always populated when the Gui component is found.
find_package(Qt6 OPTIONAL_COMPONENTS GuiPrivate)

# KF6 WindowSystem is optional — TaskTracker falls back to pure DBus
find_package(ECM QUIET NO_MODULE)
if(ECM_FOUND)
    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    find_package(KF6 QUIET COMPONENTS WindowSystem)
endif()

set(SOURCES
    src/main.cpp
    src/LayerShellWindow.cpp
    src/DockModel.cpp
    src/TaskTracker.cpp
    src/ConfigWatcher.cpp
    src/IconProvider.cpp
    src/IconThemeDetector.cpp
    src/SettingsController.cpp
    src/AppLibrary.cpp
)

add_executable(kdock ${SOURCES})

# Generate Wayland client code from the layer-shell protocol XML. Must be
# called after add_executable so the target exists.
#
# Window management (activate/close) does NOT use a Wayland protocol:
# org_kde_plasma_window_management is not advertised to ordinary clients on
# KWin 6 (confirmed empirically — it's restricted to the shell), and there is
# no public foreign-toplevel protocol either. TaskTracker instead drives KWin's
# own scripting engine (org.kde.kwin.Scripting over DBus), which runs inside
# KWin and isn't subject to that restriction.
qt6_generate_wayland_protocol_client_sources(kdock
    FILES
        ${CMAKE_SOURCE_DIR}/data/wlr-layer-shell-unstable-v1.xml
)

# The generator produces a .c file with wl_interface struct definitions that
# must be compiled separately — it is not automatically added to the target.
set(PROTOCOL_C_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-wlr-layer-shell-unstable-v1-protocol.c"
)
set_source_files_properties(${PROTOCOL_C_FILES} PROPERTIES GENERATED TRUE)
target_sources(kdock PRIVATE ${PROTOCOL_C_FILES})

# QML install path baked in at compile time
set(QML_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/kdock/qml")
target_compile_definitions(kdock PRIVATE
    QML_INSTALL_DIR="${QML_INSTALL_DIR}"
)

target_link_libraries(kdock PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::WaylandClient
    Qt6::DBus
)

# Link Qt6::GuiPrivate if its CMake config was found; always include the
# private headers (the variable is set by the Gui component regardless).
if(Qt6GuiPrivate_FOUND)
    target_link_libraries(kdock PRIVATE Qt6::GuiPrivate)
endif()
target_include_directories(kdock PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})

if(KF6WindowSystem_FOUND)
    target_link_libraries(kdock PRIVATE KF6::WindowSystem)
    target_compile_definitions(kdock PRIVATE HAVE_KF6_WINDOWSYSTEM)
endif()

install(TARGETS kdock DESTINATION bin)
install(DIRECTORY qml/ DESTINATION share/kdock/qml)
install(FILES data/default_dock.json DESTINATION share/kdock)

option(BUILD_TESTS "Build the Qt Test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

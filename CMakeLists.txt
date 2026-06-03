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
)

add_executable(kdock ${SOURCES})

# Generate Wayland client code from the layer-shell protocol XML.
# Must be called after add_executable so the target exists.
qt6_generate_wayland_protocol_client_sources(kdock
    FILES
        ${CMAKE_SOURCE_DIR}/data/wlr-layer-shell-unstable-v1.xml
)

# The generator produces a .c file with wl_interface struct definitions that
# must be compiled separately — it is not automatically added to the target.
set(PROTOCOL_C_FILE "${CMAKE_CURRENT_BINARY_DIR}/wayland-wlr-layer-shell-unstable-v1-protocol.c")
set_source_files_properties(${PROTOCOL_C_FILE} PROPERTIES GENERATED TRUE)
target_sources(kdock PRIVATE ${PROTOCOL_C_FILE})

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

# Private Qt Gui headers needed for qpa/qplatformnativeinterface.h
target_include_directories(kdock PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})

if(KF6WindowSystem_FOUND)
    target_link_libraries(kdock PRIVATE KF6::WindowSystem)
    target_compile_definitions(kdock PRIVATE HAVE_KF6_WINDOWSYSTEM)
endif()

install(TARGETS kdock DESTINATION bin)
install(DIRECTORY qml/ DESTINATION share/kdock/qml)
install(FILES data/default_dock.json DESTINATION share/kdock)

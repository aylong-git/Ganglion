cmake_minimum_required(VERSION 3.21)
project(Ganglion_SSVEP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# BrainFlow defaults to /MT (see its MSVC_RUNTIME=static). Ganglion_FR used
# MultiThreaded as well. The app must use the same CRT as Brainflow.lib.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif()

# --- vcpkg packages (glfw/glad/eigen). ImGui is vendored, do not find_package(imgui).
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

# --- BrainFlow: use a complete matching set (headers + import libs + DLLs).
# Ganglion_FR's working binaries live in AppCore/external_packages/brainflow-prebuilt.
# Do not mix a locally rebuilt BoardController.dll with FR's GanglionLib.dll.
# cmake --build is fine; POST_BUILD copies whatever BRAINFLOW_ROOT/lib contains.
set(BRAINFLOW_ROOT "" CACHE PATH "BrainFlow install/export directory (contains inc/ and lib/)")
if(NOT BRAINFLOW_ROOT)
    message(FATAL_ERROR "Set BRAINFLOW_ROOT to the BrainFlow export folder (inc + lib).")
endif()

# Drop stale find_library results after BRAINFLOW_ROOT changes.
foreach(_bf_var IN ITEMS BRAINFLOW_LIB BOARD_CONTROLLER_LIB DATA_HANDLER_LIB)
    if(${_bf_var} AND NOT ${_bf_var} MATCHES "${BRAINFLOW_ROOT}")
        unset(${_bf_var} CACHE)
    endif()
endforeach()

find_library(BRAINFLOW_LIB NAMES Brainflow libBrainflow PATHS "${BRAINFLOW_ROOT}/lib" REQUIRED)
find_library(BOARD_CONTROLLER_LIB NAMES BoardController libBoardController PATHS "${BRAINFLOW_ROOT}/lib" REQUIRED)
find_library(DATA_HANDLER_LIB NAMES DataHandler libDataHandler PATHS "${BRAINFLOW_ROOT}/lib" REQUIRED)

# --- Vendored ImGui / ImPlot (skip *demo.cpp)
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/AppCore/src/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_link_libraries(imgui PUBLIC glfw)

set(IMPLOT_DIR "${CMAKE_SOURCE_DIR}/AppCore/src/implot")
add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR} ${IMGUI_DIR})
target_link_libraries(implot PUBLIC imgui)

# --- App
set(APP_SOURCES
    AppCore/src/main.cpp
    AppCore/src/UI/UI.cpp
    AppCore/src/UI/HudOverlay.cpp
    AppCore/src/Processing/GanglionHandler.cpp
    AppCore/src/Processing/EegRecorder.cpp
    AppCore/src/Processing/InputManager.cpp
    AppCore/src/Processing/KeyIconCatalog.cpp
    AppCore/src/Processing/Utils.cpp
    AppCore/src/Processing/SSVEPClassifier.cpp
    AppCore/src/Processing/HeadObject.cpp
)

if(APPLE)
    list(APPEND APP_SOURCES AppCore/src/UI/HudOverlay.mm)
elseif(WIN32)
    list(APPEND APP_SOURCES AppCore/src/UI/HudOverlay_Win.cpp)
endif()

add_executable(Ganglion_SSVEP ${APP_SOURCES})

target_include_directories(Ganglion_SSVEP PRIVATE
    AppCore/inc
    AppCore/src
    AppCore/src/UI
    AppCore/src/Processing
    ${BRAINFLOW_ROOT}/inc
)

target_link_libraries(Ganglion_SSVEP PRIVATE
    imgui
    implot
    glfw
    glad::glad
    Eigen3::Eigen
    ${BRAINFLOW_LIB}
    ${BOARD_CONTROLLER_LIB}
    ${DATA_HANDLER_LIB}
)

if(APPLE)
    target_compile_definitions(Ganglion_SSVEP PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(Ganglion_SSVEP PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework OpenGL"
    )
    # BrainFlow dlopens GanglionLib from the exe directory
    set_target_properties(Ganglion_SSVEP PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path"
    )
elseif(WIN32)
    target_link_libraries(Ganglion_SSVEP PRIVATE opengl32 setupapi ole32 uuid)
endif()

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/AppCore/assets" GANGLION_ASSET_DIR_NORM)
target_compile_definitions(Ganglion_SSVEP PRIVATE "GANGLION_ASSET_DIR=\"${GANGLION_ASSET_DIR_NORM}\"")

# Copy BrainFlow native libs next to the binary (GanglionLib is loaded at runtime).
add_custom_command(TARGET Ganglion_SSVEP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BRAINFLOW_ROOT}/lib"
            "$<TARGET_FILE_DIR:Ganglion_SSVEP>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/AppCore/assets"
            "$<TARGET_FILE_DIR:Ganglion_SSVEP>/assets"
)
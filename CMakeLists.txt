cmake_minimum_required(VERSION 3.16)
project(OpenPractice VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENPRACTICE_BUILD_TESTS "Build the OpenPractice test suite" ON)
option(OPENPRACTICE_BUILD_GUI "Build the OpenPractice desktop app (Dear ImGui)" ON)

# Warnings for OpenPractice's own code (not applied to third-party sources).
add_library(openpractice_warnings INTERFACE)
if(MSVC)
    target_compile_options(openpractice_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(openpractice_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------ core engine
add_library(openpractice_core
    src/util.cpp
    src/money.cpp
    src/date.cpp
    src/model.cpp
    src/calc.cpp
    src/diff.cpp
    src/sample.cpp
    src/pdf.cpp
    src/report.cpp
    src/cli.cpp
)
target_include_directories(openpractice_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openpractice_core PRIVATE openpractice_warnings)
if(MSVC)
    target_compile_options(openpractice_core PUBLIC /utf-8 /bigobj)
    target_compile_definitions(openpractice_core PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# Self-contained executables on MinGW (no libstdc++/libgcc DLLs to ship).
function(openpractice_static_runtime target)
    if(MINGW)
        target_link_options(${target} PRIVATE -static)
    endif()
endfunction()

# -------------------------------------------------------------------------- CLI
add_executable(openpractice src/main.cpp)
target_link_libraries(openpractice PRIVATE openpractice_core openpractice_warnings)
if(WIN32)
    target_link_libraries(openpractice PRIVATE shell32)
endif()
openpractice_static_runtime(openpractice)
install(TARGETS openpractice RUNTIME DESTINATION bin)

# ------------------------------------------------------------------------ tests
if(OPENPRACTICE_BUILD_TESTS)
    enable_testing()
    add_executable(openpractice_tests tests/test_main.cpp)
    target_link_libraries(openpractice_tests PRIVATE openpractice_core openpractice_warnings)
    openpractice_static_runtime(openpractice_tests)
    add_test(NAME openpractice_tests COMMAND openpractice_tests)
endif()

# -------------------------------------------------------------------------- GUI
if(OPENPRACTICE_BUILD_GUI)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc)
    # Hardening: remove ImGui's built-in "open URL/file in shell" handler (ShellExecute / system())
    # so no widget can ever launch a URL or command, and drop deprecated APIs.
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

    set(OPENPRACTICE_GUI_SOURCES
        gui/app.cpp
        gui/app_commit.cpp
        gui/app_dashboard.cpp
        gui/app_projects.cpp
        gui/app_schedule.cpp
        gui/app_firm.cpp
        gui/app_theme.cpp
        gui/widgets.cpp
        gui/theme.cpp
    )

    if(WIN32)
        target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
        target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi dwmapi)
        add_executable(openpractice-gui WIN32 ${OPENPRACTICE_GUI_SOURCES} gui/main_win32_dx11.cpp gui/platform_win32.cpp)
        target_link_libraries(openpractice-gui PRIVATE comdlg32 shell32 ole32)
    else()
        find_package(glfw3 3.3 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
        target_link_libraries(imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
        add_executable(openpractice-gui ${OPENPRACTICE_GUI_SOURCES} gui/main_glfw_opengl3.cpp gui/platform_posix.cpp)
    endif()
    target_link_libraries(openpractice-gui PRIVATE openpractice_core imgui openpractice_warnings)
    openpractice_static_runtime(openpractice-gui)
    install(TARGETS openpractice-gui RUNTIME DESTINATION bin)
endif()

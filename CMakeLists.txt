cmake_minimum_required(VERSION 3.20)

project(Sheetpad LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(sheetpad_core STATIC
    src/core/csv.cpp
    src/core/document.cpp
    src/core/formula.cpp
    src/core/autocomplete.cpp
    src/core/formula_store.cpp
    src/core/html_table.cpp
    src/core/view.cpp
)
target_compile_features(sheetpad_core PUBLIC cxx_std_20)
# The core only sees src/core, so it cannot include anything from src/app.
target_include_directories(sheetpad_core PUBLIC src/core)
if(WIN32)
    target_compile_definitions(sheetpad_core PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(sheetpad_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(sheetpad_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    enable_language(RC)
    add_executable(Sheetpad WIN32
        src/app/main.cpp
        src/app/app.cpp
        src/app/app_clipboard.cpp
        src/app/app_commands.cpp
        src/app/app_file.cpp
        src/app/cell_editor.cpp
        src/app/hint_window.cpp
        src/app/find_bar.cpp
        src/app/frame_window.cpp
        src/app/grid_drawing.cpp
        src/app/grid_filter.cpp
        src/app/grid_layout.cpp
        src/app/grid_window.cpp
        src/app/inspector_pane.cpp
        src/app/text_encoding.cpp
        src/app/window_handle.cpp
        src/app/app.rc
    )
    target_include_directories(Sheetpad PRIVATE src/app)
    set_source_files_properties(src/app/app.rc PROPERTIES OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resource.h;${CMAKE_CURRENT_SOURCE_DIR}/src/app/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/src/app/app.manifest")
    target_link_libraries(Sheetpad PRIVATE sheetpad_core comdlg32 comctl32 user32 gdi32 shell32)
    target_compile_definitions(Sheetpad PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

    if(MSVC)
        target_compile_options(Sheetpad PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(Sheetpad PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(MINGW)
        # Size: drop unused sections, strip symbols, and optimise across translation units.
        target_compile_options(sheetpad_core PRIVATE -ffunction-sections -fdata-sections)
        target_compile_options(Sheetpad PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(Sheetpad PRIVATE -municode -static -static-libgcc -static-libstdc++ -Wl,--stack,33554432
                            -Wl,--gc-sections -s)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT sheetpad_ipo OUTPUT sheetpad_ipo_output LANGUAGES CXX)
        if(sheetpad_ipo)
            set_property(TARGET sheetpad_core Sheetpad PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
endif()

# Platform-independent core tests run natively (e.g. on Linux/WSL).
if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE sheetpad_core)
    add_test(NAME core_tests COMMAND core_tests)
    add_executable(bench tests/bench.cpp)
    target_link_libraries(bench PRIVATE sheetpad_core)
endif()

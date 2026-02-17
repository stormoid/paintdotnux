cmake_minimum_required(VERSION 3.20)
project(paintnux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport)
qt_standard_project_setup()

# Core library
add_library(paintnux_core STATIC
    src/core/colorbgra.h
    src/core/surface.h
    src/core/surface.cpp
    src/core/blendops.h
    src/core/blendops.cpp
)
target_include_directories(paintnux_core PUBLIC src)
target_link_libraries(paintnux_core PUBLIC Qt6::Core Qt6::Gui)

# Data library
add_library(paintnux_data STATIC
    src/data/layer.h
    src/data/layer.cpp
    src/data/bitmaplayer.h
    src/data/bitmaplayer.cpp
    src/data/document.h
    src/data/document.cpp
    src/data/selection.h
    src/data/selection.cpp
    src/data/fileio.h
    src/data/fileio.cpp
)
target_include_directories(paintnux_data PUBLIC src)
target_link_libraries(paintnux_data PUBLIC paintnux_core Qt6::Concurrent)

# History library
add_library(paintnux_history STATIC
    src/history/historystack.h
    src/history/historystack.cpp
    src/history/bitmaphistorymemento.h
    src/history/bitmaphistorymemento.cpp
    src/history/selectionhistorymemento.h
    src/history/selectionhistorymemento.cpp
    src/history/layerhistorymemento.h
    src/history/layerhistorymemento.cpp
    src/history/replacedocumentmemento.h
    src/history/replacedocumentmemento.cpp
    src/history/flipmemento.h
    src/history/flipmemento.cpp
)
target_include_directories(paintnux_history PUBLIC src)
target_link_libraries(paintnux_history PUBLIC paintnux_data)

# Adjustments library
add_library(paintnux_adjustments STATIC
    src/adjustments/adjustments.h
    src/adjustments/adjustments.cpp
)
target_include_directories(paintnux_adjustments PUBLIC src)
target_link_libraries(paintnux_adjustments PUBLIC paintnux_core)

# Effects library
add_library(paintnux_effects STATIC
    src/effects/effects.h
    src/effects/effects.cpp
)
target_include_directories(paintnux_effects PUBLIC src)
target_link_libraries(paintnux_effects PUBLIC paintnux_core paintnux_adjustments)

# Tools library
add_library(paintnux_tools STATIC
    src/tools/tool.h
    src/tools/tool.cpp
    src/tools/brushtool.h
    src/tools/brushtool.cpp
    src/tools/colorpickertool.h
    src/tools/colorpickertool.cpp
    src/tools/paintbuckettool.h
    src/tools/paintbuckettool.cpp
    src/tools/navtools.h
    src/tools/navtools.cpp
    src/tools/selecttools.h
    src/tools/selecttools.cpp
    src/tools/magicwandtool.h
    src/tools/magicwandtool.cpp
    src/tools/movetools.h
    src/tools/movetools.cpp
    src/tools/shapetools.h
    src/tools/shapetools.cpp
    src/tools/gradienttool.h
    src/tools/gradienttool.cpp
    src/tools/clonestamptool.h
    src/tools/clonestamptool.cpp
    src/tools/recolortool.h
    src/tools/recolortool.cpp
    src/tools/texttool.h
    src/tools/texttool.cpp
)
target_include_directories(paintnux_tools PUBLIC src)
target_link_libraries(paintnux_tools PUBLIC paintnux_history Qt6::Widgets)

# UI library
add_library(paintnux_ui STATIC
    src/ui/canvaswidget.h
    src/ui/canvaswidget.cpp
    src/ui/documentworkspace.h
    src/ui/documentworkspace.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/toolsdock.h
    src/ui/toolsdock.cpp
    src/ui/colorsdock.h
    src/ui/colorsdock.cpp
    src/ui/tooloptionsbar.h
    src/ui/tooloptionsbar.cpp
    src/ui/historydock.h
    src/ui/historydock.cpp
    src/ui/layersdock.h
    src/ui/layersdock.cpp
    src/ui/rulerwidget.h
    src/ui/rulerwidget.cpp
)
target_include_directories(paintnux_ui PUBLIC src)
target_link_libraries(paintnux_ui PUBLIC paintnux_tools paintnux_adjustments paintnux_effects Qt6::Widgets Qt6::PrintSupport)

# App executable
add_executable(paintnux
    src/app/main.cpp
)
target_link_libraries(paintnux PRIVATE paintnux_ui)

# Tests (optional, requires GoogleTest)
option(BUILD_TESTING "Build test executables" OFF)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(paintnux_tests
        tests/test_colorbgra.cpp
        tests/test_surface.cpp
        tests/test_blendops.cpp
        tests/test_document.cpp
    )
    target_link_libraries(paintnux_tests PRIVATE paintnux_data GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(paintnux_tests)

    # Exhaustive tests (run manually, not part of ctest)
    add_executable(paintnux_tests_exhaustive
        tests/test_exhaustive_effects.cpp
        tests/test_exhaustive_adjustments.cpp
        tests/test_exhaustive_history.cpp
        tests/test_exhaustive_fileio.cpp
        tests/test_exhaustive_selection.cpp
        tests/test_exhaustive_surface.cpp
    )
    target_link_libraries(paintnux_tests_exhaustive PRIVATE
        paintnux_data paintnux_effects paintnux_adjustments paintnux_history
        GTest::gtest_main
    )
    # NOT calling gtest_discover_tests — won't run with ctest
endif()

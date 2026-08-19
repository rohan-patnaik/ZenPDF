cmake_minimum_required(VERSION 3.25)

project(ZenPDFDesktop VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Sql Widgets)

add_library(zenpdf_core STATIC
    src/LocalState.cpp
    src/LocalState.h
    src/Logging.cpp
    src/Logging.h
)
target_link_libraries(zenpdf_core PUBLIC Qt6::Core Qt6::Sql)
target_include_directories(zenpdf_core PUBLIC src)
target_compile_options(zenpdf_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion>
)

add_executable(zenpdf
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)
target_link_libraries(zenpdf PRIVATE zenpdf_core Qt6::Widgets)
target_compile_options(zenpdf PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion>
)

install(TARGETS zenpdf RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.rohan-patnaik.zenpdf.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES packaging/io.github.rohan-patnaik.zenpdf.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_executable(zenpdf_core_tests tests/LocalStateTest.cpp)
    target_link_libraries(zenpdf_core_tests PRIVATE zenpdf_core Qt6::Test)
    add_test(NAME zenpdf_core_tests COMMAND zenpdf_core_tests)
    set_tests_properties(zenpdf_core_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

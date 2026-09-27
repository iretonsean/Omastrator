cmake_minimum_required(VERSION 3.22)
project(omastrator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(OMASTRATOR_WERROR "Treat warnings as errors" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent Test)

# Each source folder is its own library, so a folder builds and tests alone:
# Document and Rendering (the model and how it draws), IO (files), Canvas (the
# artboard view and its tools), UI (the window, panels and menus).
function(oma_library name)
    file(GLOB sources CONFIGURE_DEPENDS ${ARGN})
    if(NOT sources)
        set(sources cmake/empty.cpp)
    endif()
    add_library(${name} STATIC ${sources})
    target_include_directories(${name} PUBLIC src)
    target_compile_options(${name} PUBLIC -Wall -Wextra $<$<BOOL:${OMASTRATOR_WERROR}>:-Werror>)
    # Empty optionals and bad indexes abort, not pass by luck.
    target_compile_definitions(${name} PUBLIC OMASTRATOR_VERSION="${PROJECT_VERSION}" _GLIBCXX_ASSERTIONS)
endfunction()

oma_library(oma_core src/Logging.cpp src/Document/*.cpp src/Rendering/*.cpp)
target_link_libraries(oma_core PUBLIC Qt6::Widgets Qt6::Concurrent)

oma_library(oma_io src/IO/*.cpp)
target_include_directories(oma_io PRIVATE third_party)
target_link_libraries(oma_io PUBLIC oma_core)

oma_library(oma_canvas src/Canvas/*.cpp)
target_link_libraries(oma_canvas PUBLIC oma_core)

oma_library(oma_ui src/ContentView*.cpp src/UI/*.cpp)
target_link_libraries(oma_ui PUBLIC oma_core oma_io oma_canvas)

include(GNUInstallDirs)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/OmastratorApp.cpp)
    add_executable(omastrator src/OmastratorApp.cpp)
    target_link_libraries(omastrator PRIVATE oma_ui)
    install(TARGETS omastrator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES packaging/io.github.iretonsean.Omastrator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.iretonsean.Omastrator.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
foreach(size 16 32 64 128 256 512)
    install(FILES packaging/icons/app-icon-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME io.github.iretonsean.Omastrator.png)
endforeach()
install(FILES LICENSE third_party/nanosvg/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omastrator OPTIONAL)
# A direct install registers the document type; packages use hooks.
install(CODE "
    if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
        find_program(UPDATE_MIME update-mime-database)
        if(UPDATE_MIME)
            execute_process(COMMAND \${UPDATE_MIME} \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/mime\")
        endif()
    endif()")

set(CPACK_PACKAGE_NAME omastrator)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vector illustration for Linux, made for Omarchy")
set(CPACK_PACKAGE_CONTACT "Omastrator")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "shared-mime-info, hicolor-icon-theme")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_REQUIRES "shared-mime-info, hicolor-icon-theme")
include(CPack)

enable_testing()
# tests/<Library>/<Name>Tests.cpp, one executable each, linked to its library.
function(oma_tests library folder)
    file(GLOB tests CONFIGURE_DEPENDS tests/${folder}/*Tests.cpp)
    foreach(test ${tests})
        get_filename_component(name ${test} NAME_WE)
        add_executable(${name} ${test})
        target_link_libraries(${name} PRIVATE ${library} Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS ${folder})
    endforeach()
endfunction()
oma_tests(oma_core Document)
oma_tests(oma_io IO)
oma_tests(oma_canvas Canvas)
oma_tests(oma_ui UI)

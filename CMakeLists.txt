cmake_minimum_required(VERSION 3.26)
project(LiveQRHunter
    VERSION 1.1.0
    LANGUAGES CXX
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UNIT_TESTS "Unit Tests" OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/Zi /sdl /arch:AVX2 /utf-8)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_link_options(/DEBUG /OPT:REF,ICF,LBR)
    endif()
endif()

include(FetchContent)
include(cmake/install_ffmpeg.cmake)
include(cmake/build_QR-Code-generator.cmake)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(cpr CONFIG REQUIRED)

find_package(OpenCV REQUIRED opencv_world)
include_directories(${OpenCV_INCLUDE_DIRS})

find_package(Qt6 COMPONENTS Widgets Concurrent REQUIRED)

include_directories(src/Core)

add_executable(${PROJECT_NAME} WIN32
    src/UI/main.cpp
    src/UI/StreamQRScanner.cpp
    src/UI/StreamQRScanner.h
    src/Core/LiveStreamLink.cpp
    src/Core/LiveStreamLink.h
    src/Core/QRScanner.cpp
    src/Core/QRScanner.h
    src/Core/QRCodeInfo.hpp
    src/Core/ApiDefs.hpp
    src/Core/compile_string.hpp
    src/UI/UtilMat.hpp
)

set(PROJECT_LIBRARIES
    Ws2_32
    wldap32
    Normaliz
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    cpr::cpr
    QR_Code_generator
    nlohmann_json::nlohmann_json
    ${OpenCV_LIBS}
    ${FFMPEG_LIBS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LIBRARIES})

# Copy FFmpeg DLLs
file(GLOB FFMPEG_DLL_FILES
    "${FFMPEG_BIN_DIR}/avcodec-*.dll"
    "${FFMPEG_BIN_DIR}/avformat-*.dll"
    "${FFMPEG_BIN_DIR}/avutil-*.dll"
    "${FFMPEG_BIN_DIR}/swscale-*.dll"
    "${FFMPEG_BIN_DIR}/swresample-*.dll"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${FFMPEG_DLL_FILES}
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# Install
set(Install_Directory "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}_${PROJECT_VERSION}")

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${Install_Directory})
install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${Install_Directory})

# FFmpeg DLLs (windeployqt 不管这些, 仍手工复制)
install(FILES ${FFMPEG_DLL_FILES} DESTINATION ${Install_Directory})

# Qt DLL 与插件用 windeployqt 部署(自动补齐间接依赖/插件/运行库)。
# 让 cmake --install 本身产出自包含目录, 不依赖 CI 额外步骤。
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin"
        DOC "Qt windeployqt tool"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        set(WINDEPLOYQT_TOOL "${WINDEPLOYQT_EXECUTABLE}")
        set(DEPLOY_EXE "${Install_Directory}/${PROJECT_NAME}.exe")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/cmake/deploy_qt.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/deploy_qt.cmake"
            @ONLY
        )
        install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_qt.cmake")
    else()
        message(WARNING "windeployqt not found under ${QT6_INSTALL_PREFIX}/bin, release package will miss Qt runtime")
    endif()
endif()

# Unit tests
if(UNIT_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()

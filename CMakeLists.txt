cmake_minimum_required(VERSION 3.16)

project(SiehDirAlleAn LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${SiehDirAlleAn_SOURCE_DIR}/cmake;${SiehDirAlleAn_SOURCE_DIR}/cmake/find;${CMAKE_MODULE_PATH}")

include(3rdparty/dirman/dirman.cmake)

find_package(SDL2 REQUIRED)

add_executable(SiehDirAlleAn
    src/main.cpp
    ${DIRMANAGER_SRCS}
    res/noise.h res/noise.c
    src/video_player.h src/video_player.cpp
)
target_link_libraries(SiehDirAlleAn PRIVATE
    SDL2::SDL2main SDL2::SDL2
    avcodec avformat avfilter swscale swresample avutil
)

include(GNUInstallDirs)
install(TARGETS SiehDirAlleAn
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

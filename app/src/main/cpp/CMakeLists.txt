cmake_minimum_required(VERSION 3.10.2)

#project("opensl_demo")

set(FFMPEG ${CMAKE_SOURCE_DIR}/ffmpeg) # ffmpeg的路径
include_directories(${FFMPEG}/include) # 导入ffmpeg的头文件

# 批量导入 源文件
file(GLOB src_files *.cpp)

add_library(ffmpeg SHARED IMPORTED)
set_target_properties(
        ffmpeg
        PROPERTIES IMPORTED_LOCATION  # 从本地导入
        ${FFMPEG}/libs/${CMAKE_ANDROID_ARCH_ABI}/libffmpeg.so)

add_library(
        native-lib
        SHARED
        ${src_files})

target_link_libraries(
        native-lib
        ffmpeg
        log
        z
        android
        OpenSLES
)


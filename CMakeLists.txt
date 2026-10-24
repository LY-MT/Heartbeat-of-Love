cmake_minimum_required(VERSION 3.26)
project(love)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set(EASYX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/EasyX")
include_directories(${EASYX_ROOT}/include)
link_directories(${EASYX_ROOT}/lib64)

add_executable(love main.cpp)

target_link_libraries(love PRIVATE
        easyx
        winmm
        ole32
        uuid
        mingwex
        winpthread
)
target_link_options(love PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,-lstdc++,-lpthread,-Bdynamic
)

set_target_properties(love PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ================================================================
# 自动复制 bgm.mp3 与 libwinpthread-1.dll 到输出目录
# ================================================================
if (WIN32)
    # 获取当前 MinGW 编译器的 bin 目录，用于定位 libwinpthread-1.dll
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(LIBWINPTHREAD_DLL "${MINGW_BIN_DIR}/libwinpthread-1.dll")

    # 定位项目根目录下的 bgm.mp3
    set(BGM_MP3 "${CMAKE_SOURCE_DIR}/bgm.mp3")

    set(COPY_COMMANDS "")

    if (EXISTS "${BGM_MP3}")
        list(APPEND COPY_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BGM_MP3}" $<TARGET_FILE_DIR:love>)
    else()
        message(WARNING "未在项目根目录找到 bgm.mp3：${BGM_MP3}")
    endif()

    if (EXISTS "${LIBWINPTHREAD_DLL}")
        list(APPEND COPY_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBWINPTHREAD_DLL}" $<TARGET_FILE_DIR:love>)
    else()
        message(WARNING "未找到 ${LIBWINPTHREAD_DLL}，请检查 MinGW 路径")
    endif()

    if (COPY_COMMANDS)
        add_custom_command(TARGET love POST_BUILD
                ${COPY_COMMANDS}
                COMMENT "自动复制 bgm.mp3 和 libwinpthread-1.dll 到输出目录"
        )
    endif()
endif()
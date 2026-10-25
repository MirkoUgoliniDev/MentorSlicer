# FFmpeg 查找模块
find_package(PkgConfig QUIET)

# 设置 FFmpeg 组件列表
set(FFMPEG_COMPONENTS
    avcodec
    avformat
    avutil
    avdevice
    avfilter
    swscale
    swresample
)

# 查找 FFmpeg 包含路径
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        "C:/msys64/mingw64/include"
        "C:/msys64/usr/include"
        "$CMAKE_PREFIX_PATH/include"
    PATH_SUFFIXES ffmpeg
    DOC "FFmpeg 头文件目录"
)

# 查找各组件库文件
foreach(COMPONENT ${FFMPEG_COMPONENTS})
    string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
    
    # 优先尝试 pkg-config
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_${UPPERCOMPONENT} QUIET lib${COMPONENT})
    endif()

    find_library(${UPPERCOMPONENT}_LIBRARY
        NAMES ${COMPONENT} lib${COMPONENT}
        HINTS ${PC_${UPPERCOMPONENT}_LIBRARY_DIRS}
        PATHS
            /usr/lib
            /usr/local/lib
            /opt/local/lib
            "C:/msys64/mingw64/lib"
            "C:/msys64/usr/lib"
            "$CMAKE_PREFIX_PATH/lib"
        PATH_SUFFIXES ffmpeg
        DOC "FFmpeg ${COMPONENT} 库路径"
    )

    if(${UPPERCOMPONENT}_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${${UPPERCOMPONENT}_LIBRARY})
    endif()
endforeach()

# 版本检测
if(FFMPEG_INCLUDE_DIR AND EXISTS "${FFMPEG_INCLUDE_DIR}/libavutil/avutil.h")
    file(STRINGS "${FFMPEG_INCLUDE_DIR}/libavutil/avutil.h" FFMPEG_VERSION_MAJOR
         REGEX "#define LIBAVUTIL_VERSION_MAJOR [0-9]+")
    string(REGEX REPLACE "#define LIBAVUTIL_VERSION_MAJOR ([0-9]+)" "\\1" FFMPEG_VERSION_MAJOR "${FFMPEG_VERSION_MAJOR}")

    file(STRINGS "${FFMPEG_INCLUDE_DIR}/libavutil/avutil.h" FFMPEG_VERSION_MINOR
         REGEX "#define LIBAVUTIL_VERSION_MINOR [0-9]+")
    string(REGEX REPLACE "#define LIBAVUTIL_VERSION_MINOR ([0-9]+)" "\\1" FFMPEG_VERSION_MINOR "${FFMPEG_VERSION_MINOR}")

    set(FFMPEG_VERSION "${FFMPEG_VERSION_MAJOR}.${FFMPEG_VERSION_MINOR}")
endif()

# 处理查找结果
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS
        FFMPEG_INCLUDE_DIR
        FFMPEG_LIBRARIES
    VERSION_VAR FFMPEG_VERSION
    HANDLE_COMPONENTS
)

# 定义导入目标
if(FFMPEG_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )

    # 为每个组件创建单独的目标
    foreach(COMPONENT ${FFMPEG_COMPONENTS})
        string(TOUPPER ${COMPONENT} UPPERCOMPONENT)
        if(TARGET FFmpeg::${COMPONENT} OR NOT ${UPPERCOMPONENT}_LIBRARY)
            continue()
        endif()

        add_library(FFmpeg::${COMPONENT} UNKNOWN IMPORTED)
        message(STATUS "FFmpeg::${COMPONENT} library found: ${${UPPERCOMPONENT}_LIBRARY}")
        set_target_properties(FFmpeg::${COMPONENT} PROPERTIES
            IMPORTED_LOCATION "${${UPPERCOMPONENT}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        )
    endforeach()
endif()

# 缓存变量
mark_as_advanced(
    FFMPEG_INCLUDE_DIR
    FFMPEG_LIBRARIES
    FFMPEG_VERSION
)

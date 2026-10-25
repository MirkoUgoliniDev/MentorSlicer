# 综合检查方法
if(UNIX AND NOT APPLE)
    set(IS_UOS FALSE)
    
    # 检查 /etc/os-release
    if(EXISTS "/etc/os-release")
        file(READ "/etc/os-release" OS_RELEASE)
        if(OS_RELEASE MATCHES "NAME=.*UOS" OR OS_RELEASE MATCHES "NAME=.*uos")
            set(IS_UOS TRUE)
        endif()
    endif()
    
    # 检查 /etc/uos-release
    if(EXISTS "/etc/uos-release")
        set(IS_UOS TRUE)
    endif()
    
    # 检查其他可能的标识
    execute_process(
        COMMAND lsb_release -i -s
        OUTPUT_VARIABLE LSB_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(LSB_ID STREQUAL "UOS" OR LSB_ID STREQUAL "uos")
        set(IS_UOS TRUE)
    endif()
else()
    set(IS_UOS FALSE)
endif()

if(IS_UOS)
    message(STATUS "检测到系统是统信 UOS")
    # 添加 UOS 特定的配置选项
else()
    message(STATUS "当前系统不是统信 UOS")
endif()
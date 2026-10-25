# 添加 zstd 依赖项目
orcaslicer_add_cmake_project(
    zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=EB33E51F49A15E023950CD7825CA74A4A2B43DB8354825AC24FC1B7EE09E6FA3 # 替换为实际 SHA256
    SOURCE_SUBDIR "build/cmake"
    CMAKE_ARGS
        -DZSTD_BUILD_STATIC=ON      # 强制生成静态库
        -DZSTD_BUILD_SHARED=OFF     # 禁用动态库[3,4](@ref)
        -DZSTD_BUILD_TESTS=OFF      # 关闭测试（加速编译）
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON  # 启用 PIC（兼容链接到动态库）[3](@ref)
)
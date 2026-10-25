#ifndef CR_TPMS_LIBRARY_H
#define CR_TPMS_LIBRARY_H

#ifdef _WIN32
    #ifdef CR_TPMS_LIBRARY_EXPORTS
        #define CR_TPMS_API __declspec(dllexport)
    #else
        #define CR_TPMS_API __declspec(dllimport)
    #endif
#else
    #define CR_TPMS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 主标量场函数
CR_TPMS_API float scalar_function_test(float x, float y, float z,
                                     float bbox_min_x, float bbox_min_y, float bbox_min_z,
                                     float bbox_max_x, float bbox_max_y, float bbox_max_z,
                                     int axis, int celltype,
                                     float infill_density1, float infill_density2);

#ifdef __cplusplus
}
#endif

#endif // CR_TPMS_LIBRARY_H
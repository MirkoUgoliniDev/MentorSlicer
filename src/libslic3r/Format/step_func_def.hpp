#ifndef slic3r_Format_STEP_FUNC_DEF_hpp_
#define slic3r_Format_STEP_FUNC_DEF_hpp_

namespace Slic3r {
// Forward declarations here to avoid (#include <Windows.h> in the Model.hpp file), which cause the compile problem.
typedef std::function<void(int load_stage, int current, int total, bool& cancel)> ImportStepProgressFn;
typedef std::function<void(bool isUtf8)> StepIsUtf8Fn;

}; // namespace Slic3r

#endif /* slic3r_Format_STEP_FUNC_DEF_hpp_ */

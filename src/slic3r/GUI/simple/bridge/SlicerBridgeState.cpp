#include "SlicerBridge.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelObject.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "libslic3r/ModelInstance.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace GUI {
namespace Bridge {

// ===========================================================================
// Overhang analysis helper
// ===========================================================================

OverhangResult SlicerBridge::AnalyzeOverhang(const ModelObject& obj, double threshold_deg)
{
    OverhangResult result;
    if (obj.instances.empty() || obj.volumes.empty())
        return result;

    const Transform3d& trafo = obj.instances[0]->get_matrix();
    const Eigen::Matrix3d normal_matrix = trafo.linear();
    const double cos_thresh = -std::cos(threshold_deg * M_PI / 180.0);
    constexpr double z_epsilon = 0.05;
    constexpr double clearance_threshold = 1.0;

    double total_area          = 0.0;
    double downward_area       = 0.0;
    double unsupported_area    = 0.0;
    double bottom_contact_area = 0.0;
    double max_angle_deg       = 0.0;
    double object_min_z        = std::numeric_limits<double>::infinity();

    for (const ModelVolume* vol : obj.volumes) {
        if (!vol->is_model_part())
            continue;

        const indexed_triangle_set& its = vol->mesh().its;
        for (const auto& vertex : its.vertices) {
            const Vec3d world = trafo * vertex.cast<double>();
            object_min_z = std::min(object_min_z, world.z());
        }
    }

    if (!std::isfinite(object_min_z))
        return result;

    for (const ModelVolume* vol : obj.volumes) {
        if (!vol->is_model_part())
            continue;

        const indexed_triangle_set& its = vol->mesh().its;
        for (size_t fi = 0; fi < its.indices.size(); ++fi) {
            const auto& face = its.indices[fi];
            const Vec3d local_v0 = its.vertices[face[0]].cast<double>();
            const Vec3d local_v1 = its.vertices[face[1]].cast<double>();
            const Vec3d local_v2 = its.vertices[face[2]].cast<double>();

            const Vec3d world_v0 = trafo * local_v0;
            const Vec3d world_v1 = trafo * local_v1;
            const Vec3d world_v2 = trafo * local_v2;

            const Vec3d cross = (world_v1 - world_v0).cross(world_v2 - world_v0);
            const double area = 0.5 * cross.norm();
            if (area < 1e-12)
                continue;

            total_area += area;

            const Vec3d normal = (normal_matrix * (local_v1 - local_v0).cross(local_v2 - local_v0)).normalized();
            if (normal.z() < cos_thresh) {
                downward_area += area;

                const double face_min_z = std::min({world_v0.z(), world_v1.z(), world_v2.z()});
                const double face_max_z = std::max({world_v0.z(), world_v1.z(), world_v2.z()});
                const double clearance = face_min_z - object_min_z;

                if (face_max_z <= object_min_z + z_epsilon) {
                    bottom_contact_area += area;
                    continue;
                }

                if (clearance <= clearance_threshold)
                    continue;

                unsupported_area += area;
                double angle = std::acos(std::clamp(normal.z(), -1.0, 1.0)) * 180.0 / M_PI;
                double overhang_angle = angle - 90.0;
                if (overhang_angle > max_angle_deg)
                    max_angle_deg = overhang_angle;
            }
        }
    }

    result.downward_area_ratio = (total_area > 1e-12) ? (downward_area / total_area) : 0.0;
    result.unsupported_area_ratio = (total_area > 1e-12) ? (unsupported_area / total_area) : 0.0;
    result.bottom_contact_ratio = (total_area > 1e-12) ? (bottom_contact_area / total_area) : 0.0;
    result.bottom_contact_area_mm2 = bottom_contact_area;
    result.max_overhang_angle = max_angle_deg;
    return result;
}

} // namespace Bridge
} // namespace GUI
} // namespace Slic3r


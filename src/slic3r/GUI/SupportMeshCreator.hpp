// Lightweight scaffolding for generating a support mesh ModelInstance
// from an existing ModelInstance, using printer preset options via
// wxGetApp().preset_bundle->printers.get_edited_preset().config.option.

#ifndef SLIC3R_GUI_SUPPORT_MESH_CREATOR_HPP
#define SLIC3R_GUI_SUPPORT_MESH_CREATOR_HPP

#include <string>
#include <vector>
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ModelCommon.hpp"

namespace Slic3r {
class ModelInstance;
class ModelObject;

namespace GUI {

class SupportMeshCreator {
public:
    // Create a support mesh for the provided ModelObject by adding
    // a SUPPORT_ENFORCER volume to it. Does nothing if nothing needed.
    void createSupportMeshForNode(ModelObject* node);

    // Alternative name compatible with earlier discussion.
    void createSupportMesh(ModelObject* node) { createSupportMeshForNode(node); }

    // 1:1 port-style API of Cura's SupportMeshCreator.createSupportMesh.
    // Inputs are world-space vertices and triangle indices of the node.
    // Returns a support TriangleMesh in the same coordinate system
    // or an empty mesh if no support is needed.
    TriangleMesh createSupportMesh(const std::string& node_name,
                                   const std::vector<Vec3f>& node_vertices,
                                   const std::vector<Vec3i32>& node_indices,
                                   const ModelConfigObject&    config);
};

} // namespace GUI
} // namespace Slic3r

#endif // SLIC3R_GUI_SUPPORT_MESH_CREATOR_HPP

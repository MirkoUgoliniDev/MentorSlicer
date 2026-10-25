#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"
#include "AssimpModel.hpp"
#include <string>
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "STEP.hpp"


#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r
{

    struct AssimpCalculate
    {
        // Compute normal of a triangle
        Vec3f compute_triangle_normal(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2);

        // Compute global center of a mesh
        Vec3f compute_global_center(const std::vector<Vec3f>& vertices);

        // Fix face normal orientation so they point outward
        void Fix_Normal_Orientation(std::vector<Vec3f>& vertices, std::vector<Vec3i32>& faces);
    };

    Vec3f AssimpCalculate::compute_triangle_normal(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2)
    {
        Vec3f edge1 = v1 - v0;
        Vec3f edge2 = v2 - v0;
        Vec3f normal = edge1.cross(edge2);
        normal.normalize();
        return normal;
    }

    Vec3f AssimpCalculate::compute_global_center(const std::vector<Vec3f>& vertices)
    {
        Vec3f center = Vec3f::Zero();
        for (const auto& v : vertices)
        {
            center += v;
        }
        center /= vertices.size();
        return center;
    }

    void AssimpCalculate::Fix_Normal_Orientation(std::vector<Vec3f>& vertices, std::vector<Vec3i32>& faces)
    {
        if (vertices.empty() || faces.empty()) return;

        Vec3f global_center = compute_global_center(vertices);

        #pragma omp parallel for
        for (auto& face : faces)
        {
            const Vec3f& v0 = vertices[face(0)];
            const Vec3f& v1 = vertices[face(1)];
            const Vec3f& v2 = vertices[face(2)];
            Vec3f normal = compute_triangle_normal(v0, v1, v2);
            Vec3f face_center = (v0 + v1 + v2) / 3.0f;
            Vec3f dir = face_center - global_center;
            if (normal.dot(dir) < 0.0f)
            {
                std::swap(face(1), face(2)); // Reverse vertex order
            }
        }
    }

    bool load_assimp_model(const char* path, Model* model, bool& is_cancel, ImportStepProgressFn stepFn)
    {
        bool cb_cancel = false;
        if (stepFn)
        {
            stepFn(LOAD_STEP_STAGE_READ_FILE, 0, 1, cb_cancel);
            is_cancel = cb_cancel;
            if (cb_cancel)
            {
                return false;
            }
        }

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |             // Ensure all faces are triangles
            aiProcess_FindInvalidData |         // Find and fix invalid data
            aiProcess_GenSmoothNormals |        // Generate smooth normals
            aiProcess_ValidateDataStructure|    // Validate data structure
            aiProcess_PreTransformVertices      // Pre-transform vertices into target coordinate system
        );

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            return false;
        }

        std::vector<Vec3f> vertices;
        std::vector<Vec3i32> faces;
        int offset = 0;

        // Directly iterate meshes and accumulate geometry
        auto stage_unit = scene->mNumMeshes / LOAD_STEP_STAGE_UNIT_NUM + 1;
        for (unsigned i = 0; i < scene->mNumMeshes; i++)
        {
            if (stepFn)
            {
                if ((i % stage_unit) == 0)
                {
                    stepFn(LOAD_STEP_STAGE_GET_SOLID, i, scene->mNumMeshes, cb_cancel);
                    is_cancel = cb_cancel;
                }
                if (cb_cancel)
                {
                    return false;
                }
            }

            aiMesh* mesh = scene->mMeshes[i];
            if (!mesh) continue;

            // Load vertices
            for (unsigned j = 0; j < mesh->mNumVertices; j++)
            {
                aiVector3D& pos = mesh->mVertices[j];
                vertices.push_back(Vec3f(pos.x, pos.y, pos.z));
            }

            // Load faces
            for (unsigned m = 0; m < mesh->mNumFaces; m++)
            {
                aiFace& face = mesh->mFaces[m];
                if (face.mNumIndices != 3)
                    continue; // Ensure triangle

                faces.push_back(Vec3i32(face.mIndices[0] + offset, face.mIndices[1] + offset, face.mIndices[2] + offset));
            }

            offset += mesh->mNumVertices;
        }

        // Axis handling for some formats (3ds / dae)
        // Different modeling tools use different coordinate systems
        // which leads to different imported model orientations
        const char* dot = strrchr(path, '.');
        if (dot)
        {
            if ((strcmp(dot + 1, "3ds") == 0 || strcmp(dot + 1, "3DS") == 0) ||
               (strcmp(dot + 1, "dae") == 0 || strcmp(dot + 1, "dae") == 0))
            {
                aiMatrix4x4 rotationMatrix;
                rotationMatrix.FromEulerAnglesXYZ(ai_real(90.0f * M_PI / 180.0f), ai_real(0.0f), ai_real(0.0f));
                for (auto& vertex : vertices)
                {
                    aiVector3D pos(vertex.x(), vertex.y(), vertex.z());
                    aiVector3D rotatedPos = rotationMatrix * pos;
                    vertex.x() = rotatedPos.x;
                    vertex.y() = rotatedPos.y;
                    vertex.z() = rotatedPos.z;
                }
            }
        }

        // Build triangle mesh from vertices and faces
        TriangleMesh mesh(vertices, faces);

        // Check whether mesh volume is valid
        if (mesh.volume() <= 0.0)
        {
            // Try to fix normal orientation
            AssimpCalculate AssCalc;
            AssCalc.Fix_Normal_Orientation(vertices, faces);
            mesh = TriangleMesh(vertices, faces);

            // Recompute volume again; if still invalid we could flip triangles
            //if (mesh.volume() <= 0.0)
            //{
            //    mesh.flip_triangles();
            //}
        }

        // Derive model object name from file path
        std::string object_name;
        const char* last_slash = strrchr(path, DIR_SEPARATOR);
        object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
        model->add_object(object_name.c_str(), path, std::move(mesh));

        return true;
    }


}; // namespace Slic3r

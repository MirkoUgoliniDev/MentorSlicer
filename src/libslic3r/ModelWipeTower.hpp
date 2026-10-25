#ifndef slic3r_Model_Wipe_Tower_hpp_
#define slic3r_Model_Wipe_Tower_hpp_
#include "ModelCommon.hpp"

namespace Slic3r {

class ModelWipeTower final : public ObjectBase
{
public:
    // BBS: add partplate logic
    std::vector<Vec2d> positions;
    double             rotation;

private:
    friend class cereal::access;
    friend class UndoRedo::StackImpl;
    friend class Model;

    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    explicit ModelWipeTower();
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit ModelWipeTower(int);
    // Copy constructor copies the ID.
    explicit ModelWipeTower(const ModelWipeTower& cfg) = default;

    // Disabled methods.
    ModelWipeTower(ModelWipeTower&& rhs)                 = delete;
    ModelWipeTower& operator=(const ModelWipeTower& rhs) = delete;
    ModelWipeTower& operator=(ModelWipeTower&& rhs)      = delete;

    // For serialization / deserialization of ModelWipeTower composed into another class into the Undo / Redo stack as a separate object.
    template<typename Archive> void serialize(Archive& ar) { ar(positions, rotation); }
};
}; // namespace Slic3r

// // BS structure stores extruder parameters and speed map of all models
// struct ExtruderParams
// {B
//     std::string materialName;
//     //std::array<double, BedType::btCount> bedTemp;
//     int bedTemp;
//     double heatEndTemp;
// };

// struct GlobalSpeedMap
// {
//     double perimeterSpeed;
//     double externalPerimeterSpeed;
//     double infillSpeed;
//     double solidInfillSpeed;
//     double topSolidInfillSpeed;
//     double supportSpeed;
//     double smallPerimeterSpeed;
//     double maxSpeed;
//     Polygon bed_poly;
// };

// /* Profile data */
// class ModelProfileInfo
// {
// public:
//     std::string ProfileTile;
//     std::string ProfileCover;
//     std::string ProfileDescription;
//     std::string ProfileUserId;
//     std::string ProfileUserName;
// };

// /* info in ModelDesignInfo can not changed after initialization */
// class ModelDesignInfo
// {
// public:
//     std::string DesignId;               // DisignId for Model
//     std::string Designer;               // Designer nickname in utf8
//     std::string DesignerUserId;         // Designer user_id string
// };

// /* info in ModelInfo can be changed after initialization */
// class ModelInfo
// {
// public:
//     std::string cover_file;     // utf8 format
//     std::string license;        // utf8 format
//     std::string description;    // utf8 format
//     std::string copyright;      // utf8 format
//     std::string model_name;     // utf8 format
//     std::string origin;         // utf8 format

//     std::map<std::string, std::string> metadata_items; // other meta data items

//     void load(ModelInfo &info) {
//         this->cover_file    = info.cover_file;
//         this->license       = info.license;
//         this->description   = info.description;
//         this->copyright     = info.copyright;
//         this->model_name    = info.model_name;
//         this->origin        = info.origin;
//         this->metadata_items = info.metadata_items;
//     }
// };

#endif /* slic3r_Model_Wipe_Tower_hpp_ */

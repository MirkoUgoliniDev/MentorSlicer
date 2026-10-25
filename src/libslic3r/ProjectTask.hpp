#ifndef slic3r_ProjectTask_hpp_
#define slic3r_ProjectTask_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace Slic3r {

struct FilamentInfo
{
    int         id;         // filament id = extruder id, start with 0.
    std::string type;
    std::string color;
    std::string filament_id;
    std::string brand;
    float       used_m;
    float       used_g;
    int         tray_id;    // start with 0
    float       distance;
    int         ctype = 0;
    std::vector<std::string> colors = std::vector<std::string>();
    int         mapping_result = 0;
};

class BBLProject {
public:
    BBLProject() {
        /* give a default project name */
        project_name = "Untitled";
    }
    BBLProject(std::string name) {
        project_name = name;
    }

    std::string     project_id;
    std::string     project_model_id;       /* model id */
    std::string     project_design_id;      /* design_id */
    std::string     project_status;
    std::string     project_create_time;    /* created by cloud */
    std::string     project_url;            /* url storage on cloud */
    std::string     project_url_md5;        /* md5 of project url file */
    std::string     project_name;
    std::string     project_3mf_file;
    fs::path        project_path;
    std::string     project_content;
    std::string     project_country_code;


    /* deprecated apis */
    void set_name(std::string name) { project_name = name; }

    void reset();
};

} // namespace Slic3r

#endif //  slic3r_ProjectTask_hpp_

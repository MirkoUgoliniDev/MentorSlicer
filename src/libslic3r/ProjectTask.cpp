#include "libslic3r.h"
#include "Time.hpp"
#include "Thread.hpp"
#include "ProjectTask.hpp"


#include <thread>
#include <mutex>
#include <codecvt>

#include <boost/random.hpp>
#include <boost/log/trivial.hpp>
#include <boost/generator_iterator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "nlohmann/json.hpp"

using namespace nlohmann;

namespace pt = boost::property_tree;

namespace Slic3r {

    void BBLProject::reset()
    {
        project_model_id.clear();
        project_name.clear();
        project_id.clear();
        project_design_id.clear();
        project_status.clear();
        project_create_time.clear();
        project_url.clear();
        project_url_md5.clear();
        project_3mf_file.clear();
        project_path.clear();
    }

} // namespace Slic3r

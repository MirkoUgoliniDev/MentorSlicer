#ifndef slic3r_2DBed_hpp_
#define slic3r_2DBed_hpp_

#include <wx/wx.h>
#include "libslic3r/Config.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {
namespace GUI {

class Bed_2D : public wxPanel
{
    static const int Border = 10;

	bool		m_user_drawn_background = true;

    double		m_scale_factor;
	Vec2d		m_shift = Vec2d::Zero();
	Vec2d		m_pos = Vec2d::Zero();

    // Cache to avoid recomputing intersections on every paint
    std::vector<Vec2d> m_cached_shape;
    Polygon            m_cached_bed_polygon;
    BoundingBoxf       m_cached_bb{ Vec2d::Zero(), Vec2d::Zero() };
    Polylines          m_cached_grid;
    int                m_cached_step = 10; // 1cm grid

    void        update_cache(const std::vector<Vec2d>& shape);

    Point		to_pixels(const Vec2d& point, int height);
    void		set_pos(const Vec2d& pos);

public:
    explicit Bed_2D(wxWindow* parent);

    void repaint(const std::vector<Vec2d>& shape);
};


} // GUI
} // Slic3r

#endif /* slic3r_2DBed_hpp_ */

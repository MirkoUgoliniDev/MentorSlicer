#include <cassert>

#include "libslic3r/Flow.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/libslic3r.h"

#include "PresetHints.hpp"

#include <wx/intl.h> 

#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"

namespace Slic3r {

#define MIN_BUF_LENGTH	4096
std::string PresetHints::cooling_description(const Preset &preset)
{
	std::string out;
    //BBS: don't show cooling_description now
    /*
    bool cooling              = preset.config.opt_bool("cooling", 0);
    int  fan_cooling_layer_time = preset.config.opt_int("fan_cooling_layer_time", 0);
    int  full_fan_speed_layer = preset.config.opt_int("full_fan_speed_layer", 0);

    if (cooling) {
		int 	slow_down_layer_time 	= preset.config.opt_int("slow_down_layer_time", 0);
		int 	fan_min_speed 				= preset.config.opt_int("fan_min_speed", 0);
		int 	fan_max_speed 				= preset.config.opt_int("fan_max_speed", 0);
		int 	slow_down_min_speed				= int(preset.config.opt_float("slow_down_min_speed", 0) + 0.5);

        out += GUI::format(_L("If estimated layer time is below ~%1%s, "
                              "fan will run at %2%%% and print speed will be reduced "
                              "so that no less than %3%s are spent on that layer "
                              "(however, speed will never be reduced below %4%mm/s)."),
                              slow_down_layer_time, fan_max_speed, slow_down_layer_time, slow_down_min_speed);
        if (fan_cooling_layer_time > slow_down_layer_time) {
            out += "\n";
            if (fan_min_speed != fan_max_speed)
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at a proportionally decreasing speed between %2%%% and %3%%%."),
                               fan_cooling_layer_time, fan_max_speed, fan_min_speed);
            else
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at %2%%%"),
                               fan_cooling_layer_time, fan_min_speed);
        }
        out += "\n";
    }
	if (preset.config.opt_bool("reduce_fan_stop_start_freq", 0)) {
		int 	close_fan_the_first_x_layers 	= preset.config.opt_int("close_fan_the_first_x_layers", 0);
		int 	fan_min_speed 				= preset.config.opt_int("fan_min_speed", 0);

        if (full_fan_speed_layer > close_fan_the_first_x_layers + 1)
            out += GUI::format(_L("Fan speed will be ramped from zero at layer %1% to %2%%% at layer %3%."), close_fan_the_first_x_layers, fan_min_speed, full_fan_speed_layer);
        else {
            out += GUI::format(cooling ? _L("During the other layers, fan will always run at %1%%%") : _L("Fan will always run at %1%%%"), fan_min_speed) + " ";
            if (close_fan_the_first_x_layers > 1)
                out += GUI::format(_L("except for the first %1% layers."), close_fan_the_first_x_layers);
            else if (close_fan_the_first_x_layers == 1)
            	out += GUI::format(_L("except for the first layer."));
        }
    } else
       out += cooling ? _u8L("During the other layers, fan will be turned off.") : _u8L("Fan will be turned off.");
    */
    return out;
}

static const ConfigOptionFloatOrPercent& first_positive(const ConfigOptionFloatOrPercent *v1, const ConfigOptionFloatOrPercent &v2, const ConfigOptionFloatOrPercent &v3)
{
    return (v1 != nullptr && v1->value > 0) ? *v1 : ((v2.value > 0) ? v2 : v3);
}

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    std::string out;
    //BBS: don't show maximum_volumetric_flow_description now
 	return out;
}

std::string PresetHints::recommended_thin_wall_thickness(const PresetBundle &preset_bundle)
{
    std::string out;
    //BBS: don't show recommended_thin_wall_thickness description now
    /*
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    float   layer_height                        = float(print_config.opt_float("layer_height"));
    int     num_perimeters                      = print_config.opt_int("wall_loops");
    bool    thin_walls                          = print_config.opt_bool("detect_thin_wall");
    float   nozzle_diameter                     = float(printer_config.opt_float("nozzle_diameter", 0));
    
    std::string out;
	if (layer_height <= 0.f) {
		out += _utf8(L("Recommended object thin wall thickness: Not available due to invalid layer height."));
		return out;
	}
    
    if (num_perimeters > 0) {
        int num_lines = std::min(num_perimeters * 2, 10);
        out += (boost::format(_utf8(L("Recommended object thin wall thickness for layer height %.2f and"))) % layer_height).str() + " ";
        // Start with the width of two closely spaced 
        try {
            Flow external_perimeter_flow = Flow::new_from_config_width(
                frExternalPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("outer_wall_line_width"), 
                nozzle_diameter, layer_height);
            Flow perimeter_flow          = Flow::new_from_config_width(
                frPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("inner_wall_line_width"), 
                nozzle_diameter, layer_height);
	        double width = external_perimeter_flow.width() + external_perimeter_flow.spacing();
	        for (int i = 2; i <= num_lines; thin_walls ? ++ i : i += 2) {
	            if (i > 2)
	                out += ", ";
	            out += (boost::format(_utf8(L("%d lines: %.2f mm"))) % i %  width).str() + " ";
	            width += perimeter_flow.spacing() * (thin_walls ? 1.f : 2.f);
	        }
	    } catch (const FlowErrorNegativeSpacing &) {
            out = _utf8(L("Recommended object thin wall thickness: Not available due to excessively small extrusion width."));
        }
    }*/
    return out;
}


// Produce a textual explanation of the combined effects of the top/bottom_shell_layers
// versus top/bottom_min_shell_thickness. Which of the two values wins depends
// on the active layer height.
std::string PresetHints::top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle)
{
    std::string out;
    //BBS: don't show top_bottom_shell_thickness_explanation now
    /*
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    int 	top_shell_layers                = print_config.opt_int("top_shell_layers");
    int 	bottom_shell_layers             = print_config.opt_int("bottom_shell_layers");
    bool    has_top_layers 					= top_shell_layers > 0;
    bool    has_bottom_layers 				= bottom_shell_layers > 0;
    double  top_shell_thickness        	= print_config.opt_float("top_shell_thickness");
    double  bottom_shell_thickness  	= print_config.opt_float("bottom_shell_thickness");
    double  layer_height                    = print_config.opt_float("layer_height");
    //FIXME the following line takes into account the 1st extruder only.
    double  min_layer_height				= Slicing::min_layer_height_from_nozzle(printer_config, 1);

	if (layer_height <= 0.f) {
		out += _utf8(L("Top / bottom shell thickness hint: Not available due to invalid layer height."));
		return out;
	}

    if (has_top_layers) {
    	double top_shell_thickness = top_shell_layers * layer_height;
    	if (top_shell_thickness < top_shell_thickness) {
    		// top_solid_min_shell_thickness triggers even in case of normal layer height. Round the top_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(top_shell_thickness / layer_height);
    		top_shell_thickness = n * layer_height;
    	}
    	double top_shell_thickness_minimum = std::max(top_shell_thickness, top_shell_layers * min_layer_height);
        out += (boost::format(_utf8(L("Top shell is %1% mm thick for layer height %2% mm."))) % top_shell_thickness % layer_height).str();
        if (top_shell_thickness_minimum < top_shell_thickness) {
        	out += " ";
	        out += (boost::format(_utf8(L("Minimum top shell thickness is %1% mm."))) % top_shell_thickness_minimum).str();        	
        }
    } else
        out += _utf8(L("Top is open."));

    out += "\n";

    if (has_bottom_layers) {
    	double bottom_shell_thickness = bottom_shell_layers * layer_height;
    	if (bottom_shell_thickness < bottom_shell_thickness) {
    		// bottom_solid_min_shell_thickness triggers even in case of normal layer height. Round the bottom_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(bottom_shell_thickness / layer_height);
    		bottom_shell_thickness = n * layer_height;
    	}
    	double bottom_shell_thickness_minimum = std::max(bottom_shell_thickness, bottom_shell_layers * min_layer_height);
        out += (boost::format(_utf8(L("Bottom shell is %1% mm thick for layer height %2% mm."))) % bottom_shell_thickness % layer_height).str();
        if (bottom_shell_thickness_minimum < bottom_shell_thickness) {
        	out += " ";
	        out += (boost::format(_utf8(L("Minimum bottom shell thickness is %1% mm."))) % bottom_shell_thickness_minimum).str();        	
        }
    } else 
        out += _utf8(L("Bottom is open."));
    */
    return out;
}

}; // namespace Slic3r

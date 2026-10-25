#include "OverhangOptimization.hpp"
#include "TriangleMesh.hpp"
#include "ModelInstance.hpp"
#include <cmath>
#include "TriangleMeshSlicer.hpp"
//--output-----
//#include <iostream>
//#include <fstream>
//#include <iomanip>


#define EPS 1e-4
#define THRESHOLD 0.02f
namespace Slic3r {

	void OverhangOptimization::init(const ModelObject&         object,
                                float                      layer_height,
                                const std::vector<double>& input_layer_height,
                                Transform3d                m_trafo)
	{
		TriangleMesh		 mesh			= object.raw_mesh();
        TriangleMesh mesh_copy              = mesh;
		init_layer_height = layer_height;
		InputLayerHeight = input_layer_height;
		const ModelInstance &first_instance = *object.instances.front();
		mesh.transform(first_instance.get_matrix(), first_instance.is_left_handed());

		face_layer.reserve(mesh.facets_count());
		for (stl_triangle_vertex_indices face : mesh.its.indices) {
			stl_vertex vertex[3] = { mesh.its.vertices[face[0]], mesh.its.vertices[face[1]], mesh.its.vertices[face[2]] };
			stl_vertex n         = face_normal_normalized(vertex);
			std::pair<float, float> face_z_span {
				std::min(std::min(vertex[0].z(), vertex[1].z()), vertex[2].z()),
				std::max(std::max(vertex[0].z(), vertex[1].z()), vertex[2].z())
			};
			face_layer.emplace_back(Face_Layer({ face_z_span, n }));
			if (face_z_span.first < height_min)
				height_min = face_z_span.first;
			if (face_z_span.second > height_max)
				height_max = face_z_span.second;
		}

		float obj_height = height_max - height_min;
		MeshSlicingParamsEx slicing_params_ex;
		slicing_params_ex.trafo = m_trafo;
		std::vector<float> z_values;
		int layerNum = obj_height / 0.1f;
		for (int i = 0; i < layerNum; i++)
		{
			z_values.push_back((i+1)*0.1f);
		}
		overhang_ratio = get_mesh_overhang(mesh_copy.its, z_values, slicing_params_ex);
       
        //std::ofstream outfile_input("input_overhang.txt");
        for (int i = 0; i < overhang_ratio.size(); i++) {
            if (overhang_ratio[i] > THRESHOLD) 
			{
                is_quick = false;
                break;
			}
           // outfile_input << overhang_ratio[i] << std::endl;
        }
        //outfile_input.close();*/
		//std::sort(face_layer.begin(), face_layer.end(), [](const Face_Layer &f1, const Face_Layer &f2) { return f1.z_span < f2.z_span; });
	}

	std::vector<double> OverhangOptimization::get_layer_height()
	{
        // std::vector<float> result_b;
        float obj_height = height_max - height_min;
        // int layer_n = (int)(obj_height / init_layer_height);
        // result_b.resize(layer_n,init_layer_height);
        std::vector<double> result;
       /* if (is_quick) {
            double first_temp_height = 0;
            while (first_temp_height <= obj_height) {
                result.push_back(first_temp_height);
                result.push_back(init_layer_height);
                first_temp_height += init_layer_height;
            }
            return result;
        }*/
        std::vector<std::pair<float, float>> change_height;

        stl_vertex up_nor = Vec3f(0.f, 0.f, -1.f);
        // 0.3420 cos70
        for (Face_Layer& fl : face_layer) {
            // float arc_zeor = up_nor.dot(Vec3f(0.f,0.f,-1.f));

            float arc = fl.nor.dot(up_nor);
            if (arc < 0.998f && arc >= 0.866f) {
                // change_height.push_back(fl.span);
                // int bot_z = (int)(fl.z_span.first / init_layer_height) ;
                // int up_z = (int)(fl.z_span.second / init_layer_height) ;
                if (change_height.empty()) {
                    change_height.push_back(fl.z_span);
                    continue;
                }

                bool pass = false;
                for (int ci = 0; ci < change_height.size(); ci++) {
                    if (fl.z_span.first >= change_height[ci].first && fl.z_span.first <= change_height[ci].second) {
                        if (fl.z_span.second > change_height[ci].second) {
                            change_height[ci].second = fl.z_span.second;
                            pass                     = true;
                        }
                    }

                    if (fl.z_span.second >= change_height[ci].first && fl.z_span.second <= change_height[ci].second) {
                        if (fl.z_span.first < change_height[ci].first) {
                            change_height[ci].first = fl.z_span.first;
                            pass                    = true;
                        }
                    }

                    if (fl.z_span.first >= change_height[ci].first && fl.z_span.second <= change_height[ci].second)
                        pass = true;

                    if (pass)
                        break;
                }

                if (!pass)
                    change_height.push_back(fl.z_span);

                // for (int i = bot_z; i <= up_z; i++)
                //{
                //	result_b[i] = 0.1f;
                //	//result_b.insert(result_b.begin() + i,0.1f);
                // }
            }
        }

        //---next work
       
#if 0
		bool is_filter = false;
		if (InputLayerHeight.size() < (obj_height / (10.f * init_layer_height)))
		{
			float print_z = 0.f;
			std::vector<bool> ch_mark(change_height.size(),false);
			while (print_z + EPS < obj_height)
			{
				float current_z = print_z + init_layer_height;
				int layer_z = current_z / 0.1f;				
				//if(1)
			
				if (overhang_ratio[layer_z] > THRESHOLD)
				{
					for (int ci = 0; ci < change_height.size(); ci++)
					{
						if (current_z > change_height[ci].first && current_z < change_height[ci].second)
						{
							if(!ch_mark[ci]/*&&print_z <= change_height[ci].first*/)
							//if (print_z <= change_height[ci].first)
							{
								double sz = (double)(change_height[ci].first - print_z);
								print_z = change_height[ci].first;
								result.push_back(print_z);
								result.push_back(sz);


								while ((print_z + 0.1f + EPS) <= change_height[ci].second)
								{
									print_z += 0.1f;
									result.push_back(print_z);
									result.push_back(0.1f);
								}

								double szz = (double)(change_height[ci].second - print_z);
								print_z = change_height[ci].second;
								result.push_back(print_z);
								result.push_back(szz);
								ch_mark[ci] = true;
							}
						}
					}
				}
				else
				{
					is_filter = true;
				}
				result.push_back(print_z);
				result.push_back(init_layer_height);
				print_z += init_layer_height;
			}
		}
		else
		{
			std::vector<std::pair<int, int>> enter_area;
			for (int ci = 0; ci < change_height.size(); ci++)
			{				
				int begin = -1;
				int end = -1;
				int current_ci = -1;
				for (int i = 0; i < InputLayerHeight.size(); i += 2)
				{
					if (begin != -1 && current_ci!=-1&&InputLayerHeight[i]>=change_height[current_ci].second)
					{
						end = i;
					}
					if (begin == -1 && InputLayerHeight[i] > change_height[ci].first && InputLayerHeight[i] < change_height[ci].second)
					{						
						begin = i;
						current_ci = ci;
					}
					if (end != -1 && begin != -1)
					{
						break;
					}
				}
				if (end != -1 && begin != -1)
					enter_area.push_back(std::make_pair(begin,end));
			}


			std::sort(enter_area.begin(), enter_area.end(), [&](std::pair<int, int> a, std::pair<int, int> b) { return a.first < b.first; });
			std::vector<bool> mark_h(InputLayerHeight.size(),false);
			for (int i = 0; i < enter_area.size(); i++)
			{
				for (int ei = enter_area[i].first; ei <= enter_area[i].second; ei++)
				{
					mark_h[ei] = true;
				}
			}

			enter_area.clear();
			int begin = -1;
			int end = -1;
			for (int i = 0; i < mark_h.size(); i++)
			{
				if (i != 0)
				{
					if (mark_h[i] && !mark_h[i - 1])
					{
						begin = i;
					}
					if (!mark_h[i] && mark_h[i - 1])
					{
						end = i-1;
					}
				}
				if (begin != -1 && end != -1)
				{
					enter_area.push_back(std::make_pair(begin,end));
					begin = -1;
					end = -1;
				}
			}

			
			for (int i = 0; i < InputLayerHeight.size(); i+=1)
			{
				bool is_pass = false;
				for (int ei = 0; ei < enter_area.size(); ei++)
				{
					int f_index = enter_area[ei].first;
					int e_index = enter_area[ei].second;
					if (i >= f_index && i < e_index&&(i%2)==0)
					{						
						//overhang_ratio[layer_z];
						//int index = f_index;
						for (float current_z = InputLayerHeight[f_index]; current_z < InputLayerHeight[e_index]; /*current_z += 0.1f*/)
						{			
							int layer_z = current_z / 0.1f;
							if (overhang_ratio[layer_z] < THRESHOLD&&(i%2)==0)
							{								
								//int end_index = InputLayerHeight[e_index] / 0.1f;
								//int span = end_index - layer_z;
								//float equal_height = InputLayerHeight[index];
								///*if (span > 0 && span <= 5 && (index % 2) == 0)
								//{
								//	equal_height = 0.1f+(InputLayerHeight[index+1] - 0.1f) / (span*1.0f);
								//}*/
								//result.push_back(equal_height);
								//result.push_back(InputLayerHeight[index+1]);
								//current_z += InputLayerHeight[index+1];
								//current_z -= 0.1f;
								//index += 2;
								
								int qi = -1;
								for (int hi = f_index-2; hi < e_index; hi += 2)
								{
									if (current_z >= InputLayerHeight[hi] /*&& current_z < InputLayerHeight[hi + 2]*/)
									{
										qi = hi;
										break;
									}
								}

								result.push_back(current_z);
								result.push_back(InputLayerHeight[qi+1]);
								current_z += InputLayerHeight[qi + 1];
								is_filter = true;
								continue;						
							}

							result.push_back(current_z);
							result.push_back(0.1f);
							current_z += 0.1f;
						}
						i = e_index-1;
						is_pass = true;
						break;
					}
				}
				if (!is_pass)
				{
					result.push_back(InputLayerHeight[i]);
				}
			}
		}
#else
        for (int ci = 0; ci < change_height.size(); ci++) {
            if ((change_height[ci].second - change_height[ci].first) < init_layer_height) {
                change_height.erase(change_height.begin() + ci);
                ci--;
            }
        }

        std::vector<std::pair<int, int>> mark_overhang_beginAndend_index;
        for (int ci = 0; ci < change_height.size(); ci++) {
            int index_b = -1;
            int index_e = -1;
            for (int i = 0; i < InputLayerHeight.size(); i += 2) {
                if (InputLayerHeight[i] > (change_height[ci].first - init_layer_height) && InputLayerHeight[i] < change_height[ci].first) {
                    index_b = i;
                }
                if (InputLayerHeight[i] > change_height[ci].second && InputLayerHeight[i] < change_height[ci].second + init_layer_height) {
                    index_e = i;
                }
                if (index_b != -1 && index_e != -1) {
                    mark_overhang_beginAndend_index.push_back(std::make_pair(index_b, index_e));
                    break;
                }
            }
        }

        if (InputLayerHeight.size() < (obj_height / (10.f * init_layer_height))) {
            float print_z = 0.f;
            while (print_z + EPS < obj_height) {
                bool is_change = false;
                for (int ci = 0; ci < change_height.size(); ci++) {
                    if (print_z > change_height[ci].first - init_layer_height && print_z < change_height[ci].second + init_layer_height) {
                        is_change = true;
                        for (; print_z < change_height[ci].second + init_layer_height; /*print_z += 0.1f*/) {
                            int layer_z = print_z / 0.1f;
                            if (overhang_ratio[layer_z] < THRESHOLD) {
                                result.push_back(print_z);
                                result.push_back(init_layer_height);
                                print_z += init_layer_height;
                                continue;
                            }
                            result.push_back(print_z);
                            result.push_back(0.1f);
                            print_z += 0.1f;
                        }
                    }
                    if (is_change)
                        break;
                }

                if (!is_change) {
                    result.push_back(print_z);
                    result.push_back(init_layer_height);
                    print_z += init_layer_height;
                }
            }			
        } else {
            std::vector<std::pair<int, int>> enter_area;
            for (int ci = 0; ci < change_height.size(); ci++) {
                int begin      = -1;
                int end        = -1;
                int current_ci = -1;
                for (int i = 0; i < InputLayerHeight.size(); i += 2) {
                    if (begin != -1 && current_ci != -1 && InputLayerHeight[i] >= change_height[current_ci].second) {
                        end = i;
                    }
                    if (begin == -1 && InputLayerHeight[i] > change_height[ci].first && InputLayerHeight[i] < change_height[ci].second) {
                        begin      = i;
                        current_ci = ci;
                    }
                    if (end != -1 && begin != -1) {
                        break;
                    }
                }
                if (end != -1 && begin != -1)
                    enter_area.push_back(std::make_pair(begin, end));
            }

            std::sort(enter_area.begin(), enter_area.end(), [&](std::pair<int, int> a, std::pair<int, int> b) { return a.first < b.first; });
            std::vector<bool> mark_h(InputLayerHeight.size(), false);
            for (int i = 0; i < enter_area.size(); i++) {
                for (int ei = enter_area[i].first; ei <= enter_area[i].second; ei++) {
                    mark_h[ei] = true;
                }
            }

            enter_area.clear();
            int begin = -1;
            int end   = -1;
            for (int i = 0; i < mark_h.size(); i++) {
                if (i != 0) {
                    if (mark_h[i] && !mark_h[i - 1]) {
                        begin = i;
                    }
                    if (!mark_h[i] && mark_h[i - 1]) {
                        end = i - 1;
                    }
                }
                if (begin != -1 && end != -1) {
                    enter_area.push_back(std::make_pair(begin, end));
                    begin = -1;
                    end   = -1;
                }
            }

            for (int i = 0; i < InputLayerHeight.size(); i += 1) {
                bool is_pass = false;
                for (int ei = 0; ei < enter_area.size(); ei++) {
                    int f_index = enter_area[ei].first;
                    int e_index = enter_area[ei].second;
                    if (i >= f_index && i < e_index && (i % 2) == 0) {
                        for (float current_z = InputLayerHeight[f_index]; current_z < InputLayerHeight[e_index]; /*current_z += 0.1f*/) {
                            int layer_z = current_z / 0.1f;
                            if (overhang_ratio[layer_z] < THRESHOLD && (i % 2) == 0) {
                                int qi = -1;
                                for (int hi = f_index - 2; hi < e_index; hi += 2) {
                                    if (current_z >= InputLayerHeight[hi] /*&& current_z < InputLayerHeight[hi + 2]*/) {
                                        qi = hi;
                                        break;
                                    }
                                }

                                result.push_back(current_z);
                                result.push_back(InputLayerHeight[qi + 1]);
                                current_z += InputLayerHeight[qi + 1];
                                // is_filter = true;
                                continue;
                            }

                            result.push_back(current_z);
                            result.push_back(0.1f);
                            current_z += 0.1f;
                        }
                        i       = e_index - 1;
                        is_pass = true;
                        break;
                    }
                }
                if (!is_pass) {
                    result.push_back(InputLayerHeight[i]);
                }
            }
        }

#endif
        return result;
	}

	//init_layer_height > 0.1f
    std::vector<double> OverhangOptimization::smooth_layher_height(const std::vector<double>& r) 
	{ 
		std::vector<double> ret;
        std::vector<double> ret_lh;
		std::vector<double> br = r;       
	
        for (int i = 3; i < br.size(); i += 2)
            ret_lh.push_back(br[i]);
		//even
      						
        while (1) {
            bool pass = true;
            for (int i = 0; i < ret_lh.size(); i++) {
                if (i < ret_lh.size() - 1) {
                    if ((ret_lh[i] - ret_lh[i + 1]) < -0.02f) {
                        ret_lh[i] += 0.01f;
                        ret_lh[i + 1] -= 0.01f;
                        pass = false;
                    }
                    if ((ret_lh[i] - ret_lh[i + 1]) > 0.02f) {
                        ret_lh[i] -= 0.01f;
                        ret_lh[i + 1] += 0.01f;
                        pass = false;
                    }
                }
            }
            if (pass)
                break;
        }

		double lay_height = 0;
		for (int i = 0; i < ret_lh.size(); i++) 
		{
            ret.push_back(lay_height);
            ret.push_back(ret_lh[i]);
            lay_height += ret_lh[i];
		}
      
        return ret;
	}


	//-----need  insert to create progress-----
	std::vector<double> OverhangOptimization::smooth_layer(const std::vector<double> r)
	{
		std::vector<double> br = r;
		auto gauss_kernel = [&] (float u) -> std::vector<double> {
           
            double two_sq_sigma = 2.0 * init_layer_height * init_layer_height;
            double inv_root_two_pi_sq_sigma = 1.0 / ::sqrt(M_PI * two_sq_sigma);

			std::vector<double> ret;
			for (unsigned int i = 0; i < 6; ++i)
            {
                double x = (double)i - (double)init_layer_height;
                ret.push_back(inv_root_two_pi_sq_sigma * ::exp(-x * x / two_sq_sigma));
            }
			return ret;
        }; 

        auto gauss_kernel_k = [](unsigned int radius) -> std::vector<double> {
            unsigned int        size = 2 * radius + 1;
            std::vector<double> ret;
            ret.reserve(size);

            // Reworked from static inline int getGaussianKernelSize(float sigma) taken from
            // opencv-4.1.2\modules\features2d\src\kaze\AKAZEFeatures.cpp
            double sigma                    = 0.3 * (double) (radius - 1) + 0.8;
            double two_sq_sigma             = 2.0 * sigma * sigma;
            double inv_root_two_pi_sq_sigma = 1.0 / ::sqrt(M_PI * two_sq_sigma);

            for (unsigned int i = 0; i < size; ++i) {
                double x = (double) i - (double) radius;
                ret.push_back(inv_root_two_pi_sq_sigma * ::exp(-x * x / two_sq_sigma));
            }

            return ret;
        };


			



		int smooth_n = 6;
		std::vector<float> cos_r;
		float span_c = M_PI / (2.f*(float)smooth_n);
		for (int si = 0; si < smooth_n; si++)
		{
			cos_r.push_back((float)std::cos(0.0f + si*span_c));
		}

		int frist_change = 0;

		for (int ri = 3; ri < br.size(); ri+=2)
		{
			
			if ((ri+2)<br.size() && std::abs(br[ri]- init_layer_height)<EPS && std::abs(br[ri+2]-init_layer_height)>1e-3f)
			{
				//std::vector<double> gauss_res = gauss_kernel(br[ri]);
				int last_i = 0;
				for (int bi = 0; bi < 6; bi++)
				{
					if (std::abs(br[ri - 2*bi] - 0.1f) < EPS)
						break;
					if (ri - 2 * bi >= 0)
					{
						br[ri - 2 * bi] = 0.1f + (init_layer_height - 0.1f) * cos_r[5 - bi];
						last_i = ri - 2 * bi;
					}
				}
				if (frist_change == 0)
				{
					frist_change = last_i;
				}
			}
			else if ((ri + 2) <br.size() && std::abs(br[ri] - 0.1f) < EPS && std::abs(br[ri + 2] - 0.1f) > 1e-3f)
			{
				//std::vector<double> gauss_res = gauss_kernel(br[ri]);	
				int last_i = 0;
				for (int bi = 0; bi < 6; bi++)
				{
					if (bi>0&&std::abs(br[ri + 2*bi] - 0.1f) < EPS)
						break;
					if ((ri + 2 * bi) <= (br.size()))
					{
						br[ri + 2 * bi] = 0.1f + (init_layer_height - 0.1f) * cos_r[5 - bi];
						last_i = ri + 2 * bi;
					}
				}
				ri = last_i;
			}
		}

		float begin_print_z = 0.f;
		for (int i = frist_change-1; i < br.size(); i+=2)
		{
			if (begin_print_z == 0.f)
			{
				begin_print_z = br[i];
				continue;
			}
			br[i] = begin_print_z + br[i - 1];
			begin_print_z = br[i];
		}

		return br;
	}
}
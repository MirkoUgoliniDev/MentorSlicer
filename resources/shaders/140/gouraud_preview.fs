#version 140

uniform vec4 uniform_color;
uniform float emission_factor;

uniform vec4 specificColorLayers[256];	// vec4(color, z), 最多显示256个分段
uniform int layersCount;	

// x = tainted, y = specular;
in vec2 intensity;
in vec4 world_pos;

void main()
{
	vec3 _color = uniform_color.rgb;
    if (layersCount > 0) {
		for (int i = 0; i < layersCount; ++i) 
		{
			vec4 layerInfo = specificColorLayers[i];
			if (world_pos.z >= layerInfo.w)
				_color = vec3(layerInfo.rgb);
			else 
				break;
		}
	} 
    gl_FragColor = vec4(vec3(intensity.y) + _color * (intensity.x + emission_factor), uniform_color.a);
}




	

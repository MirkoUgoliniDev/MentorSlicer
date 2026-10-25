#version 140

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;

void main()
{
	vec3 _color = uniform_color.rgb;

    gl_FragColor = vec4(vec3(intensity.y) + _color * (intensity.x + emission_factor), 0.5);
}
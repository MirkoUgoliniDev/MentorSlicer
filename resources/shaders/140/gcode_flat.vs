#version 140

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform vec3 position_origin;

in vec3 v_position;

void main()
{
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position * 0.02 + position_origin, 1.0);
}

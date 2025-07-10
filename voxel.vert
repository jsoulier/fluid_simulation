#version 450

layout(location = 0) in vec3 inPosition;
layout(set = 0, binding = 0, r32f) uniform readonly image3D cells;
layout(set = 1, binding = 0) uniform uniformViewProjMatrix
{
    mat4 viewProjMatrix;
};

void main()
{
    /* TODO: gl_InstanceIndex or gl_InstanceID? */
    ivec3 bounds = imageSize(cells);
    uint z = gl_InstanceIndex / (bounds.x * bounds.y);
    uint y = (gl_InstanceIndex / bounds.x) % bounds.y;
    uint x = gl_InstanceIndex % bounds.x;
    gl_Position = viewProjMatrix * vec4(inPosition + vec3(x, y, z), 1.0f);
}
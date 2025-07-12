#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out float outValue;
layout(set = 0, binding = 0, r32f) uniform readonly image3D cells;
layout(set = 1, binding = 0) uniform uniformViewProj
{
    mat4 viewProj;
};
layout(set = 1, binding = 1) uniform uniformParams
{
    uint size;
};

void main()
{
    /* TODO: gl_InstanceIndex or gl_InstanceID? */
    uint z = gl_InstanceIndex / (size * size);
    uint y = (gl_InstanceIndex / size) % size;
    uint x = gl_InstanceIndex % size;
    outValue = imageLoad(cells, ivec3(x, y, z)).x;
    if (outValue > 0.0f)
    {
        gl_Position = viewProj * vec4(inPosition + vec3(x, y, z), 1.0f);
    }
    else
    {
        gl_Position = vec4(0.0f, 0.0f, 2.0f, 1.0f);
    }
}
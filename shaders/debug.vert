#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out float outValue;
layout(set = 0, binding = 0) uniform sampler3D inImage;
layout(set = 1, binding = 0) uniform uniformViewProj
{
    mat4 viewProj;
};

void main()
{
    /* TODO: gl_InstanceIndex or gl_InstanceID? */
    ivec3 size = textureSize(inImage, 0);
    uint z = gl_InstanceIndex / (size.x * size.y);
    uint y = (gl_InstanceIndex / size.x) % size.y;
    uint x = gl_InstanceIndex % size.x;
    outValue = texelFetch(inImage, ivec3(x, y, z), 0).x;
    if (abs(outValue) > 0.0f)
    {
        gl_Position = viewProj * vec4(inPosition + vec3(x, y, z), 1.0f);
    }
    else
    {
        gl_Position = vec4(0.0f, 0.0f, 2.0f, 1.0f);
    }
}
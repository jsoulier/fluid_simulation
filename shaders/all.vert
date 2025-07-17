#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outVelocity;
layout(location = 1) out float outDensity;
layout(set = 0, binding = 0) uniform sampler3D inVelocityX;
layout(set = 0, binding = 1) uniform sampler3D inVelocityY;
layout(set = 0, binding = 2) uniform sampler3D inVelocityZ;
layout(set = 0, binding = 3) uniform sampler3D inDensity;
layout(set = 1, binding = 0) uniform uniformViewProj
{
    mat4 viewProj;
};

void main()
{
    /* TODO: gl_InstanceIndex or gl_InstanceID? */
    ivec3 size = textureSize(inVelocityX, 0);
    uint z = gl_InstanceIndex / (size.x * size.y);
    uint y = (gl_InstanceIndex / size.x) % size.y;
    uint x = gl_InstanceIndex % size.x;
    ivec3 id = ivec3(x, y, z);
    outVelocity.x = texelFetch(inVelocityX, id, 0).x;
    outVelocity.y = texelFetch(inVelocityY, id, 0).x;
    outVelocity.z = texelFetch(inVelocityZ, id, 0).x;
    outDensity = texelFetch(inDensity, id, 0).x;
    if (outDensity > 0.0f)
    {
        gl_Position = viewProj * vec4(inPosition + vec3(x, y, z), 1.0f);
    }
    else
    {
        gl_Position = vec4(0.0f, 0.0f, 2.0f, 1.0f);
    }
}
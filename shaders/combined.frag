#version 450

#include "shader.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler3D inVelocityX;
layout(set = 2, binding = 1) uniform sampler3D inVelocityY;
layout(set = 2, binding = 2) uniform sampler3D inVelocityZ;
layout(set = 2, binding = 3) uniform sampler3D inDensity;
layout(set = 3, binding = 0) uniform uniformInverseView
{
    mat4 inverseView;
};
layout(set = 3, binding = 1) uniform uniformInverseProj
{
    mat4 inverseProj;
};
layout(set = 3, binding = 2) uniform uniformCameraPosition
{
    vec3 cameraPosition;
};

void main()
{
    ivec3 size = textureSize(inVelocityX, 0);
    vec3 rayDirection = GetRayDirection(inverseView, inverseProj, inTexCoord);
    outColor = vec4(0.0f);
    for (int i = 0; i < MaxSteps; i++)
    {
        ivec3 id = ivec3(cameraPosition + rayDirection * i * StepSize);
        if (any(greaterThanEqual(id, size)) || any(lessThan(id, ivec3(0))))
        {
            continue;
        }
        outColor.r += abs(texelFetch(inVelocityX, id, 0).x);
        outColor.g += abs(texelFetch(inVelocityY, id, 0).x);
        outColor.b += abs(texelFetch(inVelocityZ, id, 0).x);
        outColor.a += texelFetch(inDensity, id, 0).x;
    }
    outColor *= Scale;
}
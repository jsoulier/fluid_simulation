#version 450

layout(location = 0) in vec3 inVelocity;
layout(location = 1) in float inDensity;
layout(location = 0) out vec4 outColor;

const float VelocityScale = 5000.0f;
const float DensityScale = 100.0f;

void main()
{
    vec3 color = abs(inVelocity) * VelocityScale;
    float alpha = inDensity * DensityScale;
    outColor = vec4(color, alpha);
}
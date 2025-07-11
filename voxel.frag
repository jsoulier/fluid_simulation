#version 450

layout(location = 0) in float inValue;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vec3(inValue), 1.0f);
}
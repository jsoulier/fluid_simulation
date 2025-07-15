#version 450

layout(location = 0) in float inValue;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(1.0f, 0.0f, 0.0f, inValue * 5.0f);
}
#version 450

layout(location = 0) in float inValue;
layout(location = 0) out vec4 outColor;

const float Scale = 20.0f;

void main()
{
    vec3 color = vec3(1.0f);
    float alpha = abs(inValue * Scale);
    outColor = vec4(color, alpha);
}
#version 450

layout(location = 0) in vec3 inPosition;
layout(set = 1, binding = 0) uniform uniformViewProj
{
    mat4 viewProj;
};
layout(set = 1, binding = 1) uniform uniformSize
{
    uint size;
};

void main()
{
    gl_Position = viewProj * vec4((inPosition + vec3(0.5f)) * vec3(size - 1), 1.0f);
}
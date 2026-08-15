#ifndef SHADER_HLSL
#define SHADER_HLSL

// https://github.com/libsdl-org/SDL_shadercross/issues/211
#include "../src/config.hpp"

void LinSolve(int3 id, Texture3D<float> inImage, RWTexture3D<float> outImage, float a, float c)
{
    outImage[id] =
        (inImage.Load(int4(id, 0)) +
            a * (outImage[id + int3( 1, 0, 0 )] +
                 outImage[id + int3(-1, 0, 0 )] +
                 outImage[id + int3( 0, 1, 0 )] +
                 outImage[id + int3( 0,-1, 0 )] +
                 outImage[id + int3( 0, 0, 1 )] +
                 outImage[id + int3( 0, 0,-1 )])) / c;
}

void Advect(
    int3 id,
    RWTexture3D<float> outImage,
    Texture3D<float> inImage,
    Texture3D<float> inVelocityX,
    Texture3D<float> inVelocityY,
    Texture3D<float> inVelocityZ,
    float deltaTime,
    int3 size)
{
    float N = size.x - 2;
    float dtx = deltaTime * N;
    float dty = deltaTime * N;
    float dtz = deltaTime * N;
    float tmp1 = dtx * inVelocityX.Load(int4(id, 0));
    float tmp2 = dty * inVelocityY.Load(int4(id, 0));
    float tmp3 = dtz * inVelocityZ.Load(int4(id, 0));
    float x = clamp(id.x - tmp1, 0.5f, N + 0.5f);
    float y = clamp(id.y - tmp2, 0.5f, N + 0.5f);
    float z = clamp(id.z - tmp3, 0.5f, N + 0.5f);
    float i0 = floor(x);
    float i1 = i0 + 1.0f;
    float j0 = floor(y);
    float j1 = j0 + 1.0f;
    float k0 = floor(z);
    float k1 = k0 + 1.0f;
    float s1 = x - i0;
    float s0 = 1.0f - s1;
    float t1 = y - j0;
    float t0 = 1.0f - t1;
    float u1 = z - k0;
    float u0 = 1.0f - u1;
    int i0i = int(i0);
    int i1i = int(i1);
    int j0i = int(j0);
    int j1i = int(j1);
    int k0i = int(k0);
    int k1i = int(k1);
    outImage[id] =
        s0 * (t0 * (u0 * inImage.Load(int4(i0i, j0i, k0i, 0)) +
                    u1 * inImage.Load(int4(i0i, j0i, k1i, 0))) +
             (t1 * (u0 * inImage.Load(int4(i0i, j1i, k0i, 0)) +
                    u1 * inImage.Load(int4(i0i, j1i, k1i, 0))))) +
        s1 * (t0 * (u0 * inImage.Load(int4(i1i, j0i, k0i, 0)) +
                    u1 * inImage.Load(int4(i1i, j0i, k1i, 0))) +
             (t1 * (u0 * inImage.Load(int4(i1i, j1i, k0i, 0)) +
                    u1 * inImage.Load(int4(i1i, j1i, k1i, 0)))));
}

#endif

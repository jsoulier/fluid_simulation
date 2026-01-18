#ifndef SHADER_GLSL
#define SHADER_GLSL

const float kStepSize = 1;
const int kMaxSteps = 512;
const float kScale = 50.0f;

vec3 GetRayDirection(mat4 inverseView, mat4 inverseProj, vec2 texcoord)
{
    vec4 ndc = vec4(texcoord * 2.0f - 1.0f, 0.0f, 1.0f);
    vec4 viewRay = inverseProj * ndc;
    viewRay /= viewRay.w;
    vec4 worldRay = inverseView * vec4(viewRay.xyz, 0.0f);
    return normalize(worldRay.xyz);
}

#define LIN_SOLVE(id, inImage, outImage, a, c) \
    do \
    { \
        imageStore(outImage, id, vec4( \
            (texelFetch(inImage, id, 0).x + \
                a * (texelFetch(inImage, id + ivec3( 1, 0, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec3(-1, 0, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec3( 0, 1, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec3( 0,-1, 0 ), 0).x + \
                     texelFetch(inImage, id + ivec3( 0, 0, 1 ), 0).x + \
                     texelFetch(inImage, id + ivec3( 0, 0,-1 ), 0).x)) / c)); \
    } \
    while (false) \

#define ADVECT(id, outImage, inImage, inVelocityX, inVelocityY, inVelocityZ, deltaTime, size) \
    do \
    { \
        float N = size.x - 2; \
        float dtx = deltaTime * (N - 2); \
        float dty = deltaTime * (N - 2); \
        float dtz = deltaTime * (N - 2); \
        float tmp1 = dtx * texelFetch(inVelocityX, id, 0).x; \
        float tmp2 = dty * texelFetch(inVelocityY, id, 0).x; \
        float tmp3 = dtz * texelFetch(inVelocityZ, id, 0).x; \
        float x = clamp(id.x - tmp1, 0.5f, N + 0.5f); \
        float y = clamp(id.y - tmp2, 0.5f, N + 0.5f); \
        float z = clamp(id.z - tmp3, 0.5f, N + 0.5f); \
        float i0 = floor(x); \
        float i1 = i0 + 1.0f; \
        float j0 = floor(y); \
        float j1 = j0 + 1.0f; \
        float k0 = floor(z); \
        float k1 = k0 + 1.0f; \
        float s1 = x - i0; \
        float s0 = 1.0f - s1; \
        float t1 = y - j0; \
        float t0 = 1.0f - t1; \
        float u1 = z - k0; \
        float u0 = 1.0f - u1; \
        int i0i = int(i0); \
        int i1i = int(i1); \
        int j0i = int(j0); \
        int j1i = int(j1); \
        int k0i = int(k0); \
        int k1i = int(k1); \
        imageStore(outImage, id, vec4( \
            s0 * (t0 * (u0 * texelFetch(inImage, ivec3(i0i, j0i, k0i), 0).x + \
                        u1 * texelFetch(inImage, ivec3(i0i, j0i, k1i), 0).x) + \
                 (t1 * (u0 * texelFetch(inImage, ivec3(i0i, j1i, k0i), 0).x + \
                        u1 * texelFetch(inImage, ivec3(i0i, j1i, k1i), 0).x))) + \
            s1 * (t0 * (u0 * texelFetch(inImage, ivec3(i1i, j0i, k0i), 0).x + \
                        u1 * texelFetch(inImage, ivec3(i1i, j0i, k1i), 0).x) + \
                 (t1 * (u0 * texelFetch(inImage, ivec3(i1i, j1i, k0i), 0).x + \
                        u1 * texelFetch(inImage, ivec3(i1i, j1i, k1i), 0).x))))); \
    } \
    while (false) \

#endif
#ifndef SHADER_GLSL
#define SHADER_GLSL

const ivec3 VonNeumann[6] = ivec3[]
(
    ivec3( 1, 0, 0 ),
    ivec3(-1, 0, 0 ),
    ivec3( 0, 1, 0 ),
    ivec3( 0,-1, 0 ),
    ivec3( 0, 0, 1 ),
    ivec3( 0, 0,-1 )
);

/* Modified to use a Jacobi iteration to avoid writing to the read texture */
#define LIN_SOLVE(id, inImage, outImage, a, c) \
    do \
    { \
        float value = 0.0f; \
        for (int i = 0; i < 6; i++) \
        { \
            ivec3 neighbor = id + VonNeumann[i]; \
            value += imageLoad(inImage, neighbor).x; \
        } \
        value *= a; \
        value += imageLoad(inImage, id).x; \
        value /= c; \
        value = min(value, 1.0f); \
        imageStore(outImage, id, vec4(value)); \
    } \
    while (false) \

/* TODO: copied from Mike Ash. refactor */
#define ADVECT(id, outImage, inImage, inVelocityX, inVelocityY, inVelocityZ, deltaTime, size) \
    do \
    { \
        float N = size.x; \
        vec3 dt = deltaTime * (size - 2); \
        float tmp1 = dt.x * imageLoad(inVelocityX, id).x; \
        float tmp2 = dt.y * imageLoad(inVelocityY, id).x; \
        float tmp3 = dt.z * imageLoad(inVelocityZ, id).x; \
        float x = id.x - tmp1; \
        float y = id.y - tmp2; \
        float z = id.z - tmp3; \
        if (x < 0.5f) \
        { \
            x = 0.5f; \
        } \
        if (x > N + 0.5f) \
        { \
            x = N + 0.5f; \
        } \
        float i0 = floor(x); \
        float i1 = i0 + 1.0f; \
        if (y < 0.5f) \
        { \
            y = 0.5f; \
        } \
        if (y > N + 0.5f) \
        { \
            y = N + 0.5f; \
        } \
        float j0 = floor(y); \
        float j1 = j0 + 1.0f; \
        if (z < 0.5f) \
        { \
            z = 0.5f; \
        } \
        if (z > N + 0.5f) \
        { \
            z = N + 0.5f; \
        } \
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
        float value = \
            s0 * (t0 * (u0 * imageLoad(inImage, ivec3(i0i, j0i, k0i)).x + \
                        u1 * imageLoad(inImage, ivec3(i0i, j0i, k1i)).x) + \
                 (t1 * (u0 * imageLoad(inImage, ivec3(i0i, j1i, k0i)).x + \
                        u1 * imageLoad(inImage, ivec3(i0i, j1i, k1i)).x))) + \
            s1 * (t0 * (u0 * imageLoad(inImage, ivec3(i1i, j0i, k0i)).x + \
                        u1 * imageLoad(inImage, ivec3(i1i, j0i, k1i)).x) + \
                 (t1 * (u0 * imageLoad(inImage, ivec3(i1i, j1i, k0i)).x + \
                        u1 * imageLoad(inImage, ivec3(i1i, j1i, k1i)).x))); \
        imageStore(outImage, id, vec4(value)); \
    } \
    while (false) \

#endif
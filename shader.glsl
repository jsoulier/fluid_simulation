#ifndef SHADER_GLSL
#define SHADER_GLSL

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

/* TODO: copied from Mike Ash. refactor */
#define ADVECT(id, outImage, inImage, inVelocityX, inVelocityY, inVelocityZ, deltaTime, size) \
    do \
    { \
        float N = size.x; \
        /* Jaan: scale for the velocity */ \
        float dtx = deltaTime * (N - 2); \
        float dty = deltaTime * (N - 2); \
        float dtz = deltaTime * (N - 2); \
        /* Jaan: position delta for the current velocity */ \
        float tmp1 = dtx * texelFetch(inVelocityX, id, 0).x; \
        float tmp2 = dty * texelFetch(inVelocityY, id, 0).x; \
        float tmp3 = dtz * texelFetch(inVelocityZ, id, 0).x; \
        /* Jaan: previous position according to current position and velocity */ \
        float x = id.x - tmp1; \
        float y = id.y - tmp2; \
        float z = id.z - tmp3; \
        /* TODO: what the fuck? without N -= 2, a bunch of shit breaks */ \
        N -= 2; \
        /* TODO: shouldn't it be (N - 1) instead? is that why the previous thing is required? */ \
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
        /* Jaan: just a simple weighted average (i think) */ \
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
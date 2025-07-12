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

#endif
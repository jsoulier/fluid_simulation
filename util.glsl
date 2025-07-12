#ifndef UTIL_GLSL
#define UTIL_GLSL

const ivec3 Offsets[27] = ivec3[]
(
    ivec3(0, 0, 0), ivec3(0, 0, 1), ivec3(0, 0, 2),
    ivec3(0, 1, 0), ivec3(0, 1, 1), ivec3(0, 1, 2),
    ivec3(0, 2, 0), ivec3(0, 2, 1), ivec3(0, 2, 2),
    ivec3(1, 0, 0), ivec3(1, 0, 1), ivec3(1, 0, 2),
    ivec3(1, 1, 0), ivec3(1, 1, 1), ivec3(1, 1, 2),
    ivec3(1, 2, 0), ivec3(1, 2, 1), ivec3(1, 2, 2),
    ivec3(2, 0, 0), ivec3(2, 0, 1), ivec3(2, 0, 2),
    ivec3(2, 1, 0), ivec3(2, 1, 1), ivec3(2, 1, 2),
    ivec3(2, 2, 0), ivec3(2, 2, 1), ivec3(2, 2, 2)
);

/* TODO: I need to stop using GLSL */
#define LIN_SOLVE()

#endif
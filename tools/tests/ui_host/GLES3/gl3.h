#pragma once
// Compile-only GL declarations for the headless menu test (no GPU calls).
using GLuint = unsigned int;
constexpr unsigned GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_LINEAR = 0x2601,
    GL_CLAMP_TO_EDGE = 0x812F, GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401;
void glGenTextures(int, GLuint*);
void glBindTexture(unsigned, GLuint);
void glTexParameteri(unsigned, unsigned, int);
void glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);

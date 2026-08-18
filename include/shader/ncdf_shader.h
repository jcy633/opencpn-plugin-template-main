#ifndef NCDF_SHADER_H
#define NCDF_SHADER_H

// Windows.h must precede GL/gl.h for APIENTRY macro
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// Capability detection
bool ncdf_shader_supported();
bool ncdf_shader_initialized();

// Lifecycle
bool ncdf_shader_init();
void ncdf_shader_cleanup();

// Program access
unsigned int ncdf_shader_get_grid_program();

// Uniform locations (cached at init time)
struct NcdfShaderUniforms {
    int dataTex;   // sampler2D: normalized data texture (R=value, A=validity)
    int colorLUT;  // sampler2D: 1x256 color lookup texture
    int slopeTex;  // sampler2D: slope source texture (vorticity for current, same format)
    int texSize;   // vec2: texture dimensions for bicubic sampling
    int mode;      // int: 0=bilinear, 1=bicubic, 2=monotone bicubic
    int sCurve;    // int: 0=off, 1=on (S-curve color mapping)
    int slope;     // int: 0=off, 1=on (slope shading)
    int dataMin;   // float: data range min
    int dataMax;   // float: data range max
    int slopeMode; // int: 0=continuous Sobel, 1=SST segments
    int vectorMode; // int: 0=scalar (R channel), 1=vector (R=u, G=v, compute speed)
};
NcdfShaderUniforms ncdf_shader_get_uniforms();

// Runtime GL extension function wrappers
void ncdf_shader_use_program(unsigned int prog);
void ncdf_shader_uniform_1i(int loc, int val);
void ncdf_shader_uniform_1f(int loc, float val);
void ncdf_shader_uniform_2f(int loc, float x, float y);
void ncdf_shader_active_texture(unsigned int tex);
void ncdf_shader_enable_attrib(unsigned int idx);
void ncdf_shader_disable_attrib(unsigned int idx);
void ncdf_shader_attrib_pointer(unsigned int idx, int size, int stride, const void* ptr);

#endif // NCDF_SHADER_H

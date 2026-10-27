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
    int dataTex;      // sampler2D: R8 data texture (R=normalized coeff, A=validity)
    int physicalTex;  // sampler2D: physical values for bilinear fallback
    int colorLUT;     // sampler2D: 1x256 color lookup texture
    int texSize;      // vec2: texture dimensions for B-spline sampling
    int vectorMode;   // int: 0=scalar, 1=vector
    int dataMin;      // float: coefMin for U component (or scalar)
    int dataMax;      // float: coefMax for U component (or scalar)
    int dataMinV;     // float: coefMin for V component (vector only)
    int dataMaxV;     // float: coefMax for V component (vector only)
    int lutMin;       // float: dMin (data display range min for LUT)
    int lutMax;       // float: dMax (data display range max for LUT)
    int physMin;      // float: physical value min for U (or scalar)
    int physMax;      // float: physical value max for U (or scalar)
    int physMinV;     // float: physical value min for V (vector only)
    int physMaxV;     // float: physical value max for V (vector only)
    int landMode;     // int: 1 = draw GSHHG land, 0 = hide it (follows plugin panel)
    int landMaskTex;  // sampler2D: GSHHG land mask, coarse layer (r>0.5 = land)
    int landMaskTex2; // sampler2D: GSHHG land mask, fine layer
    int dataGeo;      // vec4: (kU, cU, kV, cV) with lon = kU*u + cU, lat = kV*v + cV
    int landGeoC;     // vec4: coarse mask UV = (aL*lon+bL, aT*lat+bT)
    int landGeoF;     // vec4: fine mask UV, same form
    int dbgFill;      // float: >0.5 -> paint the gap-fill region solid red
};
NcdfShaderUniforms ncdf_shader_get_uniforms();

// Runtime GL extension function wrappers
void ncdf_shader_use_program(unsigned int prog);
void ncdf_shader_uniform_1i(int loc, int val);
void ncdf_shader_uniform_1f(int loc, float val);
void ncdf_shader_uniform_2f(int loc, float x, float y);
void ncdf_shader_uniform_4f(int loc, float x, float y, float z, float w);
void ncdf_shader_active_texture(unsigned int tex);
void ncdf_shader_enable_attrib(unsigned int idx);
void ncdf_shader_disable_attrib(unsigned int idx);
void ncdf_shader_attrib_pointer(unsigned int idx, int size, int stride, const void* ptr);

#endif // NCDF_SHADER_H

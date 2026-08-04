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
    int dataTex;        // sampler2D: RGBA color-mapped texture
    int texSize;        // vec2: texture dimensions
    int numStops;       // int: color stop count
    int colorStops[16]; // vec4[16]: [value, R, G, B] per stop
    int sharpness;      // float: sharpening strength
    int enableAA;       // bool: color band anti-aliasing
};
NcdfShaderUniforms ncdf_shader_get_uniforms();

// Data texture upload (raw float data for future bicubic upgrade)
unsigned int ncdf_shader_create_data_texture(const float* data, int width, int height);
void ncdf_shader_delete_data_texture(unsigned int texId);

// Color legend (fixed pipeline, no shader dependency)
void ncdf_shader_draw_legend(int screenW, int screenH,
                             const float stops[][4], int numStops,
                             const char* unit, bool isTemperature);

// Runtime GL extension function wrappers (for use in ncdftexture.cpp etc.)
void ncdf_shader_use_program(unsigned int prog);
void ncdf_shader_uniform_1i(int loc, int val);
void ncdf_shader_uniform_1f(int loc, float val);
void ncdf_shader_uniform_2f(int loc, float x, float y);
void ncdf_shader_active_texture(unsigned int tex);
void ncdf_shader_enable_attrib(unsigned int idx);
void ncdf_shader_disable_attrib(unsigned int idx);
void ncdf_shader_attrib_pointer(unsigned int idx, int size, int stride, const void* ptr);

#endif // NCDF_SHADER_H

#ifndef NCDF_ANIMATE_SHADER_H
#define NCDF_ANIMATE_SHADER_H

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

bool ncdf_animate_shader_init();
void ncdf_animate_shader_cleanup();
bool ncdf_animate_shader_initialized();

unsigned int ncdf_animate_get_update_program();
unsigned int ncdf_animate_get_advect_program();
unsigned int ncdf_animate_get_display_program();

struct AnimUniforms {
    int uTex0, uTex1, uTex2;
    int texSize, scalarSize;
    int frame, period;
    int uColorLUT;
};
AnimUniforms ncdf_animate_get_update_uniforms();
AnimUniforms ncdf_animate_get_advect_uniforms();
AnimUniforms ncdf_animate_get_display_uniforms();

unsigned int ncdf_animate_create_fbo();
void ncdf_animate_delete_fbo(unsigned int fbo);
void ncdf_animate_bind_fbo(unsigned int fbo, unsigned int colorTex);
void ncdf_animate_unbind_fbo();
unsigned int ncdf_animate_create_float_texture(int w, int h);
unsigned int ncdf_animate_create_rgba_texture(int w, int h);

void ncdf_animate_use_program(unsigned int prog);
void ncdf_animate_uniform_1i(int loc, int val);
void ncdf_animate_uniform_1f(int loc, float val);
void ncdf_animate_uniform_2f(int loc, float x, float y);
void ncdf_animate_active_texture(unsigned int tex);
void ncdf_animate_draw_quad();

#endif

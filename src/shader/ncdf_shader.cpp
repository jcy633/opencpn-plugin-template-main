//===================================================================
// ncdf_shader.cpp — Shader framework with runtime GL extension loading
// High cohesion: all shader logic self-contained
// Low coupling: exposes only init/cleanup/use/uniform API
//===================================================================

#include "shader/ncdf_shader.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// GL shader constants (not in basic GL/gl.h)
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// Load GL extension function at runtime
static void* load_gl(const char* name) {
#ifdef _WIN32
    void* p = (void*)wglGetProcAddress(name);
    if (!p) {
        static HMODULE hGL = LoadLibraryA("opengl32.dll");
        if (hGL) p = (void*)GetProcAddress(hGL, name);
    }
    return p;
#else
    return NULL;
#endif
}

// Function pointer types using basic C types to avoid GL header conflicts
typedef unsigned int (*fp_create_t)(void);
typedef unsigned int (*fp_create_shader_t)(unsigned int);
typedef void (*fp_delete_t)(unsigned int);
typedef void (*fp_attach_t)(unsigned int, unsigned int);
typedef void (*fp_link_t)(unsigned int);
typedef void (*fp_use_t)(unsigned int);
typedef void (*fp_compile_t)(unsigned int);
typedef int  (*fp_uniform_loc_t)(unsigned int, const char*);
typedef void (*fp_uniform_1i_t)(int, int);
typedef void (*fp_uniform_1f_t)(int, float);
typedef void (*fp_uniform_2f_t)(int, float, float);
typedef void (*fp_bind_attr_t)(unsigned int, unsigned int, const char*);
typedef void (*fp_active_tex_t)(unsigned int);
typedef void (*fp_enable_va_t)(unsigned int);
typedef void (*fp_disable_va_t)(unsigned int);
typedef void (*fp_va_pointer_t)(unsigned int, int, unsigned int, unsigned char, int, const void*);

// Loaded function pointers
static fp_create_t fpCreateProgram = NULL;
static fp_create_shader_t fpCreateShader = NULL;
static fp_delete_t fpDeleteShader = NULL;
static fp_delete_t fpDeleteProgram = NULL;
static fp_attach_t fpAttachShader = NULL;
static fp_link_t fpLinkProgram = NULL;
static fp_use_t fpUseProgram = NULL;
static fp_compile_t fpCompileShader = NULL;
static fp_uniform_loc_t fpGetUniformLocation = NULL;
static fp_uniform_1i_t fpUniform1i = NULL;
static fp_uniform_1f_t fpUniform1f = NULL;
static fp_uniform_2f_t fpUniform2f = NULL;
static fp_bind_attr_t fpBindAttribLocation = NULL;
static fp_active_tex_t fpActiveTexture = NULL;
static fp_enable_va_t fpEnableVertexAttribArray = NULL;
static fp_disable_va_t fpDisableVertexAttribArray = NULL;
static fp_va_pointer_t fpVertexAttribPointer = NULL;

// glShaderSource/glGetShaderiv/glGetShaderInfoLog have complex signatures
// Load as void* and cast when calling
static void* fpShaderSource = NULL;
static void* fpGetShaderiv = NULL;
static void* fpGetShaderInfoLog = NULL;
static void* fpGetProgramiv = NULL;
static void* fpGetProgramInfoLog = NULL;

static bool load_gl_extensions() {
    fpCreateProgram = (fp_create_t)load_gl("glCreateProgram");
    fpCreateShader = (fp_create_shader_t)load_gl("glCreateShader");
    fpDeleteShader = (fp_delete_t)load_gl("glDeleteShader");
    fpDeleteProgram = (fp_delete_t)load_gl("glDeleteProgram");
    fpAttachShader = (fp_attach_t)load_gl("glAttachShader");
    fpLinkProgram = (fp_link_t)load_gl("glLinkProgram");
    fpUseProgram = (fp_use_t)load_gl("glUseProgram");
    fpCompileShader = (fp_compile_t)load_gl("glCompileShader");
    fpShaderSource = load_gl("glShaderSource");
    fpGetShaderiv = load_gl("glGetShaderiv");
    fpGetShaderInfoLog = load_gl("glGetShaderInfoLog");
    fpGetProgramiv = load_gl("glGetProgramiv");
    fpGetProgramInfoLog = load_gl("glGetProgramInfoLog");
    fpGetUniformLocation = (fp_uniform_loc_t)load_gl("glGetUniformLocation");
    fpUniform1i = (fp_uniform_1i_t)load_gl("glUniform1i");
    fpUniform1f = (fp_uniform_1f_t)load_gl("glUniform1f");
    fpUniform2f = (fp_uniform_2f_t)load_gl("glUniform2f");
    fpBindAttribLocation = (fp_bind_attr_t)load_gl("glBindAttribLocation");
    fpActiveTexture = (fp_active_tex_t)load_gl("glActiveTexture");
    fpEnableVertexAttribArray = (fp_enable_va_t)load_gl("glEnableVertexAttribArray");
    fpDisableVertexAttribArray = (fp_disable_va_t)load_gl("glDisableVertexAttribArray");
    fpVertexAttribPointer = (fp_va_pointer_t)load_gl("glVertexAttribPointer");
    return (fpCreateProgram && fpCreateShader && fpCompileShader && fpShaderSource && fpLinkProgram && fpUseProgram);
}

// GLSL 1.20 vertex shader
static const char* s_vert =
    "#version 120\n"
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// GLSL 1.20 fragment shader: sharpening + missing value discard
// Avoids GLSL 1.20 `continue` (incompatible with some old drivers)
static const char* s_frag =
    "#version 120\n"
    "uniform sampler2D dataTex;\n"
    "uniform vec2 texSize;\n"
    "uniform float sharpness;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vec4 center = texture2D(dataTex, vUV);\n"
    "    if (center.a < 0.01) discard;\n"
    "    vec2 ts = 1.0 / texSize;\n"
    "    vec4 left  = texture2D(dataTex, vUV - vec2(ts.x, 0.0));\n"
    "    vec4 right = texture2D(dataTex, vUV + vec2(ts.x, 0.0));\n"
    "    vec4 up    = texture2D(dataTex, vUV - vec2(0.0, ts.y));\n"
    "    vec4 down  = texture2D(dataTex, vUV + vec2(0.0, ts.y));\n"
    "    float wL = step(0.01, left.a);\n"
    "    float wR = step(0.01, right.a);\n"
    "    float wU = step(0.01, up.a);\n"
    "    float wD = step(0.01, down.a);\n"
    "    float wSum = wL + wR + wU + wD;\n"
    "    vec3 blur = vec3(0.0);\n"
    "    if (wSum > 0.0) {\n"
    "        blur = (left.rgb * wL + right.rgb * wR + up.rgb * wU + down.rgb * wD) / wSum;\n"
    "    }\n"
    "    vec3 sharpened = center.rgb + sharpness * (center.rgb - blur);\n"
    "    gl_FragColor = vec4(clamp(sharpened, 0.0, 1.0), center.a);\n"
    "}\n";

// Compile a shader using raw function pointers
static unsigned int compile_shader_gl(unsigned int type, const char* src) {
    unsigned int s = fpCreateShader(type);
    if (!s) return 0;
    // glShaderSource(shader, 1, &source, NULL)
    typedef void (*ss_fn)(unsigned int, int, const char**, const int*);
    ((ss_fn)fpShaderSource)(s, 1, &src, NULL);
    fpCompileShader(s);
    // glGetShaderiv(shader, GL_COMPILE_STATUS, &status)
    typedef void (*gsi_fn)(unsigned int, unsigned int, int*);
    int st;
    ((gsi_fn)fpGetShaderiv)(s, 0x8B81 /*GL_COMPILE_STATUS*/, &st);
    if (!st) {
        typedef void (*gil_fn)(unsigned int, int, int*, char*);
        char log[512];
        ((gil_fn)fpGetShaderInfoLog)(s, 512, NULL, log);
        fprintf(stderr, "[shader] compile: %s\n", log);
        fpDeleteShader(s);
        return 0;
    }
    return s;
}

static unsigned int link_program_gl(unsigned int vs, unsigned int fs) {
    unsigned int p = fpCreateProgram();
    if (!p) return 0;
    fpAttachShader(p, vs);
    fpAttachShader(p, fs);
    if (fpBindAttribLocation) {
        fpBindAttribLocation(p, 0, "aPos");
        fpBindAttribLocation(p, 1, "aUV");
    }
    fpLinkProgram(p);
    typedef void (*gpi_fn)(unsigned int, unsigned int, int*);
    int st;
    ((gpi_fn)fpGetProgramiv)(p, 0x8B82 /*GL_LINK_STATUS*/, &st);
    if (!st) {
        typedef void (*gpil_fn)(unsigned int, int, int*, char*);
        char log[512];
        ((gpil_fn)fpGetProgramInfoLog)(p, 512, NULL, log);
        fprintf(stderr, "[shader] link: %s\n", log);
        fpDeleteProgram(p);
        return 0;
    }
    return p;
}

// Module state
static bool s_supported = false;
static bool s_initialized = false;
static unsigned int s_program = 0;
static unsigned int s_vs = 0;
static unsigned int s_fs = 0;
static NcdfShaderUniforms s_uniforms;

bool ncdf_shader_supported() {
    const char* v = (const char*)glGetString(GL_VERSION);
    if (!v) return false;
    int mj = 0, mn = 0;
    sscanf(v, "%d.%d", &mj, &mn);
    return mj >= 2;
}

bool ncdf_shader_initialized() { return s_initialized; }

bool ncdf_shader_init() {
    if (s_initialized) return true;
    s_supported = ncdf_shader_supported();
    if (!s_supported) { fprintf(stderr, "[shader] GL < 2.0, fallback\n"); return false; }
    if (!load_gl_extensions()) { fprintf(stderr, "[shader] ext load failed\n"); s_supported = false; return false; }

    s_vs = compile_shader_gl(0x8B31 /*GL_VERTEX_SHADER*/, s_vert);
    if (!s_vs) { s_supported = false; return false; }
    s_fs = compile_shader_gl(0x8B30 /*GL_FRAGMENT_SHADER*/, s_frag);
    if (!s_fs) { fpDeleteShader(s_vs); s_vs = 0; s_supported = false; return false; }
    s_program = link_program_gl(s_vs, s_fs);
    if (!s_program) { fpDeleteShader(s_vs); s_vs = 0; fpDeleteShader(s_fs); s_fs = 0; s_supported = false; return false; }

    // Cache uniform locations
    memset(&s_uniforms, 0xFF, sizeof(s_uniforms));
    s_uniforms.dataTex = fpGetUniformLocation(s_program, "dataTex");
    s_uniforms.texSize = fpGetUniformLocation(s_program, "texSize");
    s_uniforms.numStops = fpGetUniformLocation(s_program, "numStops");
    s_uniforms.sharpness = fpGetUniformLocation(s_program, "sharpness");
    s_uniforms.enableAA = fpGetUniformLocation(s_program, "enableAA");
    for (int i = 0; i < 16; i++) {
        char name[32];
        snprintf(name, sizeof(name), "colorStops[%d]", i);
        s_uniforms.colorStops[i] = fpGetUniformLocation(s_program, name);
    }

    s_initialized = true;
    fprintf(stderr, "[shader] init OK program=%u\n", s_program);
    return true;
}

void ncdf_shader_cleanup() {
    if (s_program && fpDeleteProgram) { fpDeleteProgram(s_program); s_program = 0; }
    if (s_vs && fpDeleteShader) { fpDeleteShader(s_vs); s_vs = 0; }
    if (s_fs && fpDeleteShader) { fpDeleteShader(s_fs); s_fs = 0; }
    s_initialized = false;
}

unsigned int ncdf_shader_get_grid_program() { return s_program; }
NcdfShaderUniforms ncdf_shader_get_uniforms() { return s_uniforms; }

// Runtime GL function wrappers for external use
void ncdf_shader_use_program(unsigned int p) { if (fpUseProgram) fpUseProgram(p); }
void ncdf_shader_uniform_1i(int loc, int v) { if (fpUniform1i && loc >= 0) fpUniform1i(loc, v); }
void ncdf_shader_uniform_1f(int loc, float v) { if (fpUniform1f && loc >= 0) fpUniform1f(loc, v); }
void ncdf_shader_uniform_2f(int loc, float x, float y) { if (fpUniform2f && loc >= 0) fpUniform2f(loc, x, y); }
void ncdf_shader_active_texture(unsigned int t) { if (fpActiveTexture) fpActiveTexture(t); }
void ncdf_shader_enable_attrib(unsigned int i) { if (fpEnableVertexAttribArray) fpEnableVertexAttribArray(i); }
void ncdf_shader_disable_attrib(unsigned int i) { if (fpDisableVertexAttribArray) fpDisableVertexAttribArray(i); }
void ncdf_shader_attrib_pointer(unsigned int i, int sz, int stride, const void* p) {
    if (fpVertexAttribPointer) fpVertexAttribPointer(i, sz, 0x1406 /*GL_FLOAT*/, 0, stride, p);
}

//===================================================================
// Data texture upload
//===================================================================
unsigned int ncdf_shader_create_data_texture(const float* data, int w, int h) {
    if (!data || w <= 0 || h <= 0) return 0;
    unsigned char* px = new unsigned char[w * h * 4];
    for (int i = 0; i < w * h; i++) {
        float v = data[i];
        if (v == -999999999.0f || v != v) {
            px[i*4] = 0; px[i*4+1] = 0; px[i*4+2] = 0; px[i*4+3] = 0;
        } else {
            unsigned char packed = (unsigned char)(fminf(fmaxf(v / 50.0f, 0.0f), 1.0f) * 255.0f);
            px[i*4] = packed; px[i*4+1] = packed; px[i*4+2] = packed; px[i*4+3] = 255;
        }
    }
    unsigned int t;
    glGenTextures(1, (GLuint*)&t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    delete[] px;
    return t;
}

void ncdf_shader_delete_data_texture(unsigned int t) {
    if (t) glDeleteTextures(1, (GLuint*)&t);
}

//===================================================================
// Color legend (fixed pipeline)
//===================================================================
void ncdf_shader_draw_legend(int sw, int sh, const float stops[][4], int ns, const char* unit, bool isTemp) {
    if (!stops || ns <= 0) return;
    int bw = 30, lw = 60, m = 20, eh = 22;
    int th = ns * eh + 40;
    int x0 = sw - m - bw - lw;
    int y0 = sh - m - th;
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_QUADS);
    glVertex2i(x0 - 5, y0 - 5);
    glVertex2i(x0 + bw + lw + 5, y0 - 5);
    glVertex2i(x0 + bw + lw + 5, y0 + th + 5);
    glVertex2i(x0 - 5, y0 + th + 5);
    glEnd();
    for (int i = 0; i < ns; i++) {
        int y = y0 + i * eh;
        unsigned char r = (unsigned char)(stops[i][1] * 255.0f);
        unsigned char g = (unsigned char)(stops[i][2] * 255.0f);
        unsigned char b = (unsigned char)(stops[i][3] * 255.0f);
        glColor4ub(r, g, b, 255);
        glBegin(GL_QUADS);
        glVertex2i(x0, y);
        glVertex2i(x0 + bw, y);
        glVertex2i(x0 + bw, y + eh - 2);
        glVertex2i(x0, y + eh - 2);
        glEnd();
    }
    glDisable(GL_BLEND);
}

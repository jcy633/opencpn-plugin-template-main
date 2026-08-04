//===================================================================
// ncdf_shader.cpp — Shader framework with GLEW-style GL loading
// Uses glext.h typedefs for correct function pointer types
//===================================================================

#include "shader/ncdf_shader.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// GL types not in gl.h (defined in glext.h)
#ifndef GLchar
typedef char GLchar;
#endif
#ifndef GLvoid
typedef void GLvoid;
#endif

// GL shader constants (not in basic GL/gl.h)
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif

// Load GL function at runtime
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

// Function pointer types matching OpenGL 2.0 signatures exactly
// Using GLuint/GLint/GLenum/GLsizei/GLfloat/GLchar from GL/gl.h
typedef GLuint (APIENTRY *PFN_CREATEPROGRAM)(void);
typedef GLuint (APIENTRY *PFN_CREATESHADER)(GLenum);
typedef void (APIENTRY *PFN_DELETESHADER)(GLuint);
typedef void (APIENTRY *PFN_DELETEPROGRAM)(GLuint);
typedef void (APIENTRY *PFN_ATTACHSHADER)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LINKPROGRAM)(GLuint);
typedef void (APIENTRY *PFN_USEPROGRAM)(GLuint);
typedef void (APIENTRY *PFN_COMPILESHADER)(GLuint);
typedef void (APIENTRY *PFN_SHADERSOURCE)(GLuint, GLsizei, const GLchar**, const GLint*);
typedef void (APIENTRY *PFN_GETSHADERIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_GETSHADERINFOLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY *PFN_GETPROGRAMIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_GETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLint (APIENTRY *PFN_GETUNIFORMLOCATION)(GLuint, const GLchar*);
typedef void (APIENTRY *PFN_UNIFORM1I)(GLint, GLint);
typedef void (APIENTRY *PFN_UNIFORM1F)(GLint, GLfloat);
typedef void (APIENTRY *PFN_UNIFORM2F)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_BINDATTRIBLOCATION)(GLuint, GLuint, const GLchar*);
typedef void (APIENTRY *PFN_ACTIVETEXTURE)(GLenum);
typedef void (APIENTRY *PFN_ENABLEVERTEXATTRIBARRAY)(GLuint);
typedef void (APIENTRY *PFN_DISABLEVERTEXATTRIBARRAY)(GLuint);
typedef void (APIENTRY *PFN_VERTEXATTRIBPOINTER)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);

// Function pointer variables
static PFN_CREATEPROGRAM fpCreateProgram = NULL;
static PFN_CREATESHADER fpCreateShader = NULL;
static PFN_DELETESHADER fpDeleteShader = NULL;
static PFN_DELETEPROGRAM fpDeleteProgram = NULL;
static PFN_ATTACHSHADER fpAttachShader = NULL;
static PFN_LINKPROGRAM fpLinkProgram = NULL;
static PFN_USEPROGRAM fpUseProgram = NULL;
static PFN_COMPILESHADER fpCompileShader = NULL;
static PFN_SHADERSOURCE fpShaderSource = NULL;
static PFN_GETSHADERIV fpGetShaderiv = NULL;
static PFN_GETSHADERINFOLOG fpGetShaderInfoLog = NULL;
static PFN_GETPROGRAMIV fpGetProgramiv = NULL;
static PFN_GETPROGRAMINFOLOG fpGetProgramInfoLog = NULL;
static PFN_GETUNIFORMLOCATION fpGetUniformLocation = NULL;
static PFN_UNIFORM1I fpUniform1i = NULL;
static PFN_UNIFORM1F fpUniform1f = NULL;
static PFN_UNIFORM2F fpUniform2f = NULL;
static PFN_BINDATTRIBLOCATION fpBindAttribLocation = NULL;
static PFN_ACTIVETEXTURE fpActiveTexture = NULL;
static PFN_ENABLEVERTEXATTRIBARRAY fpEnableVertexAttribArray = NULL;
static PFN_DISABLEVERTEXATTRIBARRAY fpDisableVertexAttribArray = NULL;
static PFN_VERTEXATTRIBPOINTER fpVertexAttribPointer = NULL;

static bool load_gl_extensions() {
    fpCreateProgram = (PFN_CREATEPROGRAM)load_gl("glCreateProgram");
    fpCreateShader = (PFN_CREATESHADER)load_gl("glCreateShader");
    fpDeleteShader = (PFN_DELETESHADER)load_gl("glDeleteShader");
    fpDeleteProgram = (PFN_DELETEPROGRAM)load_gl("glDeleteProgram");
    fpAttachShader = (PFN_ATTACHSHADER)load_gl("glAttachShader");
    fpLinkProgram = (PFN_LINKPROGRAM)load_gl("glLinkProgram");
    fpUseProgram = (PFN_USEPROGRAM)load_gl("glUseProgram");
    fpCompileShader = (PFN_COMPILESHADER)load_gl("glCompileShader");
    fpShaderSource = (PFN_SHADERSOURCE)load_gl("glShaderSource");
    fpGetShaderiv = (PFN_GETSHADERIV)load_gl("glGetShaderiv");
    fpGetShaderInfoLog = (PFN_GETSHADERINFOLOG)load_gl("glGetShaderInfoLog");
    fpGetProgramiv = (PFN_GETPROGRAMIV)load_gl("glGetProgramiv");
    fpGetProgramInfoLog = (PFN_GETPROGRAMINFOLOG)load_gl("glGetProgramInfoLog");
    fpGetUniformLocation = (PFN_GETUNIFORMLOCATION)load_gl("glGetUniformLocation");
    fpUniform1i = (PFN_UNIFORM1I)load_gl("glUniform1i");
    fpUniform1f = (PFN_UNIFORM1F)load_gl("glUniform1f");
    fpUniform2f = (PFN_UNIFORM2F)load_gl("glUniform2f");
    fpBindAttribLocation = (PFN_BINDATTRIBLOCATION)load_gl("glBindAttribLocation");
    fpActiveTexture = (PFN_ACTIVETEXTURE)load_gl("glActiveTexture");
    fpEnableVertexAttribArray = (PFN_ENABLEVERTEXATTRIBARRAY)load_gl("glEnableVertexAttribArray");
    fpDisableVertexAttribArray = (PFN_DISABLEVERTEXATTRIBARRAY)load_gl("glDisableVertexAttribArray");
    fpVertexAttribPointer = (PFN_VERTEXATTRIBPOINTER)load_gl("glVertexAttribPointer");

    // All critical functions must be loaded
    return (fpCreateProgram && fpCreateShader && fpCompileShader && fpShaderSource &&
            fpLinkProgram && fpUseProgram && fpGetShaderiv && fpGetProgramiv &&
            fpGetUniformLocation && fpUniform1i && fpEnableVertexAttribArray &&
            fpDisableVertexAttribArray && fpVertexAttribPointer);
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

// GLSL 1.20 fragment shader: bicubic LA texture + color lookup
static const char* s_frag =
    "#version 120\n"
    "uniform sampler2D dataTex;\n"
    "uniform sampler1D colorLUT;\n"
    "uniform vec2 texSize;\n"
    "varying vec2 vUV;\n"
    "\n"
    "vec4 cubic(float t) {\n"
    "    float t2=t*t, t3=t2*t;\n"
    "    vec4 w;\n"
    "    w.x = -t3+3.0*t2-3.0*t+1.0;\n"
    "    w.y = 3.0*t3-6.0*t2+4.0;\n"
    "    w.z = -3.0*t3+3.0*t2+3.0*t+1.0;\n"
    "    w.w = t3;\n"
    "    return w / 6.0;\n"
    "}\n"
    "\n"
    "float bicubicLum(sampler2D tex, vec2 uv, vec2 sz) {\n"
    "    vec2 ts = 1.0 / sz;\n"
    "    vec2 tc = uv * sz - 0.5;\n"
    "    vec2 f = fract(tc);\n"
    "    vec4 wx = cubic(f.x); vec4 wy = cubic(f.y);\n"
    "    wx /= dot(wx, vec4(1.0)); wy /= dot(wy, vec4(1.0));\n"
    "    float val = 0.0, wSum = 0.0;\n"
    "    for (int j = -1; j <= 2; j++) {\n"
    "        for (int i = -1; i <= 2; i++) {\n"
    "            vec2 suv = (tc + vec2(float(i), float(j)) + 0.5) * ts;\n"
    "            vec4 s = texture2D(tex, suv);\n"
    "            if (s.a > 0.01) {\n"
    "                float w = wx[i+1] * wy[j+1];\n"
    "                val += s.r * w; wSum += w;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return (wSum > 0.0) ? val / wSum : -1.0;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 center = texture2D(dataTex, vUV);\n"
    "    if (center.a < 0.01) discard;  // Land/missing data\n"
    "    float sampled = bicubicLum(dataTex, vUV, texSize);\n"
    "    if (sampled < 0.0) discard;  // All neighbors missing\n"
    "    vec3 color = texture1D(colorLUT, sampled).rgb;\n"
    "    gl_FragColor = vec4(color, 1.0);\n"
    "}\n";

// Compile a shader
static GLuint compile_shader_gl(GLenum type, const char* src) {
    GLuint s = fpCreateShader(type);
    if (!s) return 0;
    fpShaderSource(s, 1, &src, NULL);
    fpCompileShader(s);
    GLint st;
    fpGetShaderiv(s, GL_COMPILE_STATUS, &st);
    if (!st) {
        char log[512];
        fpGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[shader] compile error: %s\n", log);
        fpDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program_gl(GLuint vs, GLuint fs) {
    GLuint p = fpCreateProgram();
    if (!p) return 0;
    fpAttachShader(p, vs);
    fpAttachShader(p, fs);
    fpBindAttribLocation(p, 0, "aPos");
    fpBindAttribLocation(p, 1, "aUV");
    fpLinkProgram(p);
    GLint st;
    fpGetProgramiv(p, GL_LINK_STATUS, &st);
    if (!st) {
        char log[512];
        fpGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[shader] link error: %s\n", log);
        fpDeleteProgram(p);
        return 0;
    }
    return p;
}

// Module state
static bool s_supported = false;
static bool s_initialized = false;
static GLuint s_program = 0, s_vs = 0, s_fs = 0;
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
    static bool s_attempted = false;
    if (s_attempted) return false;
    s_attempted = true;

    s_supported = ncdf_shader_supported();
    if (!s_supported) { fprintf(stderr, "[shader] GL < 2.0, fallback\n"); return false; }

    fprintf(stderr, "[shader] loading GL extensions...\n");
    if (!load_gl_extensions()) { fprintf(stderr, "[shader] ext load failed\n"); s_supported = false; return false; }

    fprintf(stderr, "[shader] compiling shaders...\n");
    s_vs = compile_shader_gl(GL_VERTEX_SHADER, s_vert);
    if (!s_vs) { s_supported = false; return false; }
    s_fs = compile_shader_gl(GL_FRAGMENT_SHADER, s_frag);
    if (!s_fs) { fpDeleteShader(s_vs); s_vs = 0; s_supported = false; return false; }

    fprintf(stderr, "[shader] linking program...\n");
    s_program = link_program_gl(s_vs, s_fs);
    if (!s_program) { fpDeleteShader(s_vs); s_vs = 0; fpDeleteShader(s_fs); s_fs = 0; s_supported = false; return false; }

    // Cache uniform locations
    memset(&s_uniforms, 0xFF, sizeof(s_uniforms));
    s_uniforms.dataTex = fpGetUniformLocation(s_program, "dataTex");
    s_uniforms.colorLUT = fpGetUniformLocation(s_program, "colorLUT");
    s_uniforms.texSize = fpGetUniformLocation(s_program, "texSize");
    s_uniforms.numStops = fpGetUniformLocation(s_program, "numStops");
    s_uniforms.sharpness = fpGetUniformLocation(s_program, "sharpness");
    s_uniforms.enableAA = fpGetUniformLocation(s_program, "enableAA");
    s_uniforms.dataMin = fpGetUniformLocation(s_program, "dataMin");
    s_uniforms.dataMax = fpGetUniformLocation(s_program, "dataMax");
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

// Runtime GL function wrappers
void ncdf_shader_use_program(unsigned int p) { if (fpUseProgram) fpUseProgram(p); }
void ncdf_shader_uniform_1i(int loc, int v) { if (fpUniform1i && loc >= 0) fpUniform1i(loc, v); }
void ncdf_shader_uniform_1f(int loc, float v) { if (fpUniform1f && loc >= 0) fpUniform1f(loc, v); }
void ncdf_shader_uniform_2f(int loc, float x, float y) { if (fpUniform2f && loc >= 0) fpUniform2f(loc, x, y); }
void ncdf_shader_active_texture(unsigned int t) { if (fpActiveTexture) fpActiveTexture(t); }
void ncdf_shader_enable_attrib(unsigned int i) { if (fpEnableVertexAttribArray) fpEnableVertexAttribArray(i); }
void ncdf_shader_disable_attrib(unsigned int i) { if (fpDisableVertexAttribArray) fpDisableVertexAttribArray(i); }
void ncdf_shader_attrib_pointer(unsigned int i, int sz, int stride, const void* p) {
    if (fpVertexAttribPointer) fpVertexAttribPointer(i, sz, GL_FLOAT, GL_FALSE, stride, p);
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
    GLuint t;
    glGenTextures(1, &t);
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
    if (t) glDeleteTextures(1, &t);
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

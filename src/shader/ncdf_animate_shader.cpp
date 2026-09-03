//===================================================================
// ncdf_animate_shader.cpp — Animation shader (GLSL 1.20)
// Patent-compliant: coordinate field update + scalar advect+blend
//===================================================================

#include "shader/ncdf_animate_shader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#ifndef GLchar
typedef char GLchar;
#endif
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
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

static void animLog(const char* fmt, ...) {
    FILE* f = fopen("C:\\ProgramData\\opencpn\\ncdf_debug.log", "a");
    if (!f) return;
    fprintf(f, "[anim-shader] ");
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fprintf(f, "\n"); fclose(f);
}

static void* load_gl(const char* name) {
#ifdef _WIN32
    void* p = (void*)wglGetProcAddress(name);
    if (!p) { static HMODULE h = LoadLibraryA("opengl32.dll"); if (h) p = (void*)GetProcAddress(h, name); }
    return p;
#else
    return NULL;
#endif
}

// Typedefs
typedef GLuint (APIENTRY *PFNCPROC)(void);
typedef GLuint (APIENTRY *PFNCST)(GLenum);
typedef void (APIENTRY *PFNVPROC)(GLuint);
typedef void (APIENTRY *PFNVPROC2)(GLuint, GLuint);
typedef void (APIENTRY *PFNVPROC_1I)(GLint, GLint);
typedef void (APIENTRY *PFNVPROC_1F)(GLint, GLfloat);
typedef void (APIENTRY *PFNVPROC_2F)(GLint, GLfloat, GLfloat);
typedef GLint (APIENTRY *PFNGETUNI)(GLuint, const GLchar*);
typedef void (APIENTRY *PFNSRC)(GLuint, GLsizei, const GLchar**, const GLint*);
typedef void (APIENTRY *PFNGETIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFNGETLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY *PFNBINDATTRIB)(GLuint, GLuint, const GLchar*);
typedef void (APIENTRY *PFNACTEX)(GLenum);
typedef void (APIENTRY *PFNENVA)(GLuint);
typedef void (APIENTRY *PFNDISA)(GLuint);
typedef void (APIENTRY *PFNVAP)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void (APIENTRY *PFNGENFB)(GLsizei, GLuint*);
typedef void (APIENTRY *PFNDELFB)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFNBINDFB)(GLenum, GLuint);
typedef void (APIENTRY *PFNFBTEX2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFNCHECKFB)(GLenum);

static PFNCPROC fpCreateProgram; static PFNCST fpCreateShader;
static PFNVPROC fpDeleteShader; static PFNVPROC fpDeleteProgram;
static PFNVPROC2 fpAttachShader; static PFNVPROC fpLinkProgram;
static PFNVPROC fpUseProgram; static PFNVPROC fpCompileShader;
static PFNSRC fpShaderSource; static PFNGETIV fpGetShaderiv;
static PFNGETLOG fpGetShaderInfoLog; static PFNGETIV fpGetProgramiv;
static PFNGETLOG fpGetProgramInfoLog; static PFNGETUNI fpGetUniformLocation;
static PFNVPROC_1I fpUniform1i; static PFNVPROC_1F fpUniform1f;
static PFNVPROC_2F fpUniform2f; static PFNBINDATTRIB fpBindAttribLocation;
static PFNACTEX fpActiveTexture; static PFNENVA fpEnableVertexAttribArray;
static PFNDISA fpDisableVertexAttribArray; static PFNVAP fpVertexAttribPointer;
static PFNGENFB fpGenFramebuffers; static PFNDELFB fpDeleteFramebuffers;
static PFNBINDFB fpBindFramebuffer; static PFNFBTEX2D fpFramebufferTexture2D;
static PFNCHECKFB fpCheckFramebufferStatus;

static bool load_gl_extensions() {
    fpCreateProgram=(PFNCPROC)load_gl("glCreateProgram");
    fpCreateShader=(PFNCST)load_gl("glCreateShader");
    fpDeleteShader=(PFNVPROC)load_gl("glDeleteShader");
    fpDeleteProgram=(PFNVPROC)load_gl("glDeleteProgram");
    fpAttachShader=(PFNVPROC2)load_gl("glAttachShader");
    fpLinkProgram=(PFNVPROC)load_gl("glLinkProgram");
    fpUseProgram=(PFNVPROC)load_gl("glUseProgram");
    fpCompileShader=(PFNVPROC)load_gl("glCompileShader");
    fpShaderSource=(PFNSRC)load_gl("glShaderSource");
    fpGetShaderiv=(PFNGETIV)load_gl("glGetShaderiv");
    fpGetShaderInfoLog=(PFNGETLOG)load_gl("glGetShaderInfoLog");
    fpGetProgramiv=(PFNGETIV)load_gl("glGetProgramiv");
    fpGetProgramInfoLog=(PFNGETLOG)load_gl("glGetProgramInfoLog");
    fpGetUniformLocation=(PFNGETUNI)load_gl("glGetUniformLocation");
    fpUniform1i=(PFNVPROC_1I)load_gl("glUniform1i");
    fpUniform1f=(PFNVPROC_1F)load_gl("glUniform1f");
    fpUniform2f=(PFNVPROC_2F)load_gl("glUniform2f");
    fpBindAttribLocation=(PFNBINDATTRIB)load_gl("glBindAttribLocation");
    fpActiveTexture=(PFNACTEX)load_gl("glActiveTexture");
    fpEnableVertexAttribArray=(PFNENVA)load_gl("glEnableVertexAttribArray");
    fpDisableVertexAttribArray=(PFNDISA)load_gl("glDisableVertexAttribArray");
    fpVertexAttribPointer=(PFNVAP)load_gl("glVertexAttribPointer");
    fpGenFramebuffers=(PFNGENFB)load_gl("glGenFramebuffers");
    fpDeleteFramebuffers=(PFNDELFB)load_gl("glDeleteFramebuffers");
    fpBindFramebuffer=(PFNBINDFB)load_gl("glBindFramebuffer");
    fpFramebufferTexture2D=(PFNFBTEX2D)load_gl("glFramebufferTexture2D");
    fpCheckFramebufferStatus=(PFNCHECKFB)load_gl("glCheckFramebufferStatus");
    return (fpCreateProgram && fpCreateShader && fpCompileShader && fpShaderSource &&
            fpLinkProgram && fpUseProgram && fpGetShaderiv && fpGetProgramiv &&
            fpGetUniformLocation && fpUniform1i &&
            fpEnableVertexAttribArray && fpDisableVertexAttribArray && fpVertexAttribPointer &&
            fpGenFramebuffers && fpBindFramebuffer && fpFramebufferTexture2D);
}

//===================================================================
// GLSL 1.20 Shaders
//===================================================================

static const char* s_vert =
    "#version 120\n"
    "attribute vec2 aPos;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aPos * 0.5 + 0.5;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// Pass 1&2: Coordinate field update with dual-channel encoding
// R+G = X (16-bit), B+A = Y (16-bit)
// Reads: coord field texture + displacement map texture
// Writes: updated coord field (via FBO ping-pong)
static const char* s_frag_update =
    "#version 120\n"
    "uniform sampler2D uCoord;\n"
    "uniform sampler2D uDisp;\n"
    "uniform vec2 texSize;\n"
    "uniform float maxDisp;\n"
    "uniform int forceIdentity;\n"
    "varying vec2 vUV;\n"
    "\n"
    "vec2 decodeCoord(vec4 c) {\n"
    "    float x = (c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0;\n"
    "    float y = (c.b * 255.0 * 256.0 + c.a * 255.0) / 65535.0;\n"
    "    return vec2(x, y);\n"
    "}\n"
    "\n"
    "vec4 encodeCoord(vec2 coord) {\n"
    "    float nx = clamp(coord.x, 0.0, 1.0) * 65535.0;\n"
    "    float ny = clamp(coord.y, 0.0, 1.0) * 65535.0;\n"
    "    float rx = floor(nx / 256.0);\n"
    "    float gx = nx - rx * 256.0;\n"
    "    float ry = floor(ny / 256.0);\n"
    "    float gy = ny - ry * 256.0;\n"
    "    return vec4(rx / 255.0, gx / 255.0, ry / 255.0, gy / 255.0);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    // Phase-based reset: output identity mapping when weight is zero\n"
    "    if (forceIdentity == 1) {\n"
    "        gl_FragColor = encodeCoord(vUV);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // Read displacement at this pixel (dual-channel encoded)\n"
    "    vec2 dispNorm = decodeCoord(texture2D(uDisp, vUV));\n"
    "    vec2 disp = (dispNorm - 0.5) * 2.0 * maxDisp;  // [0,1] -> [-maxDisp, maxDisp]\n"
    "\n"
    "    // Compute sample position (current UV + displacement)\n"
    "    vec2 sampleUV = clamp(vUV + disp / texSize, vec2(0.0), vec2(1.0));\n"
    "\n"
    "    // Keys cubic convolution (a=-0.5) 4x4 sampling of coordinate field\n"
    "    vec2 ts = 1.0 / texSize;\n"
    "    vec2 tc = sampleUV * texSize - 0.5;\n"
    "    vec2 f = fract(tc);\n"
    "    float t;\n"
    "    t = f.x; float t2=t*t, t3=t2*t;\n"
    "    vec4 wx = vec4(-0.5*t3+t2-0.5*t, 1.5*t3-2.5*t2+1.0,\n"
    "                   -1.5*t3+2.0*t2+0.5*t, 0.5*t3-0.5*t2);\n"
    "    t = f.y; t2=t*t; t3=t2*t;\n"
    "    vec4 wy = vec4(-0.5*t3+t2-0.5*t, 1.5*t3-2.5*t2+1.0,\n"
    "                   -1.5*t3+2.0*t2+0.5*t, 0.5*t3-0.5*t2);\n"
    "\n"
    "    vec2 result = vec2(0.0);\n"
    "    for (int j = -1; j <= 2; j++) {\n"
    "        for (int i = -1; i <= 2; i++) {\n"
    "            vec2 suv = clamp((tc + vec2(float(i), float(j)) + 0.5) * ts, vec2(0.0), vec2(1.0));\n"
    "            vec2 coord = decodeCoord(texture2D(uCoord, suv));\n"
    "            result += coord * wx[i+1] * wy[j+1];\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = encodeCoord(result);\n"
    "}\n";

// Pass 3: Advect scalar from dual coordinate fields + blend + LUT
// Reads: coefficient texture (B-spline encoded), coordA, coordB, colorLUT
// Writes: final color to screen
static const char* s_frag_advect =
    "#version 120\n"
    "uniform sampler2D uScalar;\n"
    "uniform sampler2D uCoordA;\n"
    "uniform sampler2D uCoordB;\n"
    "uniform sampler2D uColorLUT;\n"
    "uniform vec2 scalarSize;\n"
    "uniform int animFrame;\n"
    "uniform int animPeriod;\n"
    "uniform float dataMin;\n"
    "uniform float dataMax;\n"
    "uniform float lutMin;\n"
    "uniform float lutMax;\n"
    "varying vec2 vUV;\n"
    "\n"
    "vec2 decodeCoord(vec4 c) {\n"
    "    float x = (c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0;\n"
    "    float y = (c.b * 255.0 * 256.0 + c.a * 255.0) / 65535.0;\n"
    "    return vec2(x, y);\n"
    "}\n"
    "\n"
    "float bsplineWeight(float t, int i) {\n"
    "    float t2=t*t, t3=t2*t;\n"
    "    float w0 = (-t3+3.0*t2-3.0*t+1.0)/6.0;\n"
    "    float w1 = (3.0*t3-6.0*t2+4.0)/6.0;\n"
    "    float w2 = (-3.0*t3+3.0*t2+3.0*t+1.0)/6.0;\n"
    "    float w3 = t3/6.0;\n"
    "    float fi = float(i);\n"
    "    return mix(mix(w0, w1, step(0.5, fi)),\n"
    "               mix(w2, w3, step(2.5, fi)),\n"
    "               step(1.5, fi));\n"
    "}\n"
    "\n"
    "// B-spline sampling on coefficient texture at normalized UV position\n"
    "float sampleBicubic(sampler2D tex, vec2 uv, vec2 sz) {\n"
    "    vec2 ts = 1.0 / sz;\n"
    "    vec2 tc = uv * sz - 0.5;\n"
    "    vec2 f = fract(tc);\n"
    "    float val = 0.0, wSum = 0.0;\n"
    "    for (int j = -1; j <= 2; j++) {\n"
    "        for (int i = -1; i <= 2; i++) {\n"
    "            vec2 suv = clamp((tc + vec2(float(i), float(j)) + 0.5) * ts, vec2(0.0), vec2(1.0));\n"
    "            vec4 s = texture2D(tex, suv);\n"
    "            if (s.a > 0.01) {\n"
    "                float w = bsplineWeight(f.x, i+1) * bsplineWeight(f.y, j+1);\n"
    "                val += s.r * w;\n"
    "                wSum += w;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return (wSum > 0.0) ? val / wSum : -1.0;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    // Check alpha at current pixel — same as static shader land/border detection\n"
    "    float centerAlpha = texture2D(uScalar, vUV).a;\n"
    "    if (centerAlpha < 0.001) { gl_FragColor = vec4(0.0); return; }  // border: transparent\n"
    "    if (centerAlpha < 0.01) { gl_FragColor = vec4(0.667, 0.686, 0.314, 1.0); return; }  // land: green\n"
    "\n"
    "    // Decode dual-channel coordinate fields\n"
    "    vec2 coordA = decodeCoord(texture2D(uCoordA, vUV));\n"
    "    vec2 coordB = decodeCoord(texture2D(uCoordB, vUV));\n"
    "\n"
    "    // B-spline sampling of coefficient texture at advected positions\n"
    "    float valA = sampleBicubic(uScalar, coordA, scalarSize);\n"
    "    float valB = sampleBicubic(uScalar, coordB, scalarSize);\n"
    "\n"
    "    // Dual-coordinate blending weights (patent sine formula)\n"
    "    float t1 = mod(float(animFrame), float(animPeriod));\n"
    "    float w1 = sin(3.14159265 * t1 / max(float(animPeriod), 1.0));\n"
    "    float t2 = mod(float(animFrame) + float(animPeriod) * 0.5, float(animPeriod));\n"
    "    float w2 = sin(3.14159265 * t2 / max(float(animPeriod), 1.0));\n"
    "    float wsum = w1 + w2;\n"
    "    float a1 = w1 / wsum;\n"
    "    float a2 = w2 / wsum;\n"
    "\n"
    "    // Handle land (invalid values) — output transparent so base map shows through\n"
    "    if (valA < 0.0 && valB < 0.0) {\n"
    "        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
    "    float landBlend = 0.0;\n"
    "    if (valA < 0.0) { valA = 0.0; landBlend += a1; }\n"
    "    if (valB < 0.0) { valB = 0.0; landBlend += a2; }\n"
    "\n"
    "    // Denormalize to physical value (same as static color map shader)\n"
    "    float blended = clamp(a1 * valA + a2 * valB, 0.0, 1.0);\n"
    "    float coefRange = dataMax - dataMin;\n"
    "    float physical = dataMin + blended * coefRange;\n"
    "    float lutRange = lutMax - lutMin;\n"
    "    float result = (lutRange > 0.0001) ? clamp((physical - lutMin) / lutRange, 0.0, 1.0) : 0.0;\n"
    "    vec3 oceanColor = texture2D(uColorLUT, vec2(result, 0.5)).rgb;\n"
    "    gl_FragColor = vec4(oceanColor, 1.0 - clamp(landBlend, 0.0, 1.0));\n"
    "}\n";

// Pass 4: Display texture to screen
static const char* s_frag_display =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex, vUV);\n"
    "}\n";

//===================================================================
// Compilation
//===================================================================
static GLuint compile(GLenum type, const char* src) {
    GLuint s = fpCreateShader(type);
    if (!s) return 0;
    fpShaderSource(s, 1, &src, NULL);
    fpCompileShader(s);
    GLint st; fpGetShaderiv(s, GL_COMPILE_STATUS, &st);
    if (!st) {
        char log[512]; fpGetShaderInfoLog(s, sizeof(log), NULL, log);
        animLog("compile error: %s", log);
        fpDeleteShader(s); return 0;
    }
    return s;
}

static GLuint link(GLuint vs, GLuint fs) {
    GLuint p = fpCreateProgram();
    if (!p) return 0;
    fpAttachShader(p, vs); fpAttachShader(p, fs);
    fpBindAttribLocation(p, 0, "aPos");
    fpLinkProgram(p);
    GLint st; fpGetProgramiv(p, GL_LINK_STATUS, &st);
    if (!st) {
        char log[512]; fpGetProgramInfoLog(p, sizeof(log), NULL, log);
        animLog("link error: %s", log);
        fpDeleteProgram(p); return 0;
    }
    return p;
}

static GLuint buildProgram(const char* fragSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, s_vert);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!fs) { fpDeleteShader(vs); return 0; }
    GLuint prog = link(vs, fs);
    fpDeleteShader(vs); fpDeleteShader(fs);
    return prog;
}

//===================================================================
// Module state
//===================================================================
static bool s_initialized = false;
static GLuint s_progUpdate = 0, s_progAdvect = 0, s_progDisplay = 0;
static AnimUniforms s_uUpdate, s_uAdvect, s_uDisplay;

static void cacheUniforms(GLuint prog, AnimUniforms& u, const char* tex0, const char* tex1, const char* tex2) {
    memset(&u, 0xFF, sizeof(u));
    u.uTex0 = fpGetUniformLocation(prog, tex0);
    u.uTex1 = fpGetUniformLocation(prog, tex1);
    u.uTex2 = fpGetUniformLocation(prog, tex2);
    u.texSize = fpGetUniformLocation(prog, "texSize");
    u.scalarSize = fpGetUniformLocation(prog, "scalarSize");
    u.frame = fpGetUniformLocation(prog, "animFrame");
    u.period = fpGetUniformLocation(prog, "animPeriod");
    u.uColorLUT = fpGetUniformLocation(prog, "uColorLUT");
    u.dataMin = fpGetUniformLocation(prog, "dataMin");
    u.dataMax = fpGetUniformLocation(prog, "dataMax");
    u.lutMin = fpGetUniformLocation(prog, "lutMin");
    u.lutMax = fpGetUniformLocation(prog, "lutMax");
    u.maxDisp = fpGetUniformLocation(prog, "maxDisp");
    u.forceIdentity = fpGetUniformLocation(prog, "forceIdentity");
}

bool ncdf_animate_shader_supported() {
    const char* v = (const char*)glGetString(GL_VERSION);
    if (!v) return false;
    int mj = 0; sscanf(v, "%d", &mj);
    return mj >= 2;
}

bool ncdf_animate_shader_init() {
    if (s_initialized) return true;

    // Force cleanup old programs (source code may have changed between DLL reloads)
    if (s_progUpdate || s_progAdvect || s_progDisplay) {
        ncdf_animate_shader_cleanup();
    }

    if (!ncdf_animate_shader_supported()) { animLog("GL < 2.0"); return false; }
    if (!load_gl_extensions()) { animLog("ext load failed"); return false; }

    animLog("compiling shaders...");
    s_progUpdate = buildProgram(s_frag_update);
    s_progAdvect = buildProgram(s_frag_advect);
    s_progDisplay = buildProgram(s_frag_display);

    if (!s_progUpdate || !s_progAdvect || !s_progDisplay) {
        animLog("shader build failed");
        ncdf_animate_shader_cleanup(); return false;
    }

    cacheUniforms(s_progUpdate, s_uUpdate, "uCoord", "uDisp", "");
    cacheUniforms(s_progAdvect, s_uAdvect, "uScalar", "uCoordA", "uCoordB");
    s_uAdvect.frame = fpGetUniformLocation(s_progAdvect, "animFrame");
    s_uAdvect.period = fpGetUniformLocation(s_progAdvect, "animPeriod");
    cacheUniforms(s_progDisplay, s_uDisplay, "uTex", "", "");

    s_initialized = true;
    animLog("init OK update=%u advect=%u display=%u", s_progUpdate, s_progAdvect, s_progDisplay);
    return true;
}

void ncdf_animate_shader_cleanup() {
    if (s_progUpdate) { fpDeleteProgram(s_progUpdate); s_progUpdate = 0; }
    if (s_progAdvect) { fpDeleteProgram(s_progAdvect); s_progAdvect = 0; }
    if (s_progDisplay) { fpDeleteProgram(s_progDisplay); s_progDisplay = 0; }
    s_initialized = false;
}

bool ncdf_animate_shader_initialized() { return s_initialized; }
unsigned int ncdf_animate_get_update_program() { return s_progUpdate; }
unsigned int ncdf_animate_get_advect_program() { return s_progAdvect; }
unsigned int ncdf_animate_get_display_program() { return s_progDisplay; }
AnimUniforms ncdf_animate_get_update_uniforms() { return s_uUpdate; }
AnimUniforms ncdf_animate_get_advect_uniforms() { return s_uAdvect; }
AnimUniforms ncdf_animate_get_display_uniforms() { return s_uDisplay; }

//===================================================================
// FBO / Texture helpers
//===================================================================
unsigned int ncdf_animate_create_fbo() { GLuint f; fpGenFramebuffers(1,&f); return f; }
void ncdf_animate_delete_fbo(unsigned int f) { if(f&&fpDeleteFramebuffers) fpDeleteFramebuffers(1,&f); }
void ncdf_animate_bind_fbo(unsigned int f, unsigned int t) {
    fpBindFramebuffer(GL_FRAMEBUFFER,f);
    fpFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    // Explicitly set draw buffer — required on some drivers to actually write to the attachment
    typedef void (APIENTRY *PFNDB)(GLenum);
    static PFNDB fpDrawBuf = (PFNDB)load_gl("glDrawBuffer");
    if (fpDrawBuf) fpDrawBuf(GL_COLOR_ATTACHMENT0);
    if (fpCheckFramebufferStatus) {
        GLenum status = fpCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != 0x8CD5 /*GL_FRAMEBUFFER_COMPLETE*/)
            animLog("FBO incomplete: 0x%X, fbo=%u tex=%u", status, f, t);
    }
}
void ncdf_animate_unbind_fbo() { fpBindFramebuffer(GL_FRAMEBUFFER,0); }
void ncdf_animate_bind_fbo_only(unsigned int f) { if(fpBindFramebuffer) fpBindFramebuffer(GL_FRAMEBUFFER,f); }

// REMOVED: ncdf_animate_create_float_texture used GL_RGBA16F which is
// not supported on OpenCPN's OpenGL 2.1. Use ncdf_animate_create_encoded_texture
// (RGBA8 + GL_NEAREST) for coordinate/displacement textures instead.

unsigned int ncdf_animate_create_rgba_texture(int w, int h) {
    GLuint t; glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    return t;
}

// Encoded texture: GL_NEAREST (no interpolation on dual-channel encoded values)
unsigned int ncdf_animate_create_encoded_texture(int w, int h) {
    GLuint t; glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    return t;
}

void ncdf_animate_use_program(unsigned int p) { if(fpUseProgram) fpUseProgram(p); }
void ncdf_animate_uniform_1i(int loc, int v) { if(fpUniform1i&&loc>=0) fpUniform1i(loc,v); }
void ncdf_animate_uniform_1f(int loc, float v) { if(fpUniform1f&&loc>=0) fpUniform1f(loc,v); }
void ncdf_animate_uniform_2f(int loc, float x, float y) { if(fpUniform2f&&loc>=0) fpUniform2f(loc,x,y); }
void ncdf_animate_active_texture(unsigned int t) { if(fpActiveTexture) fpActiveTexture(t); }

void ncdf_animate_draw_quad() {
    static float q[] = {-1,-1,1,-1,1,1,-1,1};
    if(fpEnableVertexAttribArray) fpEnableVertexAttribArray(0);
    if(fpVertexAttribPointer) fpVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,q);
    glDrawArrays(GL_QUADS,0,4);
    if(fpDisableVertexAttribArray) fpDisableVertexAttribArray(0);
}

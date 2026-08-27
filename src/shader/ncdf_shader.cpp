//===================================================================
// ncdf_shader.cpp — Bicubic interpolation shader framework
// Runtime GL extension loading for OpenGL 2.0+ shader support
//===================================================================

#include "shader/ncdf_shader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <wx/log.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// GL types not in gl.h
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

//===================================================================
// Runtime GL function loading
//===================================================================
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

// Function pointer types (OpenGL 2.0 signatures)
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

    return (fpCreateProgram && fpCreateShader && fpCompileShader && fpShaderSource &&
            fpLinkProgram && fpUseProgram && fpGetShaderiv && fpGetProgramiv &&
            fpGetUniformLocation && fpUniform1i && fpEnableVertexAttribArray &&
            fpDisableVertexAttribArray && fpVertexAttribPointer);
}

//===================================================================
// GLSL Shaders (version 1.20 for OpenGL 2.1 compatibility)
//===================================================================

// Vertex shader: pass-through position + UV
static const char* s_vert =
    "#version 120\n"
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// Fragment shader: adaptive interpolation + LUT color lookup
// Both scalar and vector modes use the same land-aware adaptive logic
// Scalar: R=coefficient, Vector: R=u, G=v
static const char* s_frag =
    "#version 120\n"
    "uniform sampler2D dataTex;\n"
    "uniform sampler2D physicalTex;\n"
    "uniform sampler2D colorLUT;\n"
    "uniform vec2 texSize;\n"
    "uniform int vectorMode;\n"
    "uniform float dataMin;\n"
    "uniform float dataMax;\n"
    "uniform float dataMinV;\n"
    "uniform float dataMaxV;\n"
    "uniform float lutMin;\n"
    "uniform float lutMax;\n"
    "uniform float physMin;\n"
    "uniform float physMax;\n"
    "uniform float physMinV;\n"
    "uniform float physMaxV;\n"
    "varying vec2 vUV;\n"
    "\n"
    "float bspline(float t) {\n"
    "    t = abs(t);\n"
    "    if (t < 1.0) return 0.5*t*t*t - t*t + 2.0/3.0;\n"
    "    if (t < 2.0) { float d = 2.0 - t; return d*d*d / 6.0; }\n"
    "    return 0.0;\n"
    "}\n"
    "\n"
    "float cross2d(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }\n"
    "bool inTriangle(vec2 p, vec2 v0, vec2 v1, vec2 v2) {\n"
    "    float d0 = cross2d(v1-v0, p-v0);\n"
    "    float d1 = cross2d(v2-v1, p-v1);\n"
    "    float d2 = cross2d(v0-v2, p-v2);\n"
    "    bool hasNeg = (d0<0.0) || (d1<0.0) || (d2<0.0);\n"
    "    bool hasPos = (d0>0.0) || (d1>0.0) || (d2>0.0);\n"
    "    return !(hasNeg && hasPos);\n"
    "}\n"
    "\n"
    "// Read physical value: scalar uses dataTex.g, vector uses physicalTex.rg\n"
    "vec2 readPhysical(vec2 uv) {\n"
    "    if (vectorMode == 1) return texture2D(physicalTex, uv).rg;\n"
    "    return vec2(texture2D(dataTex, uv).g, 0.0);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 ts = 1.0 / texSize;\n"
    "    vec2 tc = vUV * texSize - 0.5;\n"
    "    vec2 f = fract(tc);\n"
    "\n"
    "    if (texture2D(dataTex, vUV).a < 0.01) { gl_FragColor = vec4(0.667, 0.686, 0.314, 1.0); return; }\n"
    "\n"
    "    float rSum=0.0, gSum=0.0, wSum=0.0;\n"
    "    bool usedBSpline = false;\n"
    "\n"
    "    // Sample 2x2 center from coefficient texture\n"
    "    vec4 s00=texture2D(dataTex, (tc+vec2(0.5,0.5))*ts);\n"
    "    vec4 s10=texture2D(dataTex, (tc+vec2(1.5,0.5))*ts);\n"
    "    vec4 s01=texture2D(dataTex, (tc+vec2(0.5,1.5))*ts);\n"
    "    vec4 s11=texture2D(dataTex, (tc+vec2(1.5,1.5))*ts);\n"
    "    bool h00=s00.a>0.01, h10=s10.a>0.01, h01=s01.a>0.01, h11=s11.a>0.01;\n"
    "    int land2x2 = (h00?0:1)+(h10?0:1)+(h01?0:1)+(h11?0:1);\n"
    "\n"
    "    if(land2x2 == 0) {\n"
    "        // 2x2 clean: check if 4x4 is also clean\n"
    "        int land4x4 = 0;\n"
    "        vec4 s[16];\n"
    "        s[1*4+1]=s00; s[1*4+2]=s10; s[2*4+1]=s01; s[2*4+2]=s11;\n"
    "        for(int j=-1;j<=2;j++)\n"
    "            for(int i=-1;i<=2;i++){\n"
    "                if(i>=0&&i<=1&&j>=0&&j<=1) continue;\n"
    "                vec2 suv=(tc+vec2(float(i),float(j))+0.5)*ts;\n"
    "                s[(j+1)*4+(i+1)] = texture2D(dataTex,suv);\n"
    "                if(s[(j+1)*4+(i+1)].a<0.01) land4x4++;\n"
    "            }\n"
    "        if(land4x4 == 0) {\n"
    "            // Case 1: Full ocean -> B-spline on coefficients\n"
    "            usedBSpline = true;\n"
    "            float bx[4], by[4];\n"
    "            for(int k=0;k<4;k++){ bx[k]=bspline(f.x-float(k-1)); by[k]=bspline(f.y-float(k-1)); }\n"
    "            for(int j=0;j<4;j++) for(int i=0;i<4;i++){ float w=bx[i]*by[j]; rSum+=s[j*4+i].r*w; gSum+=s[j*4+i].g*w; wSum+=w; }\n"
    "        } else {\n"
    "            // Case 2: 4x4 has land, 2x2 clean -> bilinear on physical values\n"
    "            vec2 p00=readPhysical((tc+vec2(0.5,0.5))*ts);\n"
    "            vec2 p10=readPhysical((tc+vec2(1.5,0.5))*ts);\n"
    "            vec2 p01=readPhysical((tc+vec2(0.5,1.5))*ts);\n"
    "            vec2 p11=readPhysical((tc+vec2(1.5,1.5))*ts);\n"
    "            rSum=p00.r*(1.0-f.x)*(1.0-f.y)+p10.r*f.x*(1.0-f.y)+p01.r*(1.0-f.x)*f.y+p11.r*f.x*f.y;\n"
    "            gSum=p00.g*(1.0-f.x)*(1.0-f.y)+p10.g*f.x*(1.0-f.y)+p01.g*(1.0-f.x)*f.y+p11.g*f.x*f.y;\n"
    "            wSum=1.0;\n"
    "        }\n"
    "    } else if(land2x2 == 1) {\n"
    "        // Case 3: 1 land, 3 ocean -> bilinear (skip land)\n"
    "        vec2 p00=readPhysical((tc+vec2(0.5,0.5))*ts);\n"
    "        vec2 p10=readPhysical((tc+vec2(1.5,0.5))*ts);\n"
    "        vec2 p01=readPhysical((tc+vec2(0.5,1.5))*ts);\n"
    "        vec2 p11=readPhysical((tc+vec2(1.5,1.5))*ts);\n"
    "        float w0=0.0,w1=0.0,w2=0.0,w3=0.0;\n"
    "        if(h00) w0=(1.0-f.x)*(1.0-f.y);\n"
    "        if(h10) w1=f.x*(1.0-f.y);\n"
    "        if(h01) w2=(1.0-f.x)*f.y;\n"
    "        if(h11) w3=f.x*f.y;\n"
    "        rSum=p00.r*w0+p10.r*w1+p01.r*w2+p11.r*w3;\n"
    "        gSum=p00.g*w0+p10.g*w1+p01.g*w2+p11.g*w3;\n"
    "        wSum=w0+w1+w2+w3;\n"
    "    } else if(land2x2 == 2) {\n"
    "        // Case 4/5: 2 land -> land color\n"
    "        gl_FragColor = vec4(0.667, 0.686, 0.314, 1.0); return;\n"
    "    } else {\n"
    "        // Case 6/7: 3 or 4 land -> land color\n"
    "        gl_FragColor = vec4(0.667, 0.686, 0.314, 1.0); return;\n"
    "    }\n"
    "\n"
    "    if(wSum<=0.0) { gl_FragColor = vec4(0.667, 0.686, 0.314, 1.0); return; }\n"
    "\n"
    "    float coefRange = dataMax - dataMin;\n"
    "    float coefRangeV = dataMaxV - dataMinV;\n"
    "    float physRange = physMax - physMin;\n"
    "    float physRangeV = physMaxV - physMinV;\n"
    "    float lutRange = lutMax - lutMin;\n"
    "    float result;\n"
    "    if (vectorMode == 1) {\n"
    "        float u, v;\n"
    "        if(usedBSpline) {\n"
    "            u = dataMin + (rSum/wSum) * coefRange;\n"
    "            v = dataMinV + (gSum/wSum) * coefRangeV;\n"
    "        } else {\n"
    "            u = physMin + (rSum/wSum) * physRange;\n"
    "            v = physMinV + (gSum/wSum) * physRangeV;\n"
    "        }\n"
    "        float speed = sqrt(u*u + v*v);\n"
    "        result = (lutRange > 0.0001) ? clamp((speed - lutMin) / lutRange, 0.0, 1.0) : 0.0;\n"
    "    } else {\n"
    "        float physical;\n"
    "        if(usedBSpline) {\n"
    "            physical = dataMin + (rSum/wSum) * coefRange;\n"
    "        } else {\n"
    "            physical = physMin + (rSum/wSum) * physRange;\n"
    "        }\n"
    "        result = (lutRange > 0.0001) ? clamp((physical - lutMin) / lutRange, 0.0, 1.0) : 0.0;\n"
    "    }\n"
    "\n"
    "    gl_FragColor = vec4(texture2D(colorLUT, vec2(result, 0.5)).rgb, 1.0);\n"
    "}\n";

//===================================================================
// Shader compilation and linking
//===================================================================
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
        wxLogMessage(_T("[shader] compile error: %s"), wxString(log, wxConvUTF8));
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
        wxLogMessage(_T("[shader] link error: %s"), wxString(log, wxConvUTF8));
        fpDeleteProgram(p);
        return 0;
    }
    return p;
}

//===================================================================
// Module state
//===================================================================
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
    if (!s_supported) {
        wxLogMessage(_T("[shader] GL < 2.0, fallback to fixed pipeline"));
        return false;
    }

    wxLogMessage(_T("[shader] loading GL extensions..."));
    if (!load_gl_extensions()) {
        wxLogMessage(_T("[shader] ext load failed"));
        s_supported = false;
        return false;
    }

    wxLogMessage(_T("[shader] compiling shaders..."));
    s_vs = compile_shader_gl(GL_VERTEX_SHADER, s_vert);
    if (!s_vs) { wxLogMessage(_T("[shader] vertex shader failed")); s_supported = false; return false; }
    s_fs = compile_shader_gl(GL_FRAGMENT_SHADER, s_frag);
    if (!s_fs) { wxLogMessage(_T("[shader] fragment shader failed")); fpDeleteShader(s_vs); s_vs = 0; s_supported = false; return false; }

    wxLogMessage(_T("[shader] linking program..."));
    s_program = link_program_gl(s_vs, s_fs);
    if (!s_program) {
        wxLogMessage(_T("[shader] link failed"));
        fpDeleteShader(s_vs); s_vs = 0;
        fpDeleteShader(s_fs); s_fs = 0;
        s_supported = false;
        return false;
    }

    // Cache uniform locations
    memset(&s_uniforms, 0xFF, sizeof(s_uniforms));
    s_uniforms.dataTex = fpGetUniformLocation(s_program, "dataTex");
    s_uniforms.physicalTex = fpGetUniformLocation(s_program, "physicalTex");
    s_uniforms.colorLUT = fpGetUniformLocation(s_program, "colorLUT");
    s_uniforms.texSize = fpGetUniformLocation(s_program, "texSize");
    s_uniforms.vectorMode = fpGetUniformLocation(s_program, "vectorMode");
    s_uniforms.dataMin = fpGetUniformLocation(s_program, "dataMin");
    s_uniforms.dataMax = fpGetUniformLocation(s_program, "dataMax");
    s_uniforms.dataMinV = fpGetUniformLocation(s_program, "dataMinV");
    s_uniforms.dataMaxV = fpGetUniformLocation(s_program, "dataMaxV");
    s_uniforms.lutMin = fpGetUniformLocation(s_program, "lutMin");
    s_uniforms.lutMax = fpGetUniformLocation(s_program, "lutMax");
    s_uniforms.physMin = fpGetUniformLocation(s_program, "physMin");
    s_uniforms.physMax = fpGetUniformLocation(s_program, "physMax");
    s_uniforms.physMinV = fpGetUniformLocation(s_program, "physMinV");
    s_uniforms.physMaxV = fpGetUniformLocation(s_program, "physMaxV");

    s_initialized = true;
    wxLogMessage(_T("[shader] init OK program=%u"), s_program);
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

//===================================================================
// Runtime GL function wrappers
//===================================================================
void ncdf_shader_use_program(unsigned int p) {
    if (fpUseProgram) fpUseProgram(p);
}
void ncdf_shader_uniform_1i(int loc, int v) {
    if (fpUniform1i && loc >= 0) fpUniform1i(loc, v);
}
void ncdf_shader_uniform_1f(int loc, float v) {
    if (fpUniform1f && loc >= 0) fpUniform1f(loc, v);
}
void ncdf_shader_uniform_2f(int loc, float x, float y) {
    if (fpUniform2f && loc >= 0) fpUniform2f(loc, x, y);
}
void ncdf_shader_active_texture(unsigned int t) {
    if (fpActiveTexture) fpActiveTexture(t);
}
void ncdf_shader_enable_attrib(unsigned int i) {
    if (fpEnableVertexAttribArray) fpEnableVertexAttribArray(i);
}
void ncdf_shader_disable_attrib(unsigned int i) {
    if (fpDisableVertexAttribArray) fpDisableVertexAttribArray(i);
}
void ncdf_shader_attrib_pointer(unsigned int i, int sz, int stride, const void* p) {
    if (fpVertexAttribPointer) fpVertexAttribPointer(i, sz, GL_FLOAT, GL_FALSE, stride, p);
}

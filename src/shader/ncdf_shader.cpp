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

// Fragment shader: configurable interpolation on scalar data
// mode=0: bilinear (2x2, land-aware)
// mode=1: bicubic (Catmull-Rom 4x4 + clamp)
// mode=2: monotone bicubic (PCHIP 4x4 + clamp)
static const char* s_frag =
    "#version 120\n"
    "uniform sampler2D dataTex;\n"
    "uniform sampler2D colorLUT;\n"
    "uniform sampler2D slopeTex;\n"
    "uniform vec2 texSize;\n"
    "uniform int mode;\n"
    "uniform int sCurve;\n"
    "uniform int slope;\n"
    "uniform float dataMin;\n"
    "uniform float dataMax;\n"
    "uniform int slopeMode;\n"
    "varying vec2 vUV;\n"
    "\n"
    "vec4 cubic(float t) {\n"
    "    float t2=t*t, t3=t2*t;\n"
    "    vec4 w;\n"
    "    w.x = -0.5*t3+t2-0.5*t;\n"
    "    w.y =  1.5*t3-2.5*t2+1.0;\n"
    "    w.z = -1.5*t3+2.0*t2+0.5*t;\n"
    "    w.w =  0.5*t3-0.5*t2;\n"
    "    return w;\n"
    "}\n"
    "\n"
    "float pchipSlope(bool hp, float dp, bool hn, float dn) {\n"
    "    if (hp && hn) {\n"
    "        if (dp*dn <= 0.0) return 0.0;\n"
    "        return (dp+dn)/2.0;\n"
    "    }\n"
    "    if (hp) return 2.0*dp/3.0;\n"
    "    if (hn) return dn/3.0;\n"
    "    return 0.0;\n"
    "}\n"
    "\n"
    "float pchipRow(float v0,bool h0,float v1,bool h1,\n"
    "               float v2,bool h2,float v3,bool h3,\n"
    "               float t, out float slope) {\n"
    "    if (h1 && h2) {\n"
    "        float m1=pchipSlope(h0,v1-v0,h2,v2-v1);\n"
    "        float m2=pchipSlope(h1,v2-v1,h3,v3-v2);\n"
    "        float t2=t*t,t3=t2*t;\n"
    "        slope=m2;\n"
    "        return (2.0*t3-3.0*t2+1.0)*v1+(t3-2.0*t2+t)*m1\n"
    "             +(-2.0*t3+3.0*t2)*v2+(t3-t2)*m2;\n"
    "    }\n"
    "    if (h1){slope=0.0;return v1;}\n"
    "    if (h2){slope=0.0;return v2;}\n"
    "    slope=0.0;return 0.0;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 center = texture2D(dataTex, vUV);\n"
    "    if (center.a < 0.01) discard;\n"
    "    vec2 ts = 1.0 / texSize;\n"
    "    vec2 tc = vUV * texSize - 0.5;\n"
    "    vec2 f = fract(tc);\n"
    "    float result;\n"
    "\n"
    "    if (mode == 0) {\n"
    "        // Bilinear scalar: 2x2 center\n"
    "        float v00=center.r;\n"
    "        vec4 s10=texture2D(dataTex,vUV+vec2(ts.x,0.0));\n"
    "        vec4 s01=texture2D(dataTex,vUV+vec2(0.0,ts.y));\n"
    "        vec4 s11=texture2D(dataTex,vUV+vec2(ts.x,ts.y));\n"
    "        float wSum=1.0,val=v00;\n"
    "        if(s10.a>0.01){val+=f.x*s10.r;wSum+=f.x;}\n"
    "        if(s01.a>0.01){val+=f.y*s01.r;wSum+=f.y;}\n"
    "        if(s11.a>0.01){val+=f.x*f.y*s11.r;wSum+=f.x*f.y;}\n"
    "        result=val/wSum;\n"
    "    } else {\n"
    "        if (mode == 1) {\n"
    "            // Bicubic: Catmull-Rom 4x4\n"
    "            vec4 wx=cubic(f.x),wy=cubic(f.y);\n"
    "            float val=0.0,wSum=0.0;\n"
    "            for(int j=-1;j<=2;j++){\n"
    "                for(int i=-1;i<=2;i++){\n"
    "                    vec2 suv=(tc+vec2(float(i),float(j))+0.5)*ts;\n"
    "                    vec4 s=texture2D(dataTex,suv);\n"
    "                    if(s.a>0.01){float w=wx[i+1]*wy[j+1];val+=s.r*w;wSum+=w;}\n"
    "                }\n"
    "            }\n"
    "            if(wSum<=0.0)discard;\n"
    "            result=val/wSum;\n"
    "        } else {\n"
    "            // Monotone bicubic: PCHIP\n"
    "            float sv[16];bool hv[16];\n"
    "            for(int j=-1;j<=2;j++){\n"
    "                for(int i=-1;i<=2;i++){\n"
    "                    int idx=(j+1)*4+(i+1);\n"
    "                    vec2 suv=(tc+vec2(float(i),float(j))+0.5)*ts;\n"
    "                    vec4 s=texture2D(dataTex,suv);\n"
    "                    sv[idx]=s.r;hv[idx]=(s.a>0.01);\n"
    "                }\n"
    "            }\n"
    "            float rowVal[4];bool rowHas[4];float rowSl[4];\n"
    "            for(int j=0;j<4;j++){\n"
    "                rowVal[j]=pchipRow(sv[j*4],hv[j*4],sv[j*4+1],hv[j*4+1],\n"
    "                    sv[j*4+2],hv[j*4+2],sv[j*4+3],hv[j*4+3],f.x,rowSl[j]);\n"
    "                rowHas[j]=hv[j*4+1]||hv[j*4+2];\n"
    "            }\n"
    "            float val=0.0,wSum=0.0;\n"
    "            for(int j=0;j<4;j++){\n"
    "                if(!rowHas[j])continue;\n"
    "                float dP=(j>0&&rowHas[j-1])?rowVal[j]-rowVal[j-1]:0.0;\n"
    "                float dN=(j<3&&rowHas[j+1])?rowVal[j+1]-rowVal[j]:0.0;\n"
    "                float m=pchipSlope(j>0&&rowHas[j-1],dP,j<3&&rowHas[j+1],dN);\n"
    "                float w;\n"
    "                if(f.y==0.0){w=(j==1)?1.0:0.0;}\n"
    "                else if(j==0){w=-m*f.y*(1.0-f.y)*(1.0-f.y);}\n"
    "                else if(j==1){float t2=f.y*f.y,t3=t2*f.y;\n"
    "                    w=2.0*t3-3.0*t2+1.0+(t3-2.0*t2+f.y);}\n"
    "                else if(j==2){float t2=f.y*f.y,t3=t2*f.y;\n"
    "                    w=-2.0*t3+3.0*t2+(t3-t2);}\n"
    "                else{w=m*f.y*f.y*(f.y-1.0);}\n"
    "                val+=rowVal[j]*w;wSum+=w;\n"
    "            }\n"
    "            if(wSum<=0.0)discard;\n"
    "            result=val/wSum;\n"
    "        }\n"
    "    }\n"
    "    // S-curve color mapping (enhance mid-tone contrast)\n"
    "    if (sCurve == 1) {\n"
    "        result = 1.0 / (1.0 + exp(-10.0 * (result - 0.5)));\n"
    "    }\n"
    "    vec3 color = texture2D(colorLUT, vec2(result, 0.5)).rgb;\n"
    "\n"
    "    // Slope shading: modulate brightness by gradient from slopeTex\n"
    "    if (slope == 1) {\n"
    "        float vN = texture2D(slopeTex, vUV + vec2(0.0, -ts.y)).r;\n"
    "        float vS = texture2D(slopeTex, vUV + vec2(0.0,  ts.y)).r;\n"
    "        float vW = texture2D(slopeTex, vUV + vec2(-ts.x, 0.0)).r;\n"
    "        float vE = texture2D(slopeTex, vUV + vec2( ts.x, 0.0)).r;\n"
    "        bool nValid = texture2D(slopeTex, vUV + vec2(0.0, -ts.y)).a > 0.01;\n"
    "        bool sValid = texture2D(slopeTex, vUV + vec2(0.0,  ts.y)).a > 0.01;\n"
    "        bool wValid = texture2D(slopeTex, vUV + vec2(-ts.x, 0.0)).a > 0.01;\n"
    "        bool eValid = texture2D(slopeTex, vUV + vec2( ts.x, 0.0)).a > 0.01;\n"
    "        if (nValid && sValid && wValid && eValid) {\n"
    "            float dataRange = dataMax - dataMin;\n"
    "            if (slopeMode == 1) {\n"
    "                // SST: smooth gradient with enhancement at temperature boundaries\n"
    "                float tC = result * dataRange + dataMin;\n"
    "                float nC = vN * dataRange + dataMin;\n"
    "                float sC = vS * dataRange + dataMin;\n"
    "                float wC = vW * dataRange + dataMin;\n"
    "                float eC = vE * dataRange + dataMin;\n"
    "                // Sigmoid remap: enhances contrast at 2°C segment boundaries\n"
    "                // peaks at 1, 3, 5, 7, ..., 31°C (boundaries between 2°C segments)\n"
    "                float sCv = 0.5 + 0.5 * sin((tC - 1.0) * 3.14159);\n"
    "                float sN  = 0.5 + 0.5 * sin((nC - 1.0) * 3.14159);\n"
    "                float sS  = 0.5 + 0.5 * sin((sC - 1.0) * 3.14159);\n"
    "                float sW  = 0.5 + 0.5 * sin((wC - 1.0) * 3.14159);\n"
    "                float sE  = 0.5 + 0.5 * sin((eC - 1.0) * 3.14159);\n"
    "                float gx = (sE - sW);\n"
    "                float gy = (sS - sN);\n"
    "                float mag = sqrt(gx*gx + gy*gy);\n"
    "                if (mag > 0.01) {\n"
    "                    float nx = -gx, ny = -gy, nz = 1.0;\n"
    "                    float nm = sqrt(nx*nx + ny*ny + nz*nz);\n"
    "                    float light = (nx*(-0.5) + ny*(-0.5) + nz*0.707) / nm;\n"
    "                    light = clamp(0.5 + light * 0.8, 0.3, 1.5);\n"
    "                    color *= light;\n"
    "                }\n"
    "            } else {\n"
    "                // Continuous Sobel gradient from slopeTex\n"
    "                float vNN = texture2D(slopeTex, vUV + vec2(0.0, -2.0*ts.y)).r;\n"
    "                float vSS = texture2D(slopeTex, vUV + vec2(0.0,  2.0*ts.y)).r;\n"
    "                float vWW = texture2D(slopeTex, vUV + vec2(-2.0*ts.x, 0.0)).r;\n"
    "                float vEE = texture2D(slopeTex, vUV + vec2( 2.0*ts.x, 0.0)).r;\n"
    "                float vNW = texture2D(slopeTex, vUV + vec2(-ts.x, -ts.y)).r;\n"
    "                float vNE = texture2D(slopeTex, vUV + vec2( ts.x, -ts.y)).r;\n"
    "                float vSW = texture2D(slopeTex, vUV + vec2(-ts.x,  ts.y)).r;\n"
    "                float vSE = texture2D(slopeTex, vUV + vec2( ts.x,  ts.y)).r;\n"
    "                float gx = (vEE + 2.0*vE + vNE + vSE - vWW - 2.0*vW - vNW - vSW) / 8.0;\n"
    "                float gy = (vSS + 2.0*vS + vSW + vSE - vNN - 2.0*vN - vNW - vNE) / 8.0;\n"
    "                gx *= (dataRange / 9.0);\n"
    "                gy *= (dataRange / 9.0);\n"
    "                float mag = sqrt(gx*gx + gy*gy);\n"
    "                if (mag > 0.0001) {\n"
    "                    float nx = -gx, ny = -gy, nz = 1.0;\n"
    "                    float nm = sqrt(nx*nx + ny*ny + nz*nz);\n"
    "                    float light = (nx*(-0.5) + ny*(-0.5) + nz*0.707) / nm;\n"
    "                    light = clamp(0.5 + light * 0.8, 0.3, 1.5);\n"
    "                    color *= light;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = vec4(color, 1.0);\n"
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
    s_uniforms.colorLUT = fpGetUniformLocation(s_program, "colorLUT");
    s_uniforms.slopeTex = fpGetUniformLocation(s_program, "slopeTex");
    s_uniforms.texSize = fpGetUniformLocation(s_program, "texSize");
    s_uniforms.mode = fpGetUniformLocation(s_program, "mode");
    s_uniforms.sCurve = fpGetUniformLocation(s_program, "sCurve");
    s_uniforms.slope = fpGetUniformLocation(s_program, "slope");
    s_uniforms.dataMin = fpGetUniformLocation(s_program, "dataMin");
    s_uniforms.dataMax = fpGetUniformLocation(s_program, "dataMax");
    s_uniforms.slopeMode = fpGetUniformLocation(s_program, "slopeMode");

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

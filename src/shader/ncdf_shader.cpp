//===================================================================
// ncdf_shader.cpp — Bicubic interpolation shader framework
// Runtime GL extension loading for OpenGL 2.0+ shader support
//===================================================================

#include "shader/ncdf_shader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <wx/log.h>

extern void ncdfLog(const char* format, ...);

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
typedef void (APIENTRY *PFN_UNIFORM4F)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
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
static PFN_UNIFORM4F fpUniform4f = NULL;
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
    fpUniform4f = (PFN_UNIFORM4F)load_gl("glUniform4f");
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
    "uniform int landMode;\n"
    "uniform sampler2D landMaskTex;\n"
    "uniform sampler2D landMaskTex2;\n"
    "uniform vec4 dataGeo;\n"
    "uniform vec4 landGeoC;\n"
    "uniform vec4 landGeoF;\n"
    "uniform float dbgFill;\n"
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
    "    float ca = texture2D(dataTex, vUV).a;\n"
    "    if (ca < 0.001) {\n"
    "        // ca==0: border/outside -> fully transparent\n"
    "        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 landCol = vec3(0.667, 0.686, 0.314);\n"
    "    // landMode == 2: dedicated high-resolution land pass. dataTex is bound\n"
    "    // to the land mask texture here (1 byte per mask cell, GL_NEAREST), so\n"
    "    // each fragment is judged AGAINST A MASK CELL -- the coastline is as\n"
    "    // fine as the mask, not as coarse as the data grid.\n"
    "    if (landMode == 2) {\n"
    "        float lf = texture2D(dataTex, vUV).r;\n"
    "        gl_FragColor = (lf > 0.5) ? vec4(landCol, 1.0) : vec4(0.0, 0.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // ================= Stage 1: sea colours =================\n"
    "    // Decided PER DATA GRID CELL only (the A channel = does this cell have\n"
    "    // data). The land mask plays NO part here, so green can never leak into\n"
    "    // the colour field -- that was what made land appear inside the sea.\n"
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
    "    // ============ Stage 2: land fallback (B channel) ============\n"
    "    // Only used where the dedicated high-res land texture does not cover.\n"
    "    // landFrac is the per-cell land fraction interpolated so the contour is\n"
    "    // smooth instead of a cell staircase. The test is a SELECTION, not a\n"
    "    // mix: a fragment is fully land or fully sea, never in between.\n"
    "    float l00=s00.b, l10=s10.b, l01=s01.b, l11=s11.b;\n"
    "    float landFrac = l00*(1.0-f.x)*(1.0-f.y) + l10*f.x*(1.0-f.y)\n"
    "                   + l01*(1.0-f.x)*f.y       + l11*f.x*f.y;\n"
    "\n"
    "    // ===== landMode == 4: sub-cell gap fill =====\n"
    "    // The ocean model is coarse, GSHHG is fine, so a blank band always sits\n"
    "    // between the drawn coastline and the edge of the colour field. This pass\n"
    "    // colours that band with the colour of the NEAREST sea point -- a plain\n"
    "    // point sample, never a blend across land. It paints exactly where the\n"
    "    // sea-colour pass above left a hole. It is drawn AFTER the GSHHG land\n"
    "    // layers; land is drawn once more afterwards so it can never be covered.\n"
    "    if (landMode == 4) {\n"
    "        bool seaPainted = (land2x2 == 0) && (landFrac <= 0.5);\n"
    "        if (seaPainted) { gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0); return; }\n"
    "        // Never paint where GSHHG says land. The fill must not rely on the\n"
    "        // land pass covering it afterwards -- if that coverage is ever short,\n"
    "        // sea colour is pushed out past the coastline as an extra ring.\n"
    "        // Land test via LON/LAT, not via a data-UV affine. The data texture is\n"
    "        // GL_REPEAT for global sets, so vUV.x wraps 1->0 at the antimeridian;\n"
    "        // an affine straight off vUV is NOT periodic in u and therefore jumps by\n"
    "        // a whole mask width there, flipping the land test along a hard vertical\n"
    "        // edge at the date line (and reporting deep land as sea). Going through\n"
    "        // longitude and normalising it to [-180,180) removes the wrap entirely.\n"
    "        float dLon = dataGeo.x * vUV.x + dataGeo.y;\n"
    "        float dLat = dataGeo.z * vUV.y + dataGeo.w;\n"
    "        dLon = -180.0 + mod(dLon + 180.0, 360.0);\n"
    "        vec2 mUvC = vec2(dLon * landGeoC.x + landGeoC.y,\n"
    "                         dLat * landGeoC.z + landGeoC.w);\n"
    "        vec2 mUvF = vec2(dLon * landGeoF.x + landGeoF.y,\n"
    "                         dLat * landGeoF.z + landGeoF.w);\n"
    "        if (texture2D(landMaskTex,  mUvC).r > 0.5 ||\n"
    "            texture2D(landMaskTex2, mUvF).r > 0.5) {\n"
    "            gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0); return;\n"
    "        }\n"
    "        vec4 t = texture2D(physicalTex, (tc+vec2(0.5,0.5))*ts);\n"
    "        // a = this texel has its own data, b = nearest-propagated value.\n"
    "        // Neither -> no source sea colour anywhere near (inland water the\n"
    "        // model never covered). Do not invent a colour and do not leave the\n"
    "        // chart showing through: paint the OpenCPN seabed colour instead.\n"
    "        if (t.a <= 0.003 && t.b <= 0.003) {\n"
    "            gl_FragColor = vec4(0.66275, 0.76078, 0.93725, 1.0);   // #A9C2EF\n"
    "            return;\n"
    "        }\n"
    "        // dbgFill: show where the fill really WOULD paint (after the source\n"
    "        // check above, so inland no-data water is not falsely shown as filled).\n"
    "        if (dbgFill > 0.5) { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); return; }\n"
    "        float fu = physMin + t.r * (physMax - physMin);\n"
    "        float fv = physMinV + t.g * (physMaxV - physMinV);\n"
    "        float shown = (vectorMode == 1) ? sqrt(fu*fu + fv*fv) : fu;\n"
    "        float lrng = lutMax - lutMin;\n"
    "        float fres = (lrng > 0.0001) ? clamp((shown - lutMin) / lrng, 0.0, 1.0) : 0.0;\n"
    "        gl_FragColor = vec4(texture2D(colorLUT, vec2(fres, 0.5)).rgb, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // landMode 0/1: no high-res land layer covering this area, so decide\n"
    "    // here from the per-cell flag (1 = also paint land, 0 = hide it).\n"
    "    // landMode 3: the land layers cover the land, so draw the colour\n"
    "    // everywhere and let them paint over it -- no pollution, no holes.\n"
    "    if ((landMode == 0 || landMode == 1) && landFrac > 0.5) {\n"
    "        gl_FragColor = (landMode == 1) ? vec4(landCol, 1.0) : vec4(0.0, 0.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
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
    "    } else {\n"
    "        // 2x2 has land: SKIP this fragment (2026-10-27).\n"
    "        // Requirement: if ANY texel of the 2x2 neighbourhood is land, produce\n"
    "        // no value here at all. No bilinear blend (it mixes values across the\n"
    "        // shoreline) and no nearest-neighbour substitute either -- just leave\n"
    "        // the pixel transparent so the chart shows through.\n"
    "        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // Sea side but no model data here -> leave transparent (chart shows).\n"
    "    if(wSum<=0.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0); return; }\n"
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
    "    vec3 oceanCol = texture2D(colorLUT, vec2(result, 0.5)).rgb;\n"
    "    gl_FragColor = vec4(oceanCol, 1.0);\n"
    "}\n";

//===================================================================
// Shader compilation and linking
//===================================================================
static GLuint compile_shader_gl(GLenum type, const char* src) {
    GLuint s = fpCreateShader(type);
    if (!s) return 0;
    // GLSL 源字符集只允许 ASCII（GLSL 1.20 §1.1）。本工程源码用中文注释，
    // 一旦非 ASCII 字节混进着色器字符串，NVIDIA 编译器会解析错乱并报
    // "undefined variable"（2026-10-26：颜色图整体不显示）。这里做兜底消毒：
    // 每个 >=0x80 的字节换成空格，语义只可能出现在注释里，换成空格无害。
    std::string clean;
    clean.reserve(strlen(src));
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p)
        clean.push_back(*p < 0x80 ? (char)*p : ' ');
    const char *psrc = clean.c_str();

    fpShaderSource(s, 1, &psrc, NULL);
    fpCompileShader(s);
    GLint st;
    fpGetShaderiv(s, GL_COMPILE_STATUS, &st);
    if (!st) {
        char log[512];
        fpGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[shader] compile error: %s\n", log);
        wxLogMessage(_T("[shader] compile error: %s"), wxString(log, wxConvUTF8));
        ncdfLog("[shader] compile error: %s\n", log);
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
    s_uniforms.landMode = fpGetUniformLocation(s_program, "landMode");
    s_uniforms.landMaskTex = fpGetUniformLocation(s_program, "landMaskTex");
    s_uniforms.landMaskTex2 = fpGetUniformLocation(s_program, "landMaskTex2");
    s_uniforms.dataGeo = fpGetUniformLocation(s_program, "dataGeo");
    s_uniforms.landGeoC = fpGetUniformLocation(s_program, "landGeoC");
    s_uniforms.landGeoF = fpGetUniformLocation(s_program, "landGeoF");
    s_uniforms.dbgFill = fpGetUniformLocation(s_program, "dbgFill");

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
void ncdf_shader_uniform_4f(int loc, float x, float y, float z, float w) {
    if (fpUniform4f && loc >= 0) fpUniform4f(loc, x, y, z, w);
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

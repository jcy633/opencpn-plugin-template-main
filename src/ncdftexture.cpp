//===================================================================
// Auto-split from ncdfoverlayfactory.cpp
//===================================================================

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"
#include "shader/ncdf_shader.h"

#ifndef PI
#define PI 3.14159265
#endif

#include "IsoLine2.h"

//===================================================================
// Color texture (shared via RenderGridOverlay)
//===================================================================
void ncdfOverlayFactory::DeleteColorTexture()
{
    if (m_bHasColorTexture && m_glColorTexture) {
        glDeleteTextures(1, &m_glColorTexture);
        m_glColorTexture = 0;
        m_bHasColorTexture = false;
    }
}

//===================================================================
// Sea Temperature rendering
//===================================================================

// Sea temperature color map (-2°C to 32°C, purple→red, smoothstep)
wxColour ncdfOverlayFactory::InterpolateStops(const double stops[][4], int nStops, double val, bool smooth)
{
    if (val >= stops[nStops - 1][0])
        return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
    if (val <= stops[0][0])
        return wxColour((unsigned char)stops[0][1], (unsigned char)stops[0][2], (unsigned char)stops[0][3]);

    for (int i = 1; i < nStops; i++) {
        if (val <= stops[i][0]) {
            double range = stops[i][0] - stops[i-1][0];
            double t = (range > 0) ? (val - stops[i-1][0]) / range : 0;
            if (smooth) t = t * t * (3.0 - 2.0 * t);
            unsigned char r = (unsigned char)(stops[i-1][1] + t * (stops[i][1] - stops[i-1][1]));
            unsigned char g = (unsigned char)(stops[i-1][2] + t * (stops[i][2] - stops[i-1][2]));
            unsigned char b = (unsigned char)(stops[i-1][3] + t * (stops[i][3] - stops[i-1][3]));
            return wxColour(r, g, b);
        }
    }
    return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
}

wxColour ncdfOverlayFactory::GetSeaTempGraphicColor(double temp_c)
{
    static const double stops[][4] = {
        {-2, 0x80, 0x00, 0xc0}, { 2, 0x40, 0x30, 0xff}, { 7, 0x00, 0x90, 0xfa},
        {12, 0x00, 0xd8, 0xb0}, {17, 0x10, 0xbb, 0x20}, {22, 0x90, 0xd0, 0x00},
        {26, 0xf0, 0xd0, 0x00}, {30, 0xf0, 0x70, 0x00}, {32, 0xff, 0x00, 0x00},
    };
    return InterpolateStops(stops, 9, temp_c, m_currentSmoothColors);
}

wxColour ncdfOverlayFactory::GetSalinityGraphicColor(double sal_psu)
{
    static const double stops[][4] = {
        {30.0, 0x87, 0xCE, 0xEB}, {31.0, 0x60, 0xB0, 0xE0}, {32.0, 0x40, 0x90, 0xD0},
        {33.0, 0x20, 0x70, 0xC0}, {34.0, 0x10, 0x50, 0xA0}, {35.0, 0x00, 0x80, 0x80},
        {35.5, 0x20, 0xA0, 0x70}, {36.0, 0x40, 0xC0, 0x60}, {36.5, 0x80, 0xC0, 0x40},
        {37.0, 0xC0, 0xB0, 0x20}, {37.5, 0xE0, 0xA0, 0x10}, {38.0, 0xE0, 0x70, 0x20},
        {38.5, 0xD0, 0x40, 0x20}, {39.0, 0xC0, 0x20, 0x20},
    };
    return InterpolateStops(stops, 14, sal_psu, m_currentSmoothColors);
}

void ncdfOverlayFactory::DeleteSalinityTexture()
{
    if (m_bHasSalinityTexture && m_glSalinityTexture) {
        glDeleteTextures(1, &m_glSalinityTexture);
        m_glSalinityTexture = 0;
        m_bHasSalinityTexture = false;
    }
}

void ncdfOverlayFactory::DeleteSeaTempTexture()
{
    if (m_bHasSeaTempTexture && m_glSeaTempTexture) {
        glDeleteTextures(1, &m_glSeaTempTexture);
        m_glSeaTempTexture = 0;
        m_bHasSeaTempTexture = false;
    }
}

//===================================================================
// Shared grid overlay renderer (SST & Salinity)
//===================================================================
void ncdfOverlayFactory::RenderGridOverlay(PlugIn_ViewPort *vp,
                                           double **grid,
                                           ColorFunc colorFunc,
                                           GLuint &texID, bool &hasTex, bool &needsRebuild,
                                           int dataDim[2], int glDim[2])
{
    if (!gui || !vp || !grid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    int tw = 0, th = 0;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        // Lazy texture rebuild (flagged by setData, safe in GL context)
        if (needsRebuild) {
            if (texID) { glDeleteTextures(1, &texID); texID = 0; }
            hasTex = false;
            needsRebuild = false;
        }

        // Create texture once per data change (GRIB pattern)
        if (!hasTex) {
            double lonMin = tlon, lonMax = blon;
            double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
            bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);

            int borderH = repeat ? 0 : 1;
            int extraCol = repeat ? 1 : 0;
            tw = ni + 2 * borderH + extraCol; th = nj + 2;
            unsigned char *texData = new(std::nothrow) unsigned char[tw * th * 4];
            if (!texData) return;
            memset(texData, 0, tw * th * 4);

            if (m_useShader) {
                // --- Shader path: normalized data in R channel, alpha=validity ---
                double dMin = 1e30, dMax = -1e30;
                for (int j = 0; j < nj; j++) {
                    if (!grid[j]) continue;
                    for (int i = 0; i < ni; i++) {
                        double v = grid[j][i];
                        if (v != ncdf_NOTDEF && !isnan(v) && isfinite(v)) {
                            if (v < dMin) dMin = v;
                            if (v > dMax) dMax = v;
                        }
                    }
                }
                if (dMax <= dMin) { dMin = 0; dMax = 1; }
                double dRange = dMax - dMin;
                if (dRange < 1e-10) dRange = 1.0;

                for (int j = 0; j < nj; j++) {
                    if (!grid[j]) break;
                    int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                    for (int i = 0; i < ni; i++) {
                        double val = grid[j][i];
                        int x = i + borderH, y = texRow + 1;
                        if (x >= tw - 1 || y >= th - 1) continue;
                        int off = 4 * (y * tw + x);
                        if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) {
                            texData[off] = 0; texData[off+3] = 0;
                        } else {
                            double normalized = (val - dMin) / dRange;
                            texData[off] = (unsigned char)(fmin(fmax(normalized, 0.0), 1.0) * 255.0);
                            texData[off+1] = 0; texData[off+2] = 0;
                            texData[off+3] = 255;
                        }
                    }
                }

                // Color LUT (256 entries)
                if (m_bHasColorLUT && m_glColorLUT) {
                    glDeleteTextures(1, &m_glColorLUT);
                    m_glColorLUT = 0; m_bHasColorLUT = false;
                }
                unsigned char lutData[256 * 4];
                for (int k = 0; k < 256; k++) {
                    double val = dMin + (k / 255.0) * dRange;
                    wxColour c = (this->*colorFunc)(val);
                    lutData[k*4] = c.Red(); lutData[k*4+1] = c.Green();
                    lutData[k*4+2] = c.Blue(); lutData[k*4+3] = 255;
                }
                glGenTextures(1, &m_glColorLUT);
                glBindTexture(GL_TEXTURE_2D, m_glColorLUT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
                m_bHasColorLUT = true;

            } else {
                // --- Fixed-function path: colored RGBA texture ---
                unsigned char alpha = 255;
                for (int j = 0; j < nj; j++) {
                    if (!grid[j]) break;
                    int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                    for (int i = 0; i < ni; i++) {
                        double val = grid[j][i];
                        if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) continue;
                        int x = i + borderH, y = texRow + 1;
                        if (x >= tw - 1 || y >= th - 1) continue;
                        wxColour c = (this->*colorFunc)(val);
                        int off = 4 * (y * tw + x);
                        texData[off]     = c.Red();
                        texData[off + 1] = c.Green();
                        texData[off + 2] = c.Blue();
                        texData[off + 3] = alpha;
                    }
                }
            }

            // Repeat mode: duplicate first column at end for seamless wrapping
            if (repeat) {
                for (int j = 0; j < nj; j++) {
                    int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                    int y = texRow + 1;
                    if (y >= th - 1) continue;
                    memcpy(texData + 4 * (y * tw + ni), texData + 4 * (y * tw + 0), 4);
                }
            }

            // GRIB-style border: copy adjacent row/col, then set border alpha=0
            memcpy(texData, texData + 4 * tw, 4 * tw);
            memcpy(texData + 4 * tw * (th - 1), texData + 4 * tw * (th - 2), 4 * tw);
            for (int x = 0; x < tw; x++) {
                texData[4 * x + 3] = 0;
                texData[4 * ((th - 1) * tw + x) + 3] = 0;
            }
            if (!repeat) {
                for (int y = 0; y < th; y++) {
                    memcpy(texData + 4 * y * tw, texData + 4 * (y * tw + 1), 4);
                    memcpy(texData + 4 * (y * tw + tw - 1), texData + 4 * (y * tw + tw - 2), 4);
                }
                for (int y = 0; y < th; y++) {
                    texData[4 * y * tw + 3] = 0;
                    texData[4 * (y * tw + tw - 1) + 3] = 0;
                }
            }

            glGenTextures(1, &texID);
            glBindTexture(GL_TEXTURE_2D, texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            GLenum filter = m_useShader ? GL_NEAREST : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
            delete[] texData;

            hasTex = true;
            dataDim[0] = tw; dataDim[1] = th;
            glDim[0] = tw;   glDim[1] = th;
        }

        // Draw tiled texture (GRIB pattern)
        if (hasTex && texID) {
            tw = glDim[0]; th = glDim[1];
            double potNormX = (double)dataDim[0] / tw;
            double potNormY = (double)dataDim[1] / th;
            double lat_min = blat, lon_min = tlon;
            double lat_max = tlat, lon_max = blon;
            double latstep = fabs(lat_max - lat_min) / (nj - 1);
            double lonstep = (lon_max - lon_min) / (ni - 1);
            if (latstep < 1e-10 || lonstep < 1e-10) return;
            double clon = (lon_min + lon_max) / 2;

            bool repeat = (lon_max - lon_min + lonstep >= 360);

            // Tile grid for non-linear projection handling
            double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
            if (pw < 20) pw = 20;
            int xs = (int)ceil(vp->pix_width / pw);
            int ys = (int)ceil(vp->pix_height / pw);
            if (vp->rotation == 0) xs = 1;
            if (xs < 2) xs = 2; if (ys < 2) ys = 2;
            if (xs > 16) xs = 16; if (ys > 16) ys = 16;
            int gridW = xs + 1, gridH = ys + 1;

            double *lva = new(std::nothrow) double[gridW * gridH * 2];
            if (!lva) return;

            for (int i = 0; i < gridW; i++) {
                double px = vp->pix_width / (double)xs * i;
                for (int j = 0; j < gridH; j++) {
                    double py = vp->pix_height / (double)ys * j;
                    double lat, lon;
                    wxPoint pt((int)px, (int)py);
                    GetCanvasLLPix(vp, pt, &lat, &lon);
                    if (!repeat) {
                        if (clon - lon > 180) lon += 360;
                        else if (lon - clon > 180) lon -= 360;
                    }
                    int idx = (i * gridH + j) * 2;
                    lva[idx]     = ((lon - lon_min) / lonstep - repeat + 1.5) / tw * potNormX;
                    lva[idx + 1] = ((lat - lat_min) / latstep + 1.5) / th * potNormY;
                }
            }

            // === Shader bicubic path ===
            if (m_useShader && ncdf_shader_initialized() &&
                ncdf_shader_get_grid_program() > 0) {
                wxLogMessage(_T("[shader] drawing with Catmull-Rom bicubic, texID=%u LUT=%u"),
                             texID, m_glColorLUT);
                ncdf_shader_use_program(ncdf_shader_get_grid_program());
                NcdfShaderUniforms u = ncdf_shader_get_uniforms();
                if (u.texSize >= 0) ncdf_shader_uniform_2f(u.texSize, (float)tw, (float)th);
                // mode: 0=linear scalar, 1=bicubic, 2=monotone bicubic
                int shaderMode = (m_currentInterpMode >= 2) ? m_currentInterpMode - 1 : 0;
                if (u.mode >= 0) ncdf_shader_uniform_1i(u.mode, shaderMode);
                if (u.dataTex >= 0) {
                    ncdf_shader_active_texture(0x84C0);
                    glBindTexture(GL_TEXTURE_2D, texID);
                    ncdf_shader_uniform_1i(u.dataTex, 0);
                }
                if (u.colorLUT >= 0 && m_bHasColorLUT) {
                    ncdf_shader_active_texture(0x84C1);
                    glBindTexture(GL_TEXTURE_2D, m_glColorLUT);
                    ncdf_shader_uniform_1i(u.colorLUT, 1);
                }

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                double xS = vp->pix_width / (double)xs;
                double yS = vp->pix_height / (double)ys;
                for (int i = 0; i < xs; i++) {
                    for (int j = 0; j < ys; j++) {
                        int i00 = (i * gridH + j) * 2;
                        int i10 = ((i+1) * gridH + j) * 2;
                        int i11 = ((i+1) * gridH + j+1) * 2;
                        int i01 = (i * gridH + j+1) * 2;
                        double u0=lva[i00], u1=lva[i10], u2=lva[i11], u3=lva[i01];
                        double v0=lva[i00+1], v1=lva[i10+1], v2=lva[i11+1], v3=lva[i01+1];

                        if (repeat) {
                            if (u1 - u0 > .5) u1--;
                            else if (u0 - u1 > .5) u1++;
                            if (u2 - u0 > .5) u2--;
                            else if (u0 - u2 > .5) u2++;
                            if (u3 - u0 > .5) u3--;
                            else if (u0 - u3 > .5) u3++;
                        }

                        if (!((repeat ||
                               ((u0>=0||u1>=0||u2>=0||u3>=0)&&(u0<=1||u1<=1||u2<=1||u3<=1))) &&
                              (v0>=0||v1>=0||v2>=0||v3>=0)&&(v0<=1||v1<=1||v2<=1||v3<=1))) continue;
                        if (u1 <= u0) continue;
                        double x = xS * i, y = yS * j;
                        GLfloat verts[8] = {(float)x,(float)y, (float)(x+xS),(float)y,
                                            (float)(x+xS),(float)(y+yS), (float)x,(float)(y+yS)};
                        GLfloat uvs[8] = {(float)u0,(float)v0, (float)u1,(float)v1,
                                          (float)u2,(float)v2, (float)u3,(float)v3};
                        ncdf_shader_enable_attrib(0);
                        ncdf_shader_enable_attrib(1);
                        ncdf_shader_attrib_pointer(0, 2, 0, verts);
                        ncdf_shader_attrib_pointer(1, 2, 0, uvs);
                        glDrawArrays(GL_QUADS, 0, 4);
                        ncdf_shader_disable_attrib(0);
                        ncdf_shader_disable_attrib(1);
                    }
                }

                // Restore fixed-function state
                ncdf_shader_use_program(0);
                ncdf_shader_active_texture(0x84C0);
                glDisable(GL_BLEND);
                delete[] lva;
                return;
            }

            // === Fixed-function fallback path ===
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texID);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glColor4f(1, 1, 1, 1);

            double xS = vp->pix_width / (double)xs;
            double yS = vp->pix_height / (double)ys;
            for (int i = 0; i < xs; i++) {
                for (int j = 0; j < ys; j++) {
                    int i00 = (i * gridH + j) * 2;
                    int i10 = ((i+1) * gridH + j) * 2;
                    int i11 = ((i+1) * gridH + j+1) * 2;
                    int i01 = (i * gridH + j+1) * 2;
                    double u0=lva[i00], u1=lva[i10], u2=lva[i11], u3=lva[i01];
                    double v0=lva[i00+1], v1=lva[i10+1], v2=lva[i11+1], v3=lva[i01+1];

                    if (repeat) {
                        if (u1 - u0 > .5) u1--;
                        else if (u0 - u1 > .5) u1++;
                        if (u2 - u0 > .5) u2--;
                        else if (u0 - u2 > .5) u2++;
                        if (u3 - u0 > .5) u3--;
                        else if (u0 - u3 > .5) u3++;
                    }

                    if (!((repeat ||
                           ((u0>=0||u1>=0||u2>=0||u3>=0)&&(u0<=1||u1<=1||u2<=1||u3<=1))) &&
                          (v0>=0||v1>=0||v2>=0||v3>=0)&&(v0<=1||v1<=1||v2<=1||v3<=1))) continue;
                    if (u1 <= u0) continue;
                    double x = xS * i, y = yS * j;
                    glBegin(GL_QUADS);
                    glTexCoord2d(u0,v0); glVertex2f((float)x,(float)y);
                    glTexCoord2d(u1,v1); glVertex2f((float)(x+xS),(float)y);
                    glTexCoord2d(u2,v2); glVertex2f((float)(x+xS),(float)(y+yS));
                    glTexCoord2d(u3,v3); glVertex2f((float)x,(float)(y+yS));
                    glEnd();
                }
            }
            delete[] lva;

            glDisable(GL_BLEND);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
#endif
    }
}

void ncdfOverlayFactory::RenderSeaTempOverlay(PlugIn_ViewPort *vp)
{
    RenderGridOverlay(vp, gui ? gui->gridSST : NULL,
                      &ncdfOverlayFactory::GetSeaTempGraphicColor,
                      m_glSeaTempTexture, m_bHasSeaTempTexture, m_bNeedsSeaTempTexRebuild,
                      m_sstTexDataDim, m_sstTexGLDim);
}

void ncdfOverlayFactory::RenderSalinityOverlay(PlugIn_ViewPort *vp)
{
    RenderGridOverlay(vp, gui ? gui->gridSalinity : NULL,
                      &ncdfOverlayFactory::GetSalinityGraphicColor,
                      m_glSalinityTexture, m_bHasSalinityTexture, m_bNeedsSalinityTexRebuild,
                      m_salTexDataDim, m_salTexGLDim);
}

void ncdfOverlayFactory::RenderSeaTempIsoLines(PlugIn_ViewPort *vp)
{
    if (!gui || !vp) return;
    double **sstGrid = gui->gridSST;
    if (!sstGrid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // Rebuild cached isolines only when data changes (GRIB pattern)
    if (m_bNeedsIsoRebuild) {
        m_isoSegments.clear();

        double minT = 1e10, maxT = -1e10;
        for (int j = 0; j < nj; j++) {
            if (!sstGrid[j]) break;
            for (int i = 0; i < ni; i++) {
                double v = sstGrid[j][i];
                if (v == ncdf_NOTDEF || isnan(v) || !isfinite(v)) continue;
                if (v < minT) minT = v;
                if (v > maxT) maxT = v;
            }
        }

        if (minT < maxT) {
            double spacing = 1.0;
            minT = floor(minT / spacing) * spacing;
            maxT = ceil(maxT / spacing) * spacing;
            double lat_max = tlat, lon_min = tlon;
            double incrLat = (lat_max - blat) / (nj - 1);
            if (gui->myMessage.jDirectionIncr < 0) incrLat = -incrLat;
            double incrLon = (blon - lon_min) / (ni - 1);

            for (double temp = minT; temp <= maxT; temp += spacing) {
                IsoLine isoLine(temp, sstGrid, nj, ni, lat_max, lon_min, incrLat, incrLon);
                std::list<Segment*>& trace = isoLine.getTrace();
                for (std::list<Segment*>::iterator it = trace.begin(); it != trace.end(); ++it) {
                    Segment *seg = *it;
                    IsoSeg s = {seg->py1, seg->px1, seg->py2, seg->px2};
                    m_isoSegments.push_back(s);
                }
            }
            wxLogMessage(_T("[iso] cached %d segments"), (int)m_isoSegments.size());
        }
        m_bNeedsIsoRebuild = false;
    }

    if (m_isoSegments.empty()) return;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(1.0f);
        glColor4ub(80, 80, 80, 220);

        glBegin(GL_LINES);
        for (size_t k = 0; k < m_isoSegments.size(); k++) {
            const IsoSeg& s = m_isoSegments[k];
            wxPoint ab, cd;
            GetCanvasPixLL(vp, &ab, s.lat1, s.lon1);
            GetCanvasPixLL(vp, &cd, s.lat2, s.lon2);
            if (fabs((double)ab.x - cd.x) > vp->pix_width / 2) continue;
            glVertex2i(ab.x, ab.y);
            glVertex2i(cd.x, cd.y);
        }
        glEnd();

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#endif
    }
}

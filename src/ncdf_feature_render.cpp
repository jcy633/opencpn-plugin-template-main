// ncdf_feature_render.cpp — immediate-mode GL rendering of feature objects.
//
// Frame path = projection of cached vertices + GL drawing only (C10).
// GL discipline (C5): immediate mode only (no vertex arrays — heap-corruption
// lesson), blend/line-smooth enabled, GL state restored at exit.

#include <cmath>
#include <vector>
#include <algorithm>
#include <wx/wx.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include "ocpn_plugin.h"
#include "ncdf_feature.h"
#include "ncdf_feature_render.h"
#include "ncdf_kuroshio.h"
#include "ncdf_seamt.h"
#include "ncdfoverlayfactory.h"

extern void ncdfLog(const char* format, ...);

// ---------------------------------------------------------------------------
// Text texture helper (same technique as ncdf_legend.cpp).
static GLuint renderTextToTexture(const wxString& text, int& outW, int& outH,
                                  unsigned char tr = 255, unsigned char tg = 255, unsigned char tb = 255)
{
    wxMemoryDC dc;
    wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Microsoft YaHei"));
    dc.SetFont(font);
    int tw = 0, th = 0;
    dc.GetTextExtent(text, &tw, &th);
    if (tw <= 0 || th <= 0) return 0;
    tw += 2; th += 2;
    wxBitmap bmp(tw, th, 24);
    dc.SelectObject(bmp);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    dc.SetTextForeground(*wxWHITE);
    dc.DrawText(text, 1, 1);
    dc.SelectObject(wxNullBitmap);
    wxImage img = bmp.ConvertToImage();
    int w = img.GetWidth(), h = img.GetHeight();
    std::vector<unsigned char> rgba((size_t)(w * h * 4));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t idx = (size_t)(y * w + x) * 4;
            unsigned char r = img.GetRed(x, y);
            unsigned char g = img.GetGreen(x, y);
            unsigned char b = img.GetBlue(x, y);
            rgba[idx] = tr; rgba[idx + 1] = tg; rgba[idx + 2] = tb;
            rgba[idx + 3] = (unsigned char)((r + g + b) / 3);
        }
    }
    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    outW = w; outH = h;
    return texID;
}

static void drawTexQuad(int x, int y, int w, int h, GLuint texID)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texID);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2i(x, y);
    glTexCoord2f(1, 0); glVertex2i(x + w, y);
    glTexCoord2f(1, 1); glVertex2i(x + w, y + h);
    glTexCoord2f(0, 1); glVertex2i(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// Per-row cached legend textures (rebuilt only when the row text changes)
static std::vector<GLuint> s_legendTex;
static std::vector<wxString> s_legendTxt;
static std::vector<int> s_legendW, s_legendH;

// forward decl (projectPoint is defined further below)
static bool projectPoint(PlugIn_ViewPort *vp, float lon, float lat, int &sx, int &sy);

// ---- GL-drawn popup panel (P3-prep UX): rendered like the legend, no wx
// widgets at all.  Anchored to a feature lon/lat -> follows pan/zoom for free.
static bool      s_popupValid = false;
static double    s_popupLon = 0, s_popupLat = 0;
static wxString  s_popupText;
static std::vector<GLuint> s_popTex;
static std::vector<wxString> s_popTxt;
static std::vector<int> s_popW, s_popH;

void FeatureRender::setPopup(const wxString &text, double lon, double lat)
{
    s_popupValid = true;
    s_popupText = text;
    s_popupLon = lon;
    s_popupLat = lat;
}

void FeatureRender::clearPopup()
{
    s_popupValid = false;
    s_popupText.Clear();
}

static wxString s_popupLastText;   // line-cache key (rebuild only on change)

static void drawFeaturePopup(PlugIn_ViewPort *vp)
{
    if (!s_popupValid) return;
    static std::vector<wxString> lines;
    static int maxW = 140;
    if (s_popupLastText != s_popupText) {
        // rebuild cached line textures only when the panel text changes
        s_popupLastText = s_popupText;
        lines.clear();
        wxString cur;
        for (size_t i = 0; i < s_popupText.length(); i++) {
            wxChar c = s_popupText[i];
            if (c == '\n') { lines.push_back(cur); cur.Clear(); }
            else cur += c;
        }
        lines.push_back(cur);
        size_t n = lines.size();
        if (s_popTex.size() < n) { s_popTex.resize(n, 0); s_popTxt.resize(n); s_popW.resize(n, 0); s_popH.resize(n, 0); }
        maxW = 140;
        for (size_t r = 0; r < n; r++) {
            if (s_popTxt[r] != lines[r]) {
                if (s_popTex[r]) { glDeleteTextures(1, &s_popTex[r]); s_popTex[r] = 0; }
                s_popTex[r] = renderTextToTexture(lines[r], s_popW[r], s_popH[r], 0, 0, 0);
                s_popTxt[r] = lines[r];
            }
            if (s_popW[r] > maxW) maxW = s_popW[r];
        }
    }
    size_t n = lines.size();
    int boxW = maxW + 16, rowH = 20;
    int boxH = (int)n * rowH + 12;
    int ax, ay;
    if (!projectPoint(vp, (float)s_popupLon, (float)s_popupLat, ax, ay)) return;
    int x0 = ax + 14, y0 = ay + 14;
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 0.94f, 0.93f);
    glBegin(GL_QUADS);
    glVertex2i(x0, y0); glVertex2i(x0 + boxW, y0);
    glVertex2i(x0 + boxW, y0 + boxH); glVertex2i(x0, y0 + boxH);
    glEnd();
    glColor4f(0.2f, 0.2f, 0.2f, 0.95f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(x0, y0); glVertex2i(x0 + boxW, y0);
    glVertex2i(x0 + boxW, y0 + boxH); glVertex2i(x0, y0 + boxH);
    glEnd();
    for (size_t r = 0; r < n; r++) {
        if (!s_popTex[r]) continue;
        drawTexQuad(x0 + 8, y0 + 6 + (int)r * rowH, s_popW[r], s_popH[r], s_popTex[r]);
    }
}

static void drawFeatureLegend(const FeatureSet &fs, int screenW)
{
    static const char *names[4] = {"海流特征", "密度锋", "中尺度涡", "强流轴"};
    int counts[4] = {
        -1,
        fs.detected[FK_FRONT],
        fs.detected[FK_EDDY],
        fs.detected[FK_JET]
    };
    wxString rows[4];
    for (int r = 0; r < 4; r++)
        rows[r] = (r == 0) ? wxString(names[r])
                           : wxString::Format(_T("%s  %d"), wxString(names[r]), counts[r]);

    const int ROWS = 4;
    if (s_legendTex.size() < (size_t)ROWS) {
        s_legendTex.resize((size_t)ROWS, 0);
        s_legendTxt.resize((size_t)ROWS);
        s_legendW.resize((size_t)ROWS, 0);
        s_legendH.resize((size_t)ROWS, 0);
    }
    for (int r = 0; r < ROWS; r++) {
        if (s_legendTxt[(size_t)r] != rows[r]) {
            if (s_legendTex[(size_t)r]) { glDeleteTextures(1, &s_legendTex[(size_t)r]); s_legendTex[(size_t)r] = 0; }
            s_legendTex[(size_t)r] = renderTextToTexture(rows[r], s_legendW[(size_t)r], s_legendH[(size_t)r]);
            s_legendTxt[(size_t)r] = rows[r];
        }
    }
    int boxW = 130, rowH = 22;
    int boxH = ROWS * rowH + 10;
    int x0 = screenW - boxW - 12;
    int y0 = 100;   // below the toolbar corner area

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.08f, 0.10f, 0.14f, 0.72f);
    glBegin(GL_QUADS);
    glVertex2i(x0, y0); glVertex2i(x0 + boxW, y0);
    glVertex2i(x0 + boxW, y0 + boxH); glVertex2i(x0, y0 + boxH);
    glEnd();
    glColor4f(0.8f, 0.85f, 0.9f, 0.9f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(x0, y0); glVertex2i(x0 + boxW, y0);
    glVertex2i(x0 + boxW, y0 + boxH); glVertex2i(x0, y0 + boxH);
    glEnd();
    for (int r = 0; r < ROWS; r++) {
        if (!s_legendTex[(size_t)r]) continue;
        drawTexQuad(x0 + 8, y0 + 6 + r * rowH, s_legendW[(size_t)r], s_legendH[(size_t)r], s_legendTex[(size_t)r]);
    }
}

// ---------------------------------------------------------------------------
// Ear-clipping triangulation for simple polygons (no holes). Fixes concave
// polygon fill overflow of the old centroid fan (v1.1).
static bool triangulatePoly(const float *xy, int n, std::vector<int> &tris)
{
    if (n < 3) return false;
    tris.clear();
    std::vector<int> idx((size_t)n);
    for (int i = 0; i < n; i++) idx[(size_t)i] = i;
    double area = 0;
    for (int a = 0, b = n - 1; a < n; b = a++)
        area += (double)xy[2 * b] * xy[2 * a + 1] - (double)xy[2 * a] * xy[2 * b + 1];
    if (area < 0) std::reverse(idx.begin(), idx.end());
    int guard = 0;
    while (idx.size() > 3 && guard++ < 4 * n) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int i0 = idx[(i + idx.size() - 1) % idx.size()];
            int i1 = idx[i];
            int i2 = idx[(i + 1) % idx.size()];
            double ax = xy[2 * i0], ay = xy[2 * i0 + 1];
            double bx = xy[2 * i1], by = xy[2 * i1 + 1];
            double cx = xy[2 * i2], cy = xy[2 * i2 + 1];
            double cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (cross <= 0) continue;   // reflex vertex
            bool ok = true;
            for (size_t k = 0; k < idx.size() && ok; k++) {
                int p = idx[k];
                if (p == i0 || p == i1 || p == i2) continue;
                double px = xy[2 * p], py = xy[2 * p + 1];
                double d0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                double d1 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
                double d2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
                if (d0 >= 0 && d1 >= 0 && d2 >= 0) ok = false;
            }
            if (!ok) continue;
            tris.push_back(i0); tris.push_back(i1); tris.push_back(i2);
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    return !tris.empty();
}

// Project ring + emit triangulated fill (screen coords, immediate mode).
static void fillRing(const FeatureSet &fs, const int vOff, const int vCnt,
                     PlugIn_ViewPort *vp, std::vector<int> &trisScratch)
{
    if (vCnt < 3) return;
    const float *xy = &fs.arena[(size_t)vOff];
    if (!triangulatePoly(xy, vCnt, trisScratch)) return;
    std::vector<wxPoint> sc((size_t)vCnt);
    for (int k = 0; k < vCnt; k++)
        GetCanvasPixLL(vp, &sc[(size_t)k], (double)xy[2 * k + 1], (double)xy[2 * k]);
    glBegin(GL_TRIANGLES);
    for (size_t t = 0; t < trisScratch.size(); t++) {
        const wxPoint &p = sc[(size_t)trisScratch[t]];
        glVertex2i(p.x, p.y);
    }
    glEnd();
}

// ---------------------------------------------------------------------------
static bool projectPoint(PlugIn_ViewPort *vp, float lon, float lat, int &sx, int &sy)
{
    if (lat > 84.0f) lat = 84.0f;
    if (lat < -84.0f) lat = -84.0f;
    wxPoint pt;
    GetCanvasPixLL(vp, &pt, (double)lat, (double)lon);
    sx = pt.x; sy = pt.y;
    return true;
}

// Viewport visibility test (v1.2): far-out-of-view vertices project to huge
// screen coords whose int casts overflow and wrap as phantom lines that flicker
// while panning. Cull them (with margin) before projecting.
static bool visPoint(PlugIn_ViewPort *vp, double lon, double lat)
{
    if (lat < -84.0 || lat > 84.0) return false;
    const double m = 8.0;
    if (lat < vp->lat_min - m || lat > vp->lat_max + m) return false;
    for (int s = -1; s <= 1; s += 2)
        if (lon + 360.0 * s >= vp->lon_min - m && lon + 360.0 * s <= vp->lon_max + m) return true;
    return (lon >= vp->lon_min - m && lon <= vp->lon_max + m);
}

// Front color ramp: weak pale yellow -> orange -> strong red (plan §4.2)
static void frontColor(float strength, float &r, float &g, float &b)
{
    // stops: 0.0 (1.0,1.0,0.71)  0.5 (0.94,0.63,0.16)  1.0 (0.86,0.16,0.12)
    if (strength < 0.5f) {
        float t = strength * 2.0f;
        r = 1.0f + t * (0.94f - 1.0f);
        g = 1.0f + t * (0.63f - 1.0f);
        b = 0.71f + t * (0.16f - 0.71f);
    } else {
        float t = (strength - 0.5f) * 2.0f;
        r = 0.94f + t * (0.86f - 0.94f);
        g = 0.63f + t * (0.16f - 0.63f);
        b = 0.16f + t * (0.12f - 0.16f);
    }
}

static void zoneColor(int kind, float strength,
                      float &r, float &g, float &b, float &a)
{
    switch (kind) {
    case FK_CONVERG: r = 0.94f; g = 0.55f; b = 0.16f; break;   // warm orange
    case FK_DIVERG:  r = 0.24f; g = 0.63f; b = 0.90f; break;   // cyan-blue
    default:         r = 0.90f; g = 0.78f; b = 0.24f; break;   // shear yellow
    }
    a = 0.18f + 0.14f * strength;
}

// ---------------------------------------------------------------------------
// Anchor icons (P2): small GL symbols at feature anchor points, also used as
// generous click targets.  kind: 0 eddy-warm, 1 eddy-cold, 2 front, 3 zone
static void drawAnchorIcon(int kind, int sx, int sy, bool sel)
{
    glLineWidth(1.6f);
    if (kind == 0) glColor4f(0.92f, 0.42f, 0.32f, 1.0f);
    else if (kind == 1) glColor4f(0.32f, 0.62f, 0.92f, 1.0f);
    else if (kind == 2) glColor4f(0.95f, 0.78f, 0.25f, 1.0f);
    else glColor4f(0.85f, 0.85f, 0.85f, 1.0f);
    if (kind <= 1) {
        // circle + cross (eddy)
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 12; a++) {
            double th = 2.0 * M_PI * a / 12;
            glVertex2i(sx + (int)lround(6 * cos(th)), sy + (int)lround(6 * sin(th)));
        }
        glEnd();
        glBegin(GL_LINES);
        glVertex2i(sx - 3, sy); glVertex2i(sx + 3, sy);
        glVertex2i(sx, sy - 3); glVertex2i(sx, sy + 3);
        glEnd();
    } else if (kind == 2) {
        // double bar (front)
        glBegin(GL_LINES);
        glVertex2i(sx - 6, sy - 2); glVertex2i(sx + 6, sy - 2);
        glVertex2i(sx - 6, sy + 2); glVertex2i(sx + 6, sy + 2);
        glEnd();
    } else if (kind == 4) {
        // chevron (jet)
        glColor4f(0.95f, 0.60f, 0.15f, 1.0f);
        glBegin(GL_LINES);
        glVertex2i(sx - 4, sy - 5); glVertex2i(sx + 3, sy);
        glVertex2i(sx + 3, sy);     glVertex2i(sx - 4, sy + 5);
        glEnd();
    } else {
        // diamond (zone)
        glBegin(GL_LINE_LOOP);
        glVertex2i(sx, sy - 6); glVertex2i(sx + 6, sy);
        glVertex2i(sx, sy + 6); glVertex2i(sx - 6, sy);
        glEnd();
    }
    if (sel) {
        glColor4f(1.0f, 1.0f, 1.0f, 0.95f);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 12; a++) {
            double th = 2.0 * M_PI * a / 12;
            glVertex2i(sx + (int)lround(10 * cos(th)), sy + (int)lround(10 * sin(th)));
        }
        glEnd();
    }
    glLineWidth(1.0f);
}

void FeatureRender::drawAll(const FeatureSet &fs, PlugIn_ViewPort *vp,
                            ncdfOverlayFactory *factory, bool showLegend,
                            double selLon, double selLat)
{
    if (!vp) return;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // ---- zones: translucent fill (ear-clipped) + 1px outline ----
    {
        std::vector<int> trisScratch;
        for (size_t z = 0; z < fs.zones.size(); z++) {
            const ZoneFeature &zf = fs.zones[z];
            if (zf.vCnt < 3) continue;
            {
                double czx = 0.5 * (zf.bbox[0] + zf.bbox[2]), czy = 0.5 * (zf.bbox[1] + zf.bbox[3]);
                if (!visPoint(vp, czx, czy)) continue;   // whole-polygon cull (phantom-band fix)
            }
            float r, g, b, a;
            zoneColor(zf.kind, zf.strength, r, g, b, a);
            glColor4f(r, g, b, a);
            fillRing(fs, zf.vOff, zf.vCnt, vp, trisScratch);

            glColor4f(r * 0.8f, g * 0.8f, b * 0.8f, 0.85f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            for (int k = 0; k < zf.vCnt; k++) {
                int sx, sy;
                if (projectPoint(vp, fs.arena[(size_t)(zf.vOff + 2 * k)],
                                        fs.arena[(size_t)(zf.vOff + 2 * k + 1)], sx, sy))
                    glVertex2i(sx, sy);
            }
            glEnd();
        }
    }

    // ---- eddies: translucent disc (star ring fan from center) + contour ----
    {
        std::vector<int> trisScratch;
        for (size_t e = 0; e < fs.eddies.size(); e++) {
            const EddyFeature &ef = fs.eddies[e];
            if (ef.vCnt < 3) continue;
            if (!visPoint(vp, ef.clon, ef.clat)) continue;   // whole-disc cull (phantom-band fix)
            float rC, gC, bC;
            if (ef.polarity > 0) { rC = 0.90f; gC = 0.45f; bC = 0.35f; }   // warm/anticy
            else                 { rC = 0.35f; gC = 0.60f; bC = 0.90f; }   // cold/cyclone
            float a = 0.16f + 0.10f * ef.strength;
            int scx, scy;
            if (!projectPoint(vp, (float)ef.clon, (float)ef.clat, scx, scy)) continue;
            // star-shaped ring: fan from the projected center is exact
            glColor4f(rC, gC, bC, a);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2i(scx, scy);
            for (int k = 0; k < ef.vCnt; k++) {
                int sx, sy;
                if (projectPoint(vp, fs.arena[(size_t)(ef.vOff + 2 * k)],
                                        fs.arena[(size_t)(ef.vOff + 2 * k + 1)], sx, sy))
                    glVertex2i(sx, sy);
            }
            {
                int sx, sy;
                if (projectPoint(vp, fs.arena[(size_t)ef.vOff], fs.arena[(size_t)(ef.vOff + 1)], sx, sy))
                    glVertex2i(sx, sy);
            }
            glEnd();
            // contour outline (the measured extent / radius)
            glColor4f(rC * 0.85f, gC * 0.85f, bC * 0.85f, 0.85f);
            glLineWidth(1.2f);
            glBegin(GL_LINE_LOOP);
            for (int k = 0; k < ef.vCnt; k++) {
                int sx, sy;
                if (projectPoint(vp, fs.arena[(size_t)(ef.vOff + 2 * k)],
                                        fs.arena[(size_t)(ef.vOff + 2 * k + 1)], sx, sy))
                    glVertex2i(sx, sy);
            }
            glEnd();
            glLineWidth(1.0f);
        }
    }

    // ---- jets: translucent speed ribbon + direction chevrons (capped) ----
    size_t jetDrawLim = (fs.jets.size() < 40) ? fs.jets.size() : 40;
    for (size_t t = 0; t < jetDrawLim; t++) {
        const JetFeature &jf = fs.jets[t];
        if (jf.vCnt < 2) continue;
        glColor4f(0.95f, 0.60f, 0.15f, 0.45f);   // amber ribbon
        glLineWidth(4.0f + jf.strength * 8.0f);
        glBegin(GL_LINE_STRIP);
        bool jRun = false;
        for (int k = 0; k < jf.vCnt; k++) {
            float lona = fs.arena[(size_t)(jf.vOff + 2 * k)];
            float lata = fs.arena[(size_t)(jf.vOff + 2 * k + 1)];
            if (!visPoint(vp, lona, lata)) {
                // break the strip: connecting across culled stretches draws
                // phantom full-width lines (phantom-line bug)
                if (jRun) { glEnd(); glBegin(GL_LINE_STRIP); jRun = false; }
                continue;
            }
            int sx, sy;
            if (projectPoint(vp, lona, lata, sx, sy)) { glVertex2i(sx, sy); jRun = true; }
        }
        glEnd();
        // direction chevrons every 4th vertex
        glColor4f(0.55f, 0.30f, 0.05f, 0.9f);
        glLineWidth(1.6f);
        glBegin(GL_LINES);
        for (int k = 2; k + 2 < jf.vCnt; k += 4) {
            int sx0, sy0, sx1, sy1, sx2, sy2;
            if (!projectPoint(vp, fs.arena[(size_t)(jf.vOff + 2 * (k - 2))],
                                    fs.arena[(size_t)(jf.vOff + 2 * (k - 2) + 1)], sx0, sy0)) continue;
            if (!projectPoint(vp, fs.arena[(size_t)(jf.vOff + 2 * k)],
                                    fs.arena[(size_t)(jf.vOff + 2 * k + 1)], sx1, sy1)) continue;
            if (!projectPoint(vp, fs.arena[(size_t)(jf.vOff + 2 * (k + 2))],
                                    fs.arena[(size_t)(jf.vOff + 2 * (k + 2) + 1)], sx2, sy2)) continue;
            double dx = (double)(sx2 - sx0), dy = (double)(sy2 - sy0);
            double L = sqrt(dx * dx + dy * dy);
            if (L < 1e-3) continue;
            dx /= L; dy /= L;
            // V mark: two 5px strokes trailing behind the tip
            double bx = sx1 - dx * 5, by = sy1 - dy * 5;
            double px = -dy, py = dx;
            glVertex2i(sx1, sy1);
            glVertex2i((int)(bx + px * 3), (int)(by + py * 3));
            glVertex2i(sx1, sy1);
            glVertex2i((int)(bx - px * 3), (int)(by - py * 3));
        }
        glEnd();
        glLineWidth(1.0f);
    }

    // ---- fronts: variable-width / variable-color polylines (capped) ----
    size_t frontDrawLim = (fs.fronts.size() < 150) ? fs.fronts.size() : 150;
    for (size_t f = 0; f < frontDrawLim; f++) {
        const FrontFeature &ff = fs.fronts[f];
        if (ff.vCnt < 2) continue;
        float r, g, b;
        frontColor(ff.strength, r, g, b);
        glColor4f(r, g, b, 0.95f);
        glLineWidth(1.0f + ff.strength * 1.5f);   // cap at 2.5px (v1.2)
        glBegin(GL_LINE_STRIP);
        bool inRun = false;
        for (int k = 0; k < ff.vCnt; k++) {
            float lona = fs.arena[(size_t)(ff.vOff + 2 * k)];
            float lata = fs.arena[(size_t)(ff.vOff + 2 * k + 1)];
            if (!visPoint(vp, lona, lata)) {
                if (inRun) { glEnd(); glBegin(GL_LINE_STRIP); inRun = false; }
                continue;
            }
            int sx, sy;
            if (projectPoint(vp, lona, lata, sx, sy)) { glVertex2i(sx, sy); inRun = true; }
        }
        glEnd();
    }
    glLineWidth(1.0f);

    // ---- anchor icons (P2): eddy centers, front midpoints, zone centroids ----
    {
        auto nearSel = [&](double lon, double lat) -> bool {
            if (selLon < -900) return false;
            double dLon = lon - selLon;
            while (dLon > 180) dLon -= 360;
            while (dLon < -180) dLon += 360;
            return (fabs(dLon) < 0.05 && fabs(lat - selLat) < 0.05);
        };
        for (size_t e = 0; e < fs.eddies.size(); e++) {
            const EddyFeature &ef = fs.eddies[e];
            int sx, sy;
            if (projectPoint(vp, (float)ef.clon, (float)ef.clat, sx, sy))
                drawAnchorIcon(ef.polarity > 0 ? 0 : 1, sx, sy, nearSel(ef.clon, ef.clat));
        }
        for (size_t f = 0; f < fs.fronts.size(); f++) {
            const FrontFeature &ff = fs.fronts[f];
            if (ff.vCnt < 1) continue;
            int k = ff.vCnt / 2;
            float lona = fs.arena[(size_t)(ff.vOff + 2 * k)];
            float lata = fs.arena[(size_t)(ff.vOff + 2 * k + 1)];
            int sx, sy;
            if (projectPoint(vp, lona, lata, sx, sy))
                drawAnchorIcon(2, sx, sy, nearSel(lona, lata));
        }
        for (size_t t = 0; t < fs.jets.size(); t++) {
            const JetFeature &jf = fs.jets[t];
            if (jf.vCnt < 1) continue;
            int k = jf.vCnt / 2;
            float lona = fs.arena[(size_t)(jf.vOff + 2 * k)];
            float lata = fs.arena[(size_t)(jf.vOff + 2 * k + 1)];
            int sx, sy;
            if (projectPoint(vp, lona, lata, sx, sy))
                drawAnchorIcon(4, sx, sy, nearSel(lona, lata));
        }
        for (size_t z = 0; z < fs.zones.size(); z++) {
            const ZoneFeature &zf = fs.zones[z];
            if (zf.vCnt < 1) continue;
            double cx = 0, cy = 0;
            for (int k = 0; k < zf.vCnt; k++) {
                cx += fs.arena[(size_t)(zf.vOff + 2 * k)];
                cy += fs.arena[(size_t)(zf.vOff + 2 * k + 1)];
            }
            cx /= zf.vCnt; cy /= zf.vCnt;
            int sx, sy;
            if (projectPoint(vp, (float)cx, (float)cy, sx, sy))
                drawAnchorIcon(3, sx, sy, nearSel(cx, cy));
        }
    }

    drawFeaturePopup(vp);
    if (showLegend) drawFeatureLegend(fs, (int)vp->pix_width);

    // restore GL state (project convention)
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// 黑潮主轴 / 左右边界绘制（流线约束最大流速法）
// 主轴：深色描边 + 黄色主线 + 流向人字标；左右边界：蓝 / 红细线（NaN 处断开）。
// dashPx > 0 时按**屏幕像素**画虚线（参考轴专用，与自己提取的实线黄轴区分）。
// 不用 glLineStipple：宽线下部分驱动的虚线支持不可靠，自己按屏幕弧长撒点最稳。
static void drawLatLonPolylineRaw(PlugIn_ViewPort *vp, const double *xy, size_t n,
                                  float r, float g, float b, float a, float width,
                                  bool brokenAtNaN, double dashPx = 0.0)
{
    if (!xy || n < 2) return;
    glColor4f(r, g, b, a);
    glLineWidth(width);

    if (dashPx > 0.5) {
        const double onPx = dashPx;
        const double period = dashPx * 2.2;          // 亮:暗 = 1 : 1.2
        glBegin(GL_LINES);
        double acc = 0.0;
        bool havePrev = false;
        double px0 = 0.0, py0 = 0.0;
        for (size_t k = 0; k < n; k++) {
            double lon = xy[2 * k], lat = xy[2 * k + 1];
            bool bad = !(lon == lon) || !(lat == lat);   // NaN
            int sx = 0, sy = 0;
            if (bad || !visPoint(vp, lon, lat) ||
                !projectPoint(vp, (float)lon, (float)lat, sx, sy)) {
                havePrev = false;
                continue;
            }
            if (havePrev) {
                double dx = (double)sx - px0, dy = (double)sy - py0;
                double seg = std::sqrt(dx * dx + dy * dy);
                if (seg > 0.01) {
                    // 小步 <= 2px：跨虚线边界的相位误差不超过 2px，肉眼不可见。
                    // 【必须夹紧 nsub】视口缩得很小时 visPoint 那 8° 容差对应的
                    // 屏幕距离能到上千万像素，不夹紧就是上千万个顶点堆进一个
                    // glBegin 块里，直接把驱动/堆顶爆。长段一律按 1024 小步近似。
                    int nsub = (int)(seg / 2.0) + 1;
                    if (nsub > 1024) nsub = 1024;
                    for (int s = 0; s < nsub; s++) {
                        double t0 = (double)s / (double)nsub;
                        double t1 = (double)(s + 1) / (double)nsub;
                        double a0 = acc + t0 * seg, a1 = acc + t1 * seg;
                        if (std::fmod(a0, period) < onPx && std::fmod(a1, period) < onPx) {
                            glVertex2d(px0 + t0 * dx, py0 + t0 * dy);
                            glVertex2d(px0 + t1 * dx, py0 + t1 * dy);
                        }
                    }
                    acc += seg;
                }
            }
            px0 = (double)sx; py0 = (double)sy; havePrev = true;
        }
        glEnd();
        glLineWidth(1.0f);
        return;
    }

    glBegin(GL_LINE_STRIP);
    bool run = false;
    for (size_t k = 0; k < n; k++) {
        double lon = xy[2 * k], lat = xy[2 * k + 1];
        bool bad = !(lon == lon) || !(lat == lat);   // NaN
        if (bad || !visPoint(vp, lon, lat)) {
            if (run) { glEnd(); glBegin(GL_LINE_STRIP); run = false; }
            continue;
        }
        if (brokenAtNaN && k > 0) {
            double plon = xy[2 * (k - 1)], plat = xy[2 * (k - 1) + 1];
            if (!(plon == plon) || !(plat == plat)) {
                glEnd(); glBegin(GL_LINE_STRIP); run = false;
            }
        }
        int sx, sy;
        if (projectPoint(vp, (float)lon, (float)lat, sx, sy)) { glVertex2i(sx, sy); run = true; }
    }
    glEnd();
    glLineWidth(1.0f);
}

static void drawLatLonPolyline(PlugIn_ViewPort *vp, const std::vector<double> &xy,
                               float r, float g, float b, float a, float width,
                               bool brokenAtNaN, double dashPx = 0.0)
{
    drawLatLonPolylineRaw(vp, xy.empty() ? NULL : &xy[0], xy.size() / 2,
                          r, g, b, a, width, brokenAtNaN, dashPx);
}

void FeatureRender::drawKuroshioAxis(const KuroshioAxisResult &k, PlugIn_ViewPort *vp,
                                     bool showAxis, bool showBoundaries, bool isRef)
{
    if (!vp) return;
    // 【一次性探针】必须在任何 k.xxx 成员访问**之前**打点：崩溃若落在这里之前，
    // 说明传进来的 KuroshioAxisResult 本身是坏的（撕裂/悬垂），不是画线的问题。
    static int s_kuroDrawDbg = 0;
    const bool kuroDbgThis = (s_kuroDrawDbg < 3);
    if (kuroDbgThis) {
        s_kuroDrawDbg++;
        ncdfLog("[drawkuro] enter #%d isRef=%d vp=%p ok=%d axis=%u left=%u right=%u\n",
                s_kuroDrawDbg, (int)isRef, (void*)vp, (int)k.ok,
                (unsigned)k.axis.size(), (unsigned)k.left.size(), (unsigned)k.right.size());
    }
    if (!k.ok || k.axis.size() < 4) return;
    if (!showAxis && !showBoundaries) return;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // 左右边界先画（在主轴下面）
    // 参考轴的边界用**虚线 + 低饱和**，让实线的蓝/红（自己提取的）始终是主视觉
    if (showBoundaries && k.left.size() >= 4 && k.right.size() >= 4) {
        if (isRef) {
            drawLatLonPolyline(vp, k.left,  0.78f, 0.84f, 0.95f, 0.75f, 1.4f, true, 8.0);
            drawLatLonPolyline(vp, k.right, 0.95f, 0.84f, 0.78f, 0.75f, 1.4f, true, 8.0);
        } else {
            drawLatLonPolyline(vp, k.left,  0.25f, 0.55f, 0.95f, 0.85f, 1.6f, true);
            drawLatLonPolyline(vp, k.right, 0.92f, 0.30f, 0.25f, 0.85f, 1.6f, true);
        }
    }

    if (showAxis) {
        if (isRef) {            // 参考轴：白色虚线 + 深色虚线描边，压在自己提取的实线黄轴之上。
            // 合上时是「黄线上走白虚线」，偏了就两条线分开 —— 不用肉眼猜谁是谁。
            // 不画流向人字标：那是提取结果的方向信息，参考文件里没有对应量。
            // kRefDashPx<=0 时退化成实线（排查崩溃用的 A/B 开关）
            const double kRefDashPx = 12.0;
            drawLatLonPolyline(vp, k.axis, 0.10f, 0.10f, 0.10f, 0.55f, 6.5f, false, kRefDashPx);
            drawLatLonPolyline(vp, k.axis, 1.00f, 1.00f, 1.00f, 0.95f, 3.0f, false, kRefDashPx);
        } else {
            // 深色描边提高在彩色流场上的对比度
            drawLatLonPolyline(vp, k.axis, 0.10f, 0.10f, 0.10f, 0.55f, 6.0f, false);
            drawLatLonPolyline(vp, k.axis, 1.00f, 0.88f, 0.20f, 0.95f, 3.0f, false);

            // 流向人字标（沿主轴方向）
            size_t n = k.axis.size() / 2;
            size_t stride = (n > 60) ? (n / 24) : 3;
            if (stride < 2) stride = 2;
            glColor4f(0.85f, 0.45f, 0.05f, 0.95f);
            glLineWidth(1.8f);
            glBegin(GL_LINES);
            for (size_t i = stride; i + stride < n; i += stride) {
                int sx0, sy0, sx1, sy1, sx2, sy2;
                if (!projectPoint(vp, (float)k.axis[2 * (i - stride)], (float)k.axis[2 * (i - stride) + 1], sx0, sy0)) continue;
                if (!projectPoint(vp, (float)k.axis[2 * i], (float)k.axis[2 * i + 1], sx1, sy1)) continue;
                if (!projectPoint(vp, (float)k.axis[2 * (i + stride)], (float)k.axis[2 * (i + stride) + 1], sx2, sy2)) continue;
                if (!visPoint(vp, k.axis[2 * i], k.axis[2 * i + 1])) continue;
                double dx = (double)(sx2 - sx0), dy = (double)(sy2 - sy0);
                double L = std::sqrt(dx * dx + dy * dy);
                if (L < 1e-3) continue;
                dx /= L; dy /= L;
                double bx = sx1 - dx * 6, by = sy1 - dy * 6;
                double px = -dy, py = dx;
                glVertex2i(sx1, sy1); glVertex2i((int)(bx + px * 4), (int)(by + py * 4));
                glVertex2i(sx1, sy1); glVertex2i((int)(bx - px * 4), (int)(by - py * 4));
            }
            glEnd();
            glLineWidth(1.0f);
        }
    }

    if (kuroDbgThis)
        ncdfLog("[drawkuro] done isRef=%d\n", (int)isRef);

    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// SEAMT 急流核心（Udalov2026 最小 T 参数法）
// 核心绿色粗线（与 Wang2026 的黄线区分，同论文图 2）+ 子射流着色线与出口标记。
// 只画核心与少量子射流线（几百点量级），不画 141 条集合 —— 那是纯诊断，
// 全画约 17 万顶点/帧，违反"平移缩放绝不卡顿"的硬约束。
// ---------------------------------------------------------------------------
// 接缝平滑（只改绘制副本，SeamtResult::core 里的真几何不动）。
//
// 【2026-11-05 用户定】力度必须**和两个点之间的距离挂钩**：缺口小就轻抹，
// 缺口大就用大窗让两条流线都往中间靠拢。旧做法是固定 ±3 点 + 固定权重，
// 跟间距无关 —— 缺口两点相隔 25 km 而普通步长只有 0.8 km，窗根本盖不住接缝，
// 结果只剩一个硬折角（图上看就不像流线）。
//
// 实测（海流1.nc，缺口 25.0 km、中位步长 0.77 km，接缝弦转角 94.07°）：
//   固定 ±3 点            -> 无效
//   插点 + 高斯           -> 无效（插进缺口的点是共线的，核里全是它们，抹不掉）
//   不插点 + 高斯 R=1.0×  -> 14.29°
//   **不插点 + 高斯 R=1.5× + 3 趟 -> 2.23°**，全轴 max 回到原有的 11.70°（别处）
//   贝塞尔替代缺口        -> 反而 175°（切向接反了），不用
//
// 三件事：
//   ① 量全轴中位步长 stepKm（跳过缺口那段）与缺口 gapKm；
//   ② **不插点** —— 插进缺口的点共线，高斯平均只会把它们按在直线上；
//   ③ 高斯窗 R = 1.5 × max(3×步长, 缺口长)，σ = R/2，3 趟。
//      衰减按「到**缺口段**的距离」算（缺口内 0、两侧才起算），
//      并用余弦锥度让窗边缘位移归零，否则窗边会造出更尖的折角。
// ---------------------------------------------------------------------------
static double seamDistKm(double lon1, double lat1, double lon2, double lat2)
{
    const double kRad = 3.14159265358979323846 / 180.0;
    const double mx = 0.5 * (lat1 + lat2) * kRad;
    const double dx = (lon2 - lon1) * kRad * 6371000.0 * std::cos(mx);
    const double dy = (lat2 - lat1) * kRad * 6371000.0;
    return std::sqrt(dx * dx + dy * dy) / 1000.0;
}

static void smoothSeamGaussian(std::vector<double> &xy, int seamIdx)
{
    int n = (int)(xy.size() / 2);
    if (seamIdx < 1 || seamIdx + 1 >= n) return;

    // ① 中位步长（缺口那段不参与统计，否则它会把中位数抬高）
    std::vector<double> steps;
    steps.reserve((size_t)n);
    for (int i = 1; i < n; i++) {
        if (i == seamIdx || i == seamIdx + 1) continue;
        steps.push_back(seamDistKm(xy[2 * (i - 1)], xy[2 * (i - 1) + 1],
                                   xy[2 * i], xy[2 * i + 1]));
    }
    if (steps.empty()) return;
    std::sort(steps.begin(), steps.end());
    const double stepKm = steps[steps.size() / 2];
    if (!(stepKm > 1.0e-6)) return;

    const double gapKm = seamDistKm(xy[2 * seamIdx], xy[2 * seamIdx + 1],
                                    xy[2 * (seamIdx + 1)], xy[2 * (seamIdx + 1) + 1]);

    // ③ 窗半径与缺口挂钩：缺口越大抹得越开
    const double R = 1.5 * ((gapKm > 3.0 * stepKm) ? gapKm : 3.0 * stepKm);
    const double sig = 0.5 * R;
    if (!(sig > 0.0)) return;

    std::vector<double> sArc((size_t)n, 0.0);
    for (int i = 1; i < n; i++)
        sArc[(size_t)i] = sArc[(size_t)(i - 1)] +
            seamDistKm(xy[2 * (i - 1)], xy[2 * (i - 1) + 1], xy[2 * i], xy[2 * i + 1]);
    const double sA = sArc[(size_t)seamIdx];              // 缺口两端
    const double sB = sArc[(size_t)(seamIdx + 1)];

    for (int pass = 0; pass < 3; pass++) {
        std::vector<double> tmp = xy;
        for (int i = 1; i < n - 1; i++) {
            // 衰减按「到**缺口段**的距离」：缺口内 0，两侧才起算。
            // 若锚在 seamIdx 那一个点上，缺口另一端正好落在窗边缘（alpha→0），
            // 只修好一头、另一头还是 94° 的折角（2026-11-05 实测踩到）。
            double ad = 0.0;
            if (sArc[(size_t)i] < sA)      ad = sA - sArc[(size_t)i];
            else if (sArc[(size_t)i] > sB) ad = sArc[(size_t)i] - sB;
            if (ad > R) continue;
            double wsum = 0.0, acc0 = 0.0, acc1 = 0.0;
            for (int j = 0; j < n; j++) {
                const double d = sArc[(size_t)j] - sArc[(size_t)i];
                if (d > 3.0 * sig || d < -3.0 * sig) continue;
                const double w = std::exp(-0.5 * (d / sig) * (d / sig));
                wsum += w;
                acc0 += w * tmp[2 * j];
                acc1 += w * tmp[2 * j + 1];
            }
            if (wsum > 0.0) {
                // 余弦锥度：窗边缘位移归零，否则窗边一刀切会造出更尖的折角
                const double alpha = 0.5 * (1.0 + std::cos(3.14159265358979323846 * ad / R));
                xy[2 * i]     = alpha * (acc0 / wsum) + (1.0 - alpha) * tmp[2 * i];
                xy[2 * i + 1] = alpha * (acc1 / wsum) + (1.0 - alpha) * tmp[2 * i + 1];
            }
        }
    }
}

void FeatureRender::drawSeamtCore(const SeamtResult &s, PlugIn_ViewPort *vp,
                                  bool showCore, bool showSubjets)
{
    if (!vp) return;
    if (!showCore && !showSubjets) return;
    if (s.core.size() < 4) return;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // 子射流线先画（压在核心下面）。冷 -> 暖按序号着色
    if (showSubjets && !s.subjetLines.empty()) {
        size_t off = 0;
        size_t nSub = s.subjetLineLen.size();
        for (size_t k = 0; k < nSub; k++) {
            int npts = s.subjetLineLen[k];
            if (npts >= 2 && off + (size_t)npts * 2 <= s.subjetLines.size()) {
                float t = (nSub > 1) ? (float)k / (float)(nSub - 1) : 0.5f;
                drawLatLonPolylineRaw(vp, &s.subjetLines[off], (size_t)npts,
                                      0.30f + 0.55f * t, 0.60f - 0.35f * t,
                                      0.95f - 0.75f * t, 0.85f, 1.8f, false);
            }
            off += (size_t)npts * 2;
        }
    }

    // 核心线：深色描边提高在彩色流场上的对比度
    if (showCore) {
    // 绘制副本：接缝处做高斯平滑（力度与缺口成正比），core 本身不动
    std::vector<double> coreDraw = s.core;
    smoothSeamGaussian(coreDraw, s.seamSmoothIdx);

        drawLatLonPolyline(vp, coreDraw, 0.10f, 0.10f, 0.10f, 0.55f, 6.0f, false);
        drawLatLonPolyline(vp, coreDraw, 0.25f, 0.85f, 0.35f, 0.95f, 3.0f, false);
    }

    // 子射流出口标记（小菱形）
    if (showSubjets && !s.subjets.empty()) {
        glLineWidth(1.8f);
        size_t nSub = s.subjets.size();
        for (size_t k = 0; k < nSub; k++) {
            int sx, sy;
            if (!projectPoint(vp, (float)s.subjets[k].lonOut, (float)s.subjets[k].latOut, sx, sy))
                continue;
            float t = (nSub > 1) ? (float)k / (float)(nSub - 1) : 0.5f;
            glColor4f(0.30f + 0.55f * t, 0.60f - 0.35f * t, 0.95f - 0.75f * t, 0.95f);
            glBegin(GL_LINE_LOOP);
            glVertex2i(sx, sy - 6); glVertex2i(sx + 6, sy);
            glVertex2i(sx, sy + 6); glVertex2i(sx - 6, sy);
            glEnd();
        }
        glLineWidth(1.0f);
    }

    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// SEAMT 过程中遇到的涡（椭圆点/汇）与鞍点（双曲点）—— HEPTA 式雅可比分类
//   涡：速度环边界（细橙圈）+ 涡心（品红点，汇加收敛短划）
//   鞍点：红色叉号 —— 双曲点正是次级射流的分界（Udalov 2026 §3.2）
// 画的都是"主轴识别过程中碰到的"临界点（流线停住的地方），不是全图扫描。
// ---------------------------------------------------------------------------
void FeatureRender::drawSeamtCriticals(const SeamtResult &s, PlugIn_ViewPort *vp)
{
    if (!vp) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---- 涡边界（速度环）----
    if (!s.vortexCircles.empty()) {
        size_t off = 0;
        for (size_t k = 0; k < s.vortexCircleLen.size(); k++) {
            int npts = s.vortexCircleLen[k];
            if (npts >= 3 && off + (size_t)npts * 2 <= s.vortexCircles.size())
                drawLatLonPolylineRaw(vp, &s.vortexCircles[off], (size_t)npts,
                                      1.00f, 0.55f, 0.10f, 0.85f, 1.6f, false);
            off += (size_t)npts * 2;
        }
    }
    // ---- 涡心 ----
    for (size_t i = 0; i + 3 < s.vortexCentres.size(); i += 4) {
        int sx, sy;
        if (!projectPoint(vp, (float)s.vortexCentres[i], (float)s.vortexCentres[i + 1], sx, sy))
            continue;
        const int type = (int)s.vortexCentres[i + 2];
        glColor4f(1.0f, 0.20f, 0.75f, 0.95f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 24; a++) {
            double th = 2.0 * M_PI * a / 24.0;
            glVertex2i(sx + (int)(5.0 * std::cos(th)), sy + (int)(5.0 * std::sin(th)));
        }
        glEnd();
        if (type == 2 || type == 4) {          // 汇：向心短划
            glBegin(GL_LINES);
            for (int a = 0; a < 4; a++) {
                double th = 2.0 * M_PI * a / 4.0 + M_PI / 4.0;
                glVertex2i(sx + (int)(5.0 * std::cos(th)), sy + (int)(5.0 * std::sin(th)));
                glVertex2i(sx, sy);
            }
            glEnd();
        }
    }

    // ---- 鞍点（双曲点）：红色叉号 ----
    for (size_t i = 0; i + 9 < s.critPoints.size(); i += 10) {
        if ((int)s.critPoints[i + 2] != 0) continue;
        int sx, sy;
        if (!projectPoint(vp, (float)s.critPoints[i], (float)s.critPoints[i + 1], sx, sy))
            continue;
        glColor4f(1.0f, 0.15f, 0.15f, 0.95f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glVertex2i(sx - 6, sy - 6); glVertex2i(sx + 6, sy + 6);
        glVertex2i(sx - 6, sy + 6); glVertex2i(sx + 6, sy - 6);
        glEnd();
    }
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
bool FeatureRender::clipMaskBegin(const FeatureSet &fs, PlugIn_ViewPort *vp)
{
    if (!vp) return false;
    GLint stencilBits = 0;
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    if (stencilBits <= 0) return false;

    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);

    // write zone polygons (the clip containers; line features carry no clip)
    {
        std::vector<int> trisScratch;
        for (size_t z = 0; z < fs.zones.size(); z++) {
            fillRing(fs, fs.zones[z].vOff, fs.zones[z].vCnt, vp, trisScratch);
        }
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);   // only inside feature interiors
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    return true;
}

void FeatureRender::clipMaskEnd()
{
    glDisable(GL_STENCIL_TEST);
}

//===================================================================
// ncdf_legend.cpp — Color legend with value labels
//===================================================================

#include "ncdf_legend.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <cstdio>
#include <cstring>
#include <cmath>
#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/font.h>

ncdfLegend::ncdfLegend()
    : m_stops(NULL), m_count(0)
{
    m_unit[0] = 0;
    m_title[0] = 0;
}

ncdfLegend::~ncdfLegend()
{
    Clear();
}

void ncdfLegend::Clear()
{
    if (m_stops) { delete[] m_stops; m_stops = NULL; }
    m_count = 0;
}

void ncdfLegend::SetData(const ColorStop* stops, int count, const char* unit, const char* title)
{
    Clear();
    if (!stops || count <= 0) return;
    m_count = count;
    m_stops = new ColorStop[count];
    memcpy(m_stops, stops, count * sizeof(ColorStop));
    if (unit) strncpy(m_unit, unit, sizeof(m_unit)-1);
    if (title) strncpy(m_title, title, sizeof(m_title)-1);
}

// Render a string to an OpenGL texture using wxMemoryDC
// Returns texture ID, sets outW/outH to pixel dimensions
static GLuint renderTextToTexture(const wxString& text, wxFont& font, int& outW, int& outH)
{
    wxMemoryDC dc;
    dc.SetFont(font);

    int tw = 0, th = 0;
    dc.GetTextExtent(text, &tw, &th);
    if (tw <= 0 || th <= 0) return 0;
    tw += 2;  // padding
    th += 2;

    wxBitmap bmp(tw, th, 24);
    dc.SelectObject(bmp);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    dc.SetTextForeground(*wxWHITE);
    dc.DrawText(text, 1, 1);
    dc.SelectObject(wxNullBitmap);

    wxImage img = bmp.ConvertToImage();
    int w = img.GetWidth(), h = img.GetHeight();
    unsigned char* rgba = new unsigned char[w * h * 4];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            unsigned char r = img.GetRed(x, y);
            unsigned char g = img.GetGreen(x, y);
            unsigned char b = img.GetBlue(x, y);
            unsigned char lum = (unsigned char)((r + g + b) / 3);
            rgba[idx]     = 255;  // white text
            rgba[idx + 1] = 255;
            rgba[idx + 2] = 255;
            rgba[idx + 3] = lum;  // alpha from luminance
        }
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    delete[] rgba;

    outW = w;
    outH = h;
    return texID;
}

// Draw a textured quad at screen position
static void drawTexturedQuad(int x, int y, int w, int h, GLuint texID)
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

void ncdfLegend::Draw(int screenW, int screenH)
{
    if (!m_stops || m_count < 2) return;

    // Layout constants
    const int barW = 20;       // gradient bar width
    const int labelW = 80;     // label area width
    const int entryH = 22;     // height per color stop
    const int pad = 10;        // padding
    const int margin = 15;     // screen margin

    // Total height: title + gradient bar + bottom label
    int titleH = (m_title[0] ? 24 : 0);
    int totalH = titleH + (m_count - 1) * entryH + pad * 2;
    int totalW = barW + 8 + labelW + pad * 2;

    // Position: right side, vertically centered
    int x0 = screenW - margin - totalW;
    int y0 = (screenH - totalH) / 2;

    // Save GL state
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Background
    glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
    glBegin(GL_QUADS);
    glVertex2i(x0, y0);
    glVertex2i(x0 + totalW, y0);
    glVertex2i(x0 + totalW, y0 + totalH);
    glVertex2i(x0, y0 + totalH);
    glEnd();

    // Draw gradient bar
    int barX = x0 + pad;
    int barY0 = y0 + pad + titleH;
    int barH = (m_count - 1) * entryH;

    for (int i = 0; i < m_count - 1; i++) {
        int yTop = barY0 + barH - (i + 1) * entryH;
        int yBot = barY0 + barH - i * entryH;

        glBegin(GL_QUADS);
        glColor4ub(m_stops[i+1].r, m_stops[i+1].g, m_stops[i+1].b, 255);
        glVertex2i(barX, yTop);
        glVertex2i(barX + barW, yTop);
        glColor4ub(m_stops[i].r, m_stops[i].g, m_stops[i].b, 255);
        glVertex2i(barX + barW, yBot);
        glVertex2i(barX, yBot);
        glEnd();
    }

    // Border
    glColor4ub(180, 180, 180, 200);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(barX, barY0);
    glVertex2i(barX + barW, barY0);
    glVertex2i(barX + barW, barY0 + barH);
    glVertex2i(barX, barY0 + barH);
    glEnd();

    // Title
    if (m_title[0]) {
        wxFont titleFont = wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        int tw = 0, th = 0;
        GLuint titleTex = renderTextToTexture(wxString(m_title, wxConvUTF8), titleFont, tw, th);
        if (titleTex) {
            glEnable(GL_BLEND);
            drawTexturedQuad(barX, y0 + pad, tw, th, titleTex);
            glDeleteTextures(1, &titleTex);
        }
    }

    // Value labels — aligned to each color boundary
    wxFont labelFont = wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    int labelX = barX + barW + 8;

    for (int i = 0; i < m_count; i++) {
        // Stop i: top of bar = stop[count-1], bottom = stop[0]
        // Boundary y position: exact pixel on the bar
        int stopY = barY0 + barH - (int)((double)i / (m_count - 1) * barH);

        char valStr[32];
        if (fabs(m_stops[i].value) < 0.01 && m_stops[i].value != 0.0)
            snprintf(valStr, sizeof(valStr), "%.2f %s", m_stops[i].value, m_unit);
        else if (fabs(m_stops[i].value) >= 100)
            snprintf(valStr, sizeof(valStr), "%.0f %s", m_stops[i].value, m_unit);
        else
            snprintf(valStr, sizeof(valStr), "%.1f %s", m_stops[i].value, m_unit);

        int tw = 0, th = 0;
        GLuint labelTex = renderTextToTexture(wxString(valStr, wxConvUTF8), labelFont, tw, th);
        if (labelTex) {
            glEnable(GL_BLEND);
            // Vertically center the text on the boundary line
            drawTexturedQuad(labelX, stopY - th/2, tw, th, labelTex);
            glDeleteTextures(1, &labelTex);
        }
    }

    glPopAttrib();
}

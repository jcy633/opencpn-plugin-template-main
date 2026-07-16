/***************************************************************************
 *   Adapted from GRIB plugin's IsoLine.cpp (Jacques Zaninetti)
 *   Modified for ncdf plugin: replaced GribRecord with NcdfGridRecord
 ***************************************************************************/

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <wx/graphics.h>
#include <GL/gl.h>

#include "NcdfIsoLine.h"
#include "ncdfoverlayfactory.h"
#include "ncdfdata.h"

wxList ncdf_spline_point_list;

#include <wx/listimpl.cpp>
WX_DEFINE_LIST(NcdfSegList);
WX_DEFINE_LIST(NcdfSegListList);

#ifndef PI
#define PI 3.14159
#endif
#define NCDF_TRUE -1
#define NCDF_FALSE 0

// Cohen-Sutherland line clipping
struct LOC_ncdf_cohen_sutherland {
    double xmin, xmax, ymin, ymax;
};

void NcdfCompOutCode(double x, double y, NcdfOutcode *code,
                     struct LOC_ncdf_cohen_sutherland *LINK) {
    *code = 0;
    if (y > LINK->ymax) *code = 1L << ((long)NCDF_TOP);
    else if (y < LINK->ymin) *code = 1L << ((long)NCDF_BOTTOM);
    if (x > LINK->xmax) *code |= 1L << ((long)NCDF_RIGHT);
    else if (x < LINK->xmin) *code |= 1L << ((long)NCDF_LEFT);
}

NcdfClipResult ncdf_cohen_sutherland_line_clip_d(
    double *x0, double *y0, double *x1, double *y1,
    double xmin_, double xmax_, double ymin_, double ymax_) {
    struct LOC_ncdf_cohen_sutherland V;
    int done = NCDF_FALSE;
    NcdfClipResult clip = NcdfVisible;
    NcdfOutcode outcode0, outcode1, outcodeOut;
    double x = 0., y = 0.;

    V.xmin = xmin_; V.xmax = xmax_;
    V.ymin = ymin_; V.ymax = ymax_;
    NcdfCompOutCode(*x0, *y0, &outcode0, &V);
    NcdfCompOutCode(*x1, *y1, &outcode1, &V);
    do {
        if (outcode0 == 0 && outcode1 == 0) {
            done = NCDF_TRUE;
        } else if ((outcode0 & outcode1) != 0) {
            clip = NcdfInvisible;
            done = NCDF_TRUE;
        } else {
            clip = NcdfVisible;
            if (outcode0 != 0) outcodeOut = outcode0;
            else outcodeOut = outcode1;

            if (((1L << ((long)NCDF_TOP)) & outcodeOut) != 0) {
                x = *x0 + (*x1 - *x0) * (V.ymax - *y0) / (*y1 - *y0);
                y = V.ymax;
            } else if (((1L << ((long)NCDF_BOTTOM)) & outcodeOut) != 0) {
                x = *x0 + (*x1 - *x0) * (V.ymin - *y0) / (*y1 - *y0);
                y = V.ymin;
            } else if (((1L << ((long)NCDF_RIGHT)) & outcodeOut) != 0) {
                y = *y0 + (*y1 - *y0) * (V.xmax - *x0) / (*x1 - *x0);
                x = V.xmax;
            } else if (((1L << ((long)NCDF_LEFT)) & outcodeOut) != 0) {
                y = *y0 + (*y1 - *y0) * (V.xmin - *x0) / (*x1 - *x0);
                x = V.xmin;
            }
            if (outcodeOut == outcode0) {
                *x0 = x; *y0 = y;
                NcdfCompOutCode(*x0, *y0, &outcode0, &V);
            } else {
                *x1 = x; *y1 = y;
                NcdfCompOutCode(*x1, *y1, &outcode1, &V);
            }
        }
    } while (!done);
    return clip;
}

//---------------------------------------------------------------
// NcdfIsoLine
//---------------------------------------------------------------
NcdfIsoLine::NcdfIsoLine(double val, const NcdfGridRecord *rec_) {
    value = val;
    rec = rec_;
    W = rec_->getNi();
    H = rec_->getNj();

    isoLineColor = wxColour(80, 80, 80);

    extractIsoLine(rec_);

    if (trace.size() == 0) return;

    std::list<NcdfSegment *>::iterator it;
    for (it = trace.begin(); it != trace.end(); it++) {
        NcdfSegment *seg = *it;
        seg->bUsed = false;
        m_seglist.Append(*it);
    }

    bool bdone = false;
    while (!bdone) {
        NcdfSegList *ps = BuildContinuousSegment();
        m_SegListList.Append(ps);

        NcdfSegList::Node *node;
        NcdfSegment *seg;

        node = m_seglist.GetFirst();
        while (node) {
            seg = node->GetData();
            if (seg->bUsed) {
                m_seglist.Erase(node);
                node = m_seglist.GetFirst();
            } else
                node = node->GetNext();
        }
        if (0 == m_seglist.GetCount()) bdone = true;
    }
}

NcdfIsoLine::~NcdfIsoLine() {
    std::list<NcdfSegment *>::iterator it;
    for (it = trace.begin(); it != trace.end(); it++) {
        delete *it;
        *it = nullptr;
    }
    trace.clear();
    m_SegListList.DeleteContents(true);
    m_SegListList.Clear();
}

NcdfSegList *NcdfIsoLine::BuildContinuousSegment(void) {
    NcdfSegList::Node *node;
    NcdfSegment *seg;

    NcdfSegList *ret_list = new NcdfSegList;
    NcdfSegList segjoin2;

    node = m_seglist.GetFirst();
    NcdfSegment *seg0 = node->GetData();
    seg0->bUsed = true;
    segjoin2.Append(seg0);

    NcdfSegment *tseg = seg0;
    while (tseg) {
        bool badded = false;
        node = m_seglist.GetFirst();
        while (node) {
            seg = node->GetData();
            if ((!seg->bUsed) && (seg->py1 == tseg->py2) && (seg->px1 == tseg->px2)) {
                seg->bUsed = true;
                segjoin2.Append(seg);
                badded = true;
                break;
            } else if ((!seg->bUsed) && (seg->py2 == tseg->py2) && (seg->px2 == tseg->px2)) {
                seg->bUsed = true;
                double a = seg->px2; seg->px2 = seg->px1; seg->px1 = a;
                double b = seg->py2; seg->py2 = seg->py1; seg->py1 = b;
                segjoin2.Append(seg);
                badded = true;
                break;
            }
            node = node->GetNext();
        }
        if (badded) tseg = seg;
        else tseg = nullptr;
    }

    NcdfSegList segjoin1;
    node = m_seglist.GetFirst();
    seg0 = node->GetData();
    seg0->bUsed = true;
    segjoin1.Append(seg0);

    tseg = seg0;
    while (tseg) {
        bool badded = false;
        node = m_seglist.GetFirst();
        while (node) {
            seg = node->GetData();
            if ((!seg->bUsed) && (seg->py2 == tseg->py1) && (seg->px2 == tseg->px1)) {
                seg->bUsed = true;
                segjoin1.Append(seg);
                badded = true;
                break;
            } else if ((!seg->bUsed) && (seg->py1 == tseg->py1) && (seg->px1 == tseg->px1)) {
                seg->bUsed = true;
                double a = seg->px2; seg->px2 = seg->px1; seg->px1 = a;
                double b = seg->py2; seg->py2 = seg->py1; seg->py1 = b;
                segjoin1.Append(seg);
                badded = true;
                break;
            }
            node = node->GetNext();
        }
        if (badded) tseg = seg;
        else tseg = nullptr;
    }

    int n1 = segjoin1.GetCount();
    for (int i = n1 - 1; i > 0; i--) {
        node = segjoin1.Item(i);
        seg = node->GetData();
        ret_list->Append(seg);
    }

    int n2 = segjoin2.GetCount();
    for (int i = 0; i < n2; i++) {
        node = segjoin2.Item(i);
        seg = node->GetData();
        ret_list->Append(seg);
    }

    return ret_list;
}

//---------------------------------------------------------------
void NcdfIsoLine::drawIsoLine(ncdfOverlayFactory *pof, wxDC *dc,
                               PlugIn_ViewPort *vp, bool bHiDef) {
    int nsegs = trace.size();
    if (nsegs < 1) return;

#if wxUSE_GRAPHICS_CONTEXT
    wxGraphicsContext *pgc = nullptr;
#endif

    if (dc) {
        wxPen ppISO(isoLineColor, 2);
#if wxUSE_GRAPHICS_CONTEXT
        wxMemoryDC *pmdc = dynamic_cast<wxMemoryDC *>(dc);
        if (pmdc) {
            pgc = wxGraphicsContext::Create(*pmdc);
            pgc->SetPen(ppISO);
        }
#endif
        dc->SetPen(ppISO);
    } else {
#ifdef ocpnUSE_GL
        glColor4ub(isoLineColor.Red(), isoLineColor.Green(), isoLineColor.Blue(), 255);
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(1.5f);
#endif
    }

    std::list<NcdfSegment *>::iterator it;
    for (it = trace.begin(); it != trace.end(); it++) {
        NcdfSegment *seg = *it;

        if (vp->m_projection_type == PI_PROJECTION_MERCATOR ||
            vp->m_projection_type == PI_PROJECTION_EQUIRECTANGULAR) {
            double sx1 = seg->px1, sx2 = seg->px2;
            if (sx2 - sx1 > 180) sx2 -= 360;
            else if (sx1 - sx2 > 180) sx1 -= 360;
            if ((sx1 + 180 < vp->clon && sx2 + 180 > vp->clon) ||
                (sx1 + 180 > vp->clon && sx2 + 180 < vp->clon) ||
                (sx1 - 180 < vp->clon && sx2 - 180 > vp->clon) ||
                (sx1 - 180 > vp->clon && sx2 - 180 < vp->clon))
                continue;
        }

        wxPoint ab;
        GetCanvasPixLL(vp, &ab, seg->py1, seg->px1);
        wxPoint cd;
        GetCanvasPixLL(vp, &cd, seg->py2, seg->px2);

        if (dc) {
#if wxUSE_GRAPHICS_CONTEXT
            if (bHiDef && pgc)
                pgc->StrokeLine(ab.x, ab.y, cd.x, cd.y);
            else
#endif
                dc->DrawLine(ab.x, ab.y, cd.x, cd.y);
        } else {
#ifdef ocpnUSE_GL
            glBegin(GL_LINES);
            glVertex2f(ab.x, ab.y);
            glVertex2f(cd.x, cd.y);
            glEnd();
#endif
        }
    }

#ifdef ocpnUSE_GL
    if (!dc) {
        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
    }
#endif

#if wxUSE_GRAPHICS_CONTEXT
    delete pgc;
#endif
}

//---------------------------------------------------------------
void NcdfIsoLine::drawIsoLineLabels(ncdfOverlayFactory *pof, wxDC *dc,
                                      PlugIn_ViewPort *vp, int density, int first,
                                      wxString label, wxColour &color) {
    std::list<NcdfSegment *>::iterator it;
    int nb = first;

    wxRect prev;
    for (it = trace.begin(); it != trace.end(); it++, nb++) {
        if (nb % density == 0) {
            NcdfSegment *seg = *it;
            wxPoint ab;
            GetCanvasPixLL(vp, &ab, (seg->py1 + seg->py2) / 2., (seg->px1 + seg->px2) / 2.);

            int w = label.length() * 7 + 4;
            int h = 14;
            int xd = ab.x - w / 2;
            int yd = ab.y - h / 2;

            wxRect r(xd, yd, w, h);
            r.Inflate(w);
            if (!prev.Intersects(r)) {
                prev = r;
                if (dc) {
                    dc->SetTextForeground(*wxWHITE);
                    dc->SetPen(*wxBLACK_PEN);
                    dc->SetBrush(wxBrush(color));
                    dc->DrawRectangle(xd, yd, w, h);
                    dc->DrawText(label, xd + 2, yd + 1);
                } else {
#ifdef ocpnUSE_GL
                    glColor4ub(color.Red(), color.Green(), color.Blue(), 200);
                    glBegin(GL_QUADS);
                    glVertex2i(xd, yd);
                    glVertex2i(xd + w, yd);
                    glVertex2i(xd + w, yd + h);
                    glVertex2i(xd, yd + h);
                    glEnd();
                    glColor4ub(0, 0, 0, 255);
                    glBegin(GL_LINE_LOOP);
                    glVertex2i(xd, yd);
                    glVertex2i(xd + w, yd);
                    glVertex2i(xd + w, yd + h);
                    glVertex2i(xd, yd + h);
                    glEnd();
                    // Draw label text using DC onto bitmap
                    wxBitmap bmp(w, h);
                    wxMemoryDC memDC(bmp);
                    memDC.SetBackground(*wxWHITE_BRUSH);
                    memDC.Clear();
                    memDC.SetTextForeground(*wxBLACK);
                    memDC.DrawText(label, 2, 1);
                    memDC.SelectObject(wxNullBitmap);
                    wxImage img = bmp.ConvertToImage();
                    if (img.HasAlpha()) img.InitAlpha();
                    // Blit at position
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    // Skip complex texture upload for now - rectangle + text is sufficient
#endif
                }
            }
        }
    }
}

//---------------------------------------------------------------
// Segment
//---------------------------------------------------------------
NcdfSegment::NcdfSegment(int I, int w, int J, char c1, char c2, char c3, char c4,
                           const NcdfGridRecord *rec, double pressure) {
    traduitCode(I, w, J, c1, i, j);
    traduitCode(I, w, J, c2, k, l);
    traduitCode(I, w, J, c3, m, n);
    traduitCode(I, w, J, c4, o, p);

    intersectionAreteGrille(i, j, k, l, &px1, &py1, rec, pressure);
    intersectionAreteGrille(m, n, o, p, &px2, &py2, rec, pressure);
}

void NcdfSegment::intersectionAreteGrille(int i, int j, int k, int l, double *x,
                                            double *y, const NcdfGridRecord *rec,
                                            double pressure) {
    double xa, xb, ya, yb, pa, pb, dec;
    pa = rec->getValue(i, j);
    pb = rec->getValue(k, l);

    rec->getXY(i, j, &xa, &ya);
    rec->getXY(k, l, &xb, &yb);

    if (pb != pa) dec = (pressure - pa) / (pb - pa);
    else dec = 0.5;
    if (fabs(dec) > 1) dec = 0.5;
    double xd = xb - xa;
    if (xd < -180) xd += 360;
    else if (xd > 180) xd -= 360;
    *x = xa + xd * dec;

    if (pb != pa) dec = (pressure - pa) / (pb - pa);
    else dec = 0.5;
    if (fabs(dec) > 1) dec = 0.5;
    *y = ya + (yb - ya) * dec;
}

void NcdfSegment::traduitCode(int I, int w, int J, char c1, int &i, int &j) {
    int Im1 = I ? I - 1 : w - 1;
    switch (c1) {
        case 'a': i = Im1; j = J - 1; break;
        case 'b': i = I;   j = J - 1; break;
        case 'c': i = Im1; j = J;     break;
        case 'd': i = I;   j = J;     break;
        default:  i = I;   j = J;
    }
}

//---------------------------------------------------------------
void NcdfIsoLine::extractIsoLine(const NcdfGridRecord *rec) {
    int i, j;
    int W = rec->getNi();
    int H = rec->getNj();

    int We = W;
    if (rec->getLonMax() + rec->getDi() - rec->getLonMin() == 360) We++;

    for (j = 1; j < H; j++) {
        double a = rec->getValue(0, j - 1);
        double c = rec->getValue(0, j);
        for (i = 1; i < We; i++, a = b, c = d) {
            double b, d;
            int ni = i;
            if (i == W) ni = 0;
            b = rec->getValue(ni, j - 1);
            d = rec->getValue(ni, j);

            if (a == ncdf_NOTDEF || b == ncdf_NOTDEF || c == ncdf_NOTDEF || d == ncdf_NOTDEF)
                continue;

            if ((a < value && b < value && c < value && d < value) ||
                (a > value && b > value && c > value && d > value))
                continue;

            // 1 diagonal segment
            if ((a <= value && b <= value && c <= value && d > value) ||
                (a > value && b > value && c > value && d <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'c', 'd', 'b', 'd', rec, value));
            else if ((a <= value && c <= value && d <= value && b > value) ||
                     (a > value && c > value && d > value && b <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'b', 'b', 'd', rec, value));
            else if ((c <= value && d <= value && b <= value && a > value) ||
                     (c > value && d > value && b > value && a <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'b', 'a', 'c', rec, value));
            else if ((a <= value && b <= value && d <= value && c > value) ||
                     (a > value && b > value && d > value && c <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'c', 'c', 'd', rec, value));
            // 1 horizontal/vertical segment
            else if ((a <= value && b <= value && c > value && d > value) ||
                     (a > value && b > value && c <= value && d <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'c', 'b', 'd', rec, value));
            else if ((a <= value && c <= value && b > value && d > value) ||
                     (a > value && c > value && b <= value && d <= value))
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'b', 'c', 'd', rec, value));
            // 2 diagonal segments
            else if (a <= value && d <= value && c > value && b > value) {
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'b', 'b', 'd', rec, value));
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'c', 'c', 'd', rec, value));
            } else if (a > value && d > value && c <= value && b <= value) {
                trace.push_back(new NcdfSegment(ni, W, j, 'a', 'b', 'a', 'c', rec, value));
                trace.push_back(new NcdfSegment(ni, W, j, 'b', 'd', 'c', 'd', rec, value));
            }
        }
    }
}

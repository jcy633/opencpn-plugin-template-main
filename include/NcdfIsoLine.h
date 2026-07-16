/***************************************************************************
 *   Adapted from GRIB plugin's IsoLine (Jacques Zaninetti)
 *   Modified for ncdf plugin: replaced GribRecord with NcdfGridRecord
 ***************************************************************************/

#ifndef NCDF_ISOLINE_H
#define NCDF_ISOLINE_H

#include <iostream>
#include <cmath>
#include <vector>
#include <list>
#include <set>

#include "ocpn_plugin.h"

class ViewPort;
class wxDC;
class ncdfOverlayFactory;

// Lightweight grid record adapter (replaces GribRecord)
struct NcdfGridRecord {
    int ni, nj;
    double **grid;
    double lonMin, latMin, di, dj;

    int getNi() const { return ni; }
    int getNj() const { return nj; }
    double getValue(int i, int j) const { return grid[j][i]; }
    void getXY(int i, int j, double *x, double *y) const {
        *x = lonMin + i * di;
        *y = latMin + j * dj;
    }
    double getDi() const { return di; }
    double getLonMin() const { return lonMin; }
    double getLonMax() const { return lonMin + (ni - 1) * di; }
};

class Segment;
WX_DECLARE_LIST(Segment, NcdfSegList);
WX_DECLARE_LIST(NcdfSegList, NcdfSegListList);

//-------------------------------------------------------------------------------------------------------
//  Cohen & Sutherland Line clipping
//-------------------------------------------------------------------------------------------------------
typedef enum { NcdfVisible, NcdfInvisible } NcdfClipResult;
typedef enum { NCDF_LEFT, NCDF_RIGHT, NCDF_BOTTOM, NCDF_TOP } NcdfEdge;
typedef long NcdfOutcode;

void NcdfCompOutCode(double x, double y, NcdfOutcode *code,
                     struct LOC_ncdf_cohen_sutherland *LINK);

NcdfClipResult ncdf_cohen_sutherland_line_clip_d(
    double *x0, double *y0, double *x1, double *y1,
    double xmin_, double xmax_, double ymin_, double ymax_);

class NcdfSegment {
public:
    NcdfSegment(int I, int w, int J, char c1, char c2, char c3, char c4,
                const NcdfGridRecord *rec, double pressure);

    int i, j, k, l;
    double px1, py1;
    int m, n, o, p;
    double px2, py2;
    bool bUsed;

private:
    void traduitCode(int I, int w, int J, char c1, int &i, int &j);
    void intersectionAreteGrille(int i, int j, int k, int l, double *x, double *y,
                                 const NcdfGridRecord *rec, double pressure);
};

//===============================================================
class NcdfIsoLine {
public:
    NcdfIsoLine(double val, const NcdfGridRecord *rec);
    ~NcdfIsoLine();

    void drawIsoLine(ncdfOverlayFactory *pof, wxDC *dc, PlugIn_ViewPort *vp, bool bHiDef);
    void drawIsoLineLabels(ncdfOverlayFactory *pof, wxDC *dc, PlugIn_ViewPort *vp,
                           int density, int first, wxString label, wxColour &color);

    int getNbSegments() { return trace.size(); }
    double getValue() { return value; }

private:
    double value;
    int W, H;
    const NcdfGridRecord *rec;

    wxColour isoLineColor;
    std::list<NcdfSegment *> trace;

    void extractIsoLine(const NcdfGridRecord *rec);
    NcdfSegList *BuildContinuousSegment(void);

    NcdfSegList m_seglist;
    NcdfSegListList m_SegListList;
};

#endif // NCDF_ISOLINE_H

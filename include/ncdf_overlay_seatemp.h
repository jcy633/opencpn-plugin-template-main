#ifndef NCDF_OVERLAY_SEATEMP_H
#define NCDF_OVERLAY_SEATEMP_H

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include "ncdf.h"
#include <vector>
#include <memory>

class MainDialog;
class ncdf_pi;
class ncdfOverlayFactory;
struct PlugIn_ViewPort;
struct ncdfIsoSeg;

// Sea temperature (海温) overlay: color map + isolines
struct SeaTempOverlay {
    // Static texture
    GLuint glTexture;
    bool hasTexture;
    bool needsRebuild;
    bool dataReady;
    int dataDim[2];
    int glDim[2];

    // Per-type LUT
    GLuint lutID;
    bool hasLUT;

    // Persistent upload buffer
    unsigned char *uploadBuf;
    int uploadBufSize;

    // Cached processed grid (sharpened / diffused), owns the 2D array
    std::unique_ptr<double*[]> cachedGrid;
    int cachedNj, cachedNi;
    // Cached base grid (before sharpen/diffusion), for re-applying filters on settings change
    std::unique_ptr<double*[]> cachedBaseGrid;

    // Cached data range (computed during rebuild, not every frame)
    float cachedDataMin, cachedDataMax;
    // Cached coefficient range (set by RenderGridOverlay during texture creation)
    float cachedCoefMin, cachedCoefMax;
    // Cached physical value range (for shader bilinear fallback)
    float cachedPhysMin, cachedPhysMax;
    float cachedPhysMinV, cachedPhysMaxV;

    // Cached B-spline coefficients (computed once per data change, reused every frame)
    float *cachedCoeff;
    float cachedCoefScalarMin, cachedCoefScalarMax;
    float cachedPhysScalarMin, cachedPhysScalarMax;
    int cachedCoeffNj, cachedCoeffNi;

    // Isolines
    bool needsIsoRebuild;
    std::vector<ncdfIsoSeg> isoSegments;

    void Init();
    void Cleanup();
    void Invalidate();
    void prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory);

    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory,
                        double** animatedGrid = NULL);
    void RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui);

    static bool s_smoothColors;
    static wxColour GetColor(double temp_c);
};

#endif // NCDF_OVERLAY_SEATEMP_H

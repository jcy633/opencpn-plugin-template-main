#ifndef NCDF_OVERLAY_SALINITY_H
#define NCDF_OVERLAY_SALINITY_H

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

// Salinity (海盐) overlay: color map + isolines
struct SalinityOverlay {
    // Static texture
    GLuint glTexture;
    bool hasTexture;
    bool needsRebuild;
    int dataDim[2];
    int glDim[2];

    // Per-type LUT
    GLuint lutID;
    bool hasLUT;

    // Persistent upload buffer
    unsigned char *uploadBuf;
    int uploadBufSize;

    // Cached processed grid (owns the 2D array)
    std::unique_ptr<double*[]> cachedGrid;
    int cachedNj, cachedNi;

    // Cached data range (computed during rebuild, not every frame)
    float cachedDataMin, cachedDataMax;
    // Cached coefficient range (set by RenderGridOverlay during texture creation)
    float cachedCoefMin, cachedCoefMax;
    // Cached physical value range (for shader bilinear fallback)
    float cachedPhysMin, cachedPhysMax;
    float cachedPhysMinV, cachedPhysMaxV;

    // Isolines
    bool needsIsoRebuild;
    std::vector<ncdfIsoSeg> isoSegments;

    void Init();
    void Cleanup();
    void Invalidate();

    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory,
                        bool useShader,
                        double** animatedGrid = NULL);
    void RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui);

    static bool s_smoothColors;
    static wxColour GetColor(double sal_psu);
};

#endif // NCDF_OVERLAY_SALINITY_H

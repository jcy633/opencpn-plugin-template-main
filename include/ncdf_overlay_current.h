#ifndef NCDF_OVERLAY_CURRENT_H
#define NCDF_OVERLAY_CURRENT_H

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include "ncdf.h"
#include <memory>

class MainDialog;
class ncdf_pi;
class ncdfOverlayFactory;
struct PlugIn_ViewPort;

// Current (海流) overlay: color map, arrows, particles, LIC
struct CurrentOverlay {
    // Static color map texture
    GLuint glTexture;
    bool hasTexture;
    bool needsRebuild;
    bool dataReady;  // true after prepareData completes (data is in cached grids/coefficients)
    int dataDim[2];
    int glDim[2];

    // Per-type LUT
    GLuint lutID;
    bool hasLUT;

    // Physical value texture for bilinear fallback (vector mode only)
    GLuint physicalTexID;
    bool hasPhysicalTex;

    // Persistent upload buffer
    unsigned char *uploadBuf;
    int uploadBufSize;

    int cachedNj, cachedNi;

    // Cached U/V grids for vector mode (float, avoid per-frame rebuild)
    float **cachedU, **cachedV;
    int cachedUNj, cachedVNi;

    // Cached B-spline coefficients for vector mode (computed once per data change)
    float *cachedCoeffU, *cachedCoeffV;
    float cachedCoefU_min, cachedCoefU_max, cachedCoefV_min, cachedCoefV_max;
    int cachedCoeffNj, cachedCoeffNi;

    // Cached data range (computed during rebuild, not every frame)
    float cachedDataMin, cachedDataMax;
    float cachedDataMinV, cachedDataMaxV;  // V component range (vector only)
    // Cached coefficient range (set by RenderGridOverlay during texture creation)
    float cachedCoefMin, cachedCoefMax;
    // Cached physical value range (for shader bilinear fallback)
    float cachedPhysMin, cachedPhysMax;
    float cachedPhysMinV, cachedPhysMaxV;  // V component range (vector only)

    void Init();
    void Cleanup();
    void clearCache();           // Free CPU caches + derived GPU textures
    void clearCoefficients();    // Free B-spline coefficients only, preserve grids (same-file switch)
    void prepareGrid(MainDialog *gui);   // Convert myMessage data to float grids (no coefficients)
    void prepareCoeff(ncdfOverlayFactory *factory);  // Compute B-spline coefficients from grids
    void releaseCoeffData();     // Free coefficient data after texture upload (keep grid capacity)
    ~CurrentOverlay() { Cleanup(); }
    void Invalidate();
    void prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory);  // Precompute grids + coefficients

    // Render the current color map overlay
    // animatedGrid: if non-NULL, use this as speed grid (scalar mode for animation)
    // animUGrid/animVGrid: if non-NULL, use vector mode with these u/v grids
    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory,
                        double** animatedGrid = NULL,
                        double** animUGrid = NULL,
                        double** animVGrid = NULL);

    // Color function (static for use as function pointer)
    static wxColour GetColor(double val_in);
};

#endif // NCDF_OVERLAY_CURRENT_H

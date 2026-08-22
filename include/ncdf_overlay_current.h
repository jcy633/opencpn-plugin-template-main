#ifndef NCDF_OVERLAY_CURRENT_H
#define NCDF_OVERLAY_CURRENT_H

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include "ncdf.h"

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
    int dataDim[2];
    int glDim[2];

    // Per-type LUT
    GLuint lutID;
    bool hasLUT;

    // Persistent upload buffer
    unsigned char *uploadBuf;
    int uploadBufSize;

    // Cached processed grid (sharpened / diffused)
    double **cachedGrid;
    int cachedNj, cachedNi;
    bool cachedOwns;

    // Cached data range (computed during rebuild, not every frame)
    float cachedDataMin, cachedDataMax;
    // Cached coefficient range (set by RenderGridOverlay during texture creation)
    float cachedCoefMin, cachedCoefMax;

    // Vorticity grid for slope shading
    double **cachedVorticity;
    int vortNj, vortNi;
    GLuint vortTexture;
    bool hasVortTexture;
    GLuint slopeSource;

    // LIC texture
    GLuint licTexture;
    bool hasLICTexture;
    bool needsLICRebuild;

    void Init();
    void Cleanup();
    void Invalidate();

    // Render the current color map overlay
    // animatedGrid: if non-NULL, use this as speed grid (scalar mode for animation)
    // animUGrid/animVGrid: if non-NULL, use vector mode with these u/v grids
    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory,
                        bool useShader,
                        double** animatedGrid = NULL,
                        double** animUGrid = NULL,
                        double** animVGrid = NULL);

    // Color function (static for use as function pointer)
    static bool s_smoothColors;  // set by RenderColorMap before drawing
    static wxColour GetColor(double val_in);
};

#endif // NCDF_OVERLAY_CURRENT_H

#ifndef NCDF_OVERLAY_SEATEMP_H
#define NCDF_OVERLAY_SEATEMP_H

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include "ncdf.h"
#include <vector>

class MainDialog;
class ncdf_pi;
class ncdfOverlayFactory;
struct PlugIn_ViewPort;
struct ncdfIsoSeg;

struct ncdfIsoSeg;

// Sea temperature (海温) overlay: color map + isolines
struct SeaTempOverlay {
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

    // Cached processed grid
    double **cachedGrid;
    int cachedNj, cachedNi;
    bool cachedOwns;

    // Isolines
    bool needsIsoRebuild;
    std::vector<ncdfIsoSeg> isoSegments;

    void Init();
    void Cleanup();
    void Invalidate();

    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory,
                        bool useShader, int interpMode);
    void RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui);

    static bool s_smoothColors;
    static wxColour GetColor(double temp_c);
};

#endif // NCDF_OVERLAY_SEATEMP_H

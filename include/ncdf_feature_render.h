#ifndef NCDF_FEATURE_RENDER_H
#define NCDF_FEATURE_RENDER_H

#include <wx/string.h>

class ncdfOverlayFactory;
struct PlugIn_ViewPort;
struct FeatureSet;
struct KuroshioAxisResult;

// Vector-object renderer for detected features (plan §4).
// All methods run on the render thread (GL context available).
// Frame cost: projection of cached vertices + immediate-mode GL only (C10).
class FeatureRender {
public:
    // Draw feature objects (zone fills+outlines, front polylines) and, when
    // showLegend, the feature legend frame (icon meaning + detected counts).
    static void drawAll(const FeatureSet &fs, PlugIn_ViewPort *vp,
                        ncdfOverlayFactory *factory, bool showLegend,
                        double selLon = -9999.0, double selLat = -9999.0);

    // 黑潮主轴与左右边界（流线约束最大流速法）：主轴黄色粗线 + 流向人字标，
    // 左边界蓝细线、右边界红细线。仅在渲染线程调用。
    static void drawKuroshioAxis(const KuroshioAxisResult &k, PlugIn_ViewPort *vp,
                                 bool showAxis, bool showBoundaries);

    // "Data layers only inside features" mode (plan §4.1): write areal-feature
    // polygons into the stencil buffer so subsequent data-layer rendering is
    // clipped to feature interiors. Returns false when the GL context has no
    // stencil buffer (caller then hides data layers entirely).
    // Usage:  if (clipMaskBegin(fs, vp)) { ...data layers...; clipMaskEnd(); }
    // GL-drawn popup panel (no wx widgets), anchored to a feature lon/lat
    static void setPopup(const wxString &text, double lon, double lat);
    static void clearPopup();

    static bool clipMaskBegin(const FeatureSet &fs, PlugIn_ViewPort *vp);
    static void clipMaskEnd();
};

#endif // NCDF_FEATURE_RENDER_H

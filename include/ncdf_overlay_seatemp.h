#ifndef NCDF_OVERLAY_SEATEMP_H
#define NCDF_OVERLAY_SEATEMP_H

#include "ncdf_overlay_scalar.h"

// Sea surface temperature (SST) overlay: color map + isolines
// 仅保留本图层的差异部分，公共实现见 ScalarOverlayBase（ncdf_overlay_scalar.h）。
struct SeaTempOverlay : ScalarOverlayBase {
    const char *logTag() const override { return "sst"; }
    bool   hasData(MainDialog &gui) const override;
    double valueAt(MainDialog &gui, int i, int j) const override;
    size_t dataSize(MainDialog &gui) const override;
    static wxColour GetColor(double v);
    ScalarColorFn colorFunc() const override { return &GetColor; }
    float  lutMin() const override { return -2.0f; }
    float  lutMax() const override { return 32.0f; }
};

#endif // NCDF_OVERLAY_SEATEMP_H

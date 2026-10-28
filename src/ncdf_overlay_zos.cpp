// ZosOverlay —— 仅差异部分，公共实现见 ncdf_overlay_scalar.cpp
#include "ncdf_overlay_zos.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"

bool ZosOverlay::hasData(MainDialog &gui) const { return gui.myMessage.hasZosData(); }
double ZosOverlay::valueAt(MainDialog &gui, int i, int j) const { return gui.myMessage.getZos(i, j); }
size_t ZosOverlay::dataSize(MainDialog &gui) const { return gui.myMessage.zos.size(); }

// Color map: Sea surface height (Zos) overlay: color map + isolines
wxColour ZosOverlay::GetColor(double v) {
// RdBu_r diverging: blue (negative) -> cream (geoid zero) -> red (positive)
    static const double stops[][4] = {
        {-2.0,  30,  60, 140}, {-1.5,  60, 110, 180}, {-1.0, 110, 160, 210},
        {-0.5, 170, 200, 230}, { 0.0, 240, 240, 225}, { 0.5, 245, 190, 160},
        { 1.0, 235, 130,  90}, { 1.5, 210,  70,  50}, { 2.0, 170,  30,  30},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 9, v, false);
}

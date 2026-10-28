// SeaTempOverlay —— 仅差异部分，公共实现见 ncdf_overlay_scalar.cpp
#include "ncdf_overlay_seatemp.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"

bool SeaTempOverlay::hasData(MainDialog &gui) const { return gui.myMessage.hasSSTData(); }
double SeaTempOverlay::valueAt(MainDialog &gui, int i, int j) const { return gui.myMessage.getSST(i, j); }
size_t SeaTempOverlay::dataSize(MainDialog &gui) const { return gui.myMessage.sst.size(); }

// Color map: Sea surface temperature (SST) overlay: color map + isolines
wxColour SeaTempOverlay::GetColor(double v) {
// NOAA OSPO SST palette style: purple-blue (cold) -> cyan -> green -> yellow -> red (hot)
    static const double stops[][4] = {
        {-2.0, 120, 130, 220}, {0.0,   70, 100, 225}, {2.0,   30,  60, 210},
        { 5.0,  15,  25, 190}, {8.0,    0, 100, 200}, {10.0,   0, 160, 185},
        {13.0,   0, 200, 130}, {16.0,  50, 220,  70}, {19.0, 130, 230,  25},
        {22.0, 205, 220,  25}, {25.0, 240, 185,  25}, {28.0, 240, 130,  25},
        {30.0, 225,  80,  30}, {32.0, 195,  45,  45},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 14, v, false);
}

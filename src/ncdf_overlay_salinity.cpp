// SalinityOverlay —— 仅差异部分，公共实现见 ncdf_overlay_scalar.cpp
#include "ncdf_overlay_salinity.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"

bool SalinityOverlay::hasData(MainDialog &gui) const { return gui.myMessage.hasSalData(); }
double SalinityOverlay::valueAt(MainDialog &gui, int i, int j) const { return gui.myMessage.getSal(i, j); }
size_t SalinityOverlay::dataSize(MainDialog &gui) const { return gui.myMessage.salinity.size(); }

// Color map: Salinity overlay: color map + isolines
wxColour SalinityOverlay::GetColor(double v) {
// cmocean "haline" style: pale sand (low) -> mint -> teal -> deep blue -> purple (high)
    static const double stops[][4] = {
        {30.0, 245, 235, 195}, {31.0, 210, 225, 185}, {32.0, 170, 210, 175},
        {33.0, 125, 195, 172}, {34.0,  80, 180, 170}, {35.0,  55, 165, 172},
        {36.0,  30, 140, 168}, {37.0,  28, 105, 152}, {38.0,  30,  70, 130},
        {39.0,  40,  30,  90},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 10, v, false);
}

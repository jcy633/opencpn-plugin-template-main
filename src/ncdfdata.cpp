/*
    Copyright (c) 2011, Konni <email>
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
        * Neither the name of the <organization> nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY Konni <email> ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL Konni <email> BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

//#include "complex.h"

#include "ncdfdata.h"
#include "ncdf_reader.h"


double ncdfDataMessage::getInterpolatedValue(const ncdfDataMessage& msg, const double* data,
                                            double px, double py, bool numericalInterpolation) const
{
	if (!data) return ncdf_NOTDEF;
	if (!isPointInMap(msg, px, py)) {
		px += 360.0;
		if (!isPointInMap(msg, px, py)) {
			px -= 2 * 360.0;
			if (!isPointInMap(msg, px, py)) return ncdf_NOTDEF;
		}
	}

	double dataEnd = msg.lastGridPointLong + msg.iDirectionIncr;
	while (px > dataEnd) px -= 360.0;
	while (px < msg.firstGridPointLong) px += 360.0;

	if (fabs(msg.iDirectionIncr) < 1e-10 || fabs(msg.jDirectionIncr) < 1e-10)
		return ncdf_NOTDEF;
	double pi = (px - msg.firstGridPointLong) / msg.iDirectionIncr;
	double pj = (py - msg.firstGridPointLat) / msg.jDirectionIncr;

	int i0 = (int)pi;
	int j0 = (int)pj;
	double dx = pi - i0;
	double dy = pj - j0;

	int ni = msg.noPointsParallel;
	int nj = msg.noPointsMeridian;
	int i1 = i0 + 1;
	double lonRange = fabs(msg.lastGridPointLong - msg.firstGridPointLong);
	if (i1 >= ni) {
		i1 = (lonRange + fabs(msg.iDirectionIncr) >= 360) ? 0 : i0;
	}

	// Bilinear interpolation from flat array [j * ni + i]
	auto valAt = [&](int ii, int jj) -> double {
		if (ii < 0 || ii >= ni || jj < 0 || jj >= nj) return ncdf_NOTDEF;
		return data[jj * ni + ii];
	};

	double v00 = valAt(i0, j0);
	double v10 = valAt(i1, j0);
	double v01 = valAt(i0, j0 + 1);
	double v11 = valAt(i1, j0 + 1);

	// Count valid neighbors
	int nbval = 0;
	if (v00 != ncdf_NOTDEF) nbval++;
	if (v10 != ncdf_NOTDEF) nbval++;
	if (v01 != ncdf_NOTDEF) nbval++;
	if (v11 != ncdf_NOTDEF) nbval++;

	if (nbval == 0) return ncdf_NOTDEF;
	if (nbval < 4 && numericalInterpolation) {
		// Fall back to nearest neighbor
		double minDist = 1e10;
		double bestVal = ncdf_NOTDEF;
		struct { double v; double dx; double dy; } pts[] = {
			{v00, dx, dy}, {v10, 1-dx, dy}, {v01, dx, 1-dy}, {v11, 1-dx, 1-dy}
		};
		for (auto& p : pts) {
			if (p.v != ncdf_NOTDEF) {
				double d = p.dx * p.dx + p.dy * p.dy;
				if (d < minDist) { minDist = d; bestVal = p.v; }
			}
		}
		return bestVal;
	}

	// Bilinear interpolation
	return v00 * (1-dx) * (1-dy) + v10 * dx * (1-dy)
	     + v01 * (1-dx) * dy + v11 * dx * dy;
}

// Joint UV interpolation with Hermite smoothing (GRIB pattern)
// One boundary check, one index calculation for both components
bool ncdfDataMessage::getInterpolatedUV(const ncdfDataMessage& msg, double px, double py,
                                        double &uOut, double &vOut) const
{
	if (!msg.hasCurrent()) return false;

	// Single boundary check for both components
	if (!isPointInMap(msg, px, py)) {
		px += 360.0;
		if (!isPointInMap(msg, px, py)) {
			px -= 2 * 360.0;
			if (!isPointInMap(msg, px, py)) return false;
		}
	}

	double dataEnd = msg.lastGridPointLong + msg.iDirectionIncr;
	while (px > dataEnd) px -= 360.0;
	while (px < msg.firstGridPointLong) px += 360.0;

	if (fabs(msg.iDirectionIncr) < 1e-10 || fabs(msg.jDirectionIncr) < 1e-10)
		return false;

	// Single index calculation
	double pi = (px - msg.firstGridPointLong) / msg.iDirectionIncr;
	double pj = (py - msg.firstGridPointLat) / msg.jDirectionIncr;
	int i0 = (int)pi, j0 = (int)pj;
	int i1 = i0 + 1, j1 = j0 + 1;

	int ni = msg.noPointsParallel, nj = msg.noPointsMeridian;
	if (i0 < 0 || j0 < 0 || i0 >= ni || j0 >= nj) return false;
	double lonRange = fabs(msg.lastGridPointLong - msg.firstGridPointLong);
	if (i1 >= ni) i1 = (lonRange + fabs(msg.iDirectionIncr) >= 360) ? 0 : i0;
	if (j1 >= nj) j1 = j0;

	// Read 4 corners for both U and V (8 reads, not 16)
	double dx = pi - i0, dy = pj - j0;
	const double* uArr = msg.ucurr.data();
	const double* vArr = msg.vcurr.data();
	int r0 = j0 * ni, r1 = j1 * ni;
	double u00 = uArr[r0 + i0], u10 = uArr[r0 + i1];
	double u01 = uArr[r1 + i0], u11 = uArr[r1 + i1];
	double v00 = vArr[r0 + i0], v10 = vArr[r0 + i1];
	double v01 = vArr[r1 + i0], v11 = vArr[r1 + i1];

	// Count valid corners
	int nbval = 0;
	if (u00 != ncdf_NOTDEF && v00 != ncdf_NOTDEF) nbval++;
	if (u10 != ncdf_NOTDEF && v10 != ncdf_NOTDEF) nbval++;
	if (u01 != ncdf_NOTDEF && v01 != ncdf_NOTDEF) nbval++;
	if (u11 != ncdf_NOTDEF && v11 != ncdf_NOTDEF) nbval++;

	if (nbval == 0) return false;

	// Pseudo-Hermite smoothing (GRIB pattern)
	dx = (3.0 - 2.0 * dx) * dx * dx;
	dy = (3.0 - 2.0 * dy) * dy * dy;

	if (nbval == 4) {
		uOut = u00 * (1-dx) * (1-dy) + u10 * dx * (1-dy)
		     + u01 * (1-dx) * dy + u11 * dx * dy;
		vOut = v00 * (1-dx) * (1-dy) + v10 * dx * (1-dy)
		     + v01 * (1-dx) * dy + v11 * dx * dy;
		return true;
	}

	// Partial: nearest valid corner
	struct { double u, v, d; } pts[] = {
		{u00, v00, dx*dx + dy*dy},
		{u10, v10, (1-dx)*(1-dx) + dy*dy},
		{u01, v01, dx*dx + (1-dy)*(1-dy)},
		{u11, v11, (1-dx)*(1-dx) + (1-dy)*(1-dy)}
	};
	double minD = 1e10;
	bool found = false;
	for (auto& p : pts) {
		if (p.u != ncdf_NOTDEF && p.v != ncdf_NOTDEF && p.d < minD) {
			minD = p.d; uOut = p.u; vOut = p.v; found = true;
		}
	}
	return found;
}


bool ncdfDataMessage::isPointInMap(const ncdfDataMessage& g2message, double x, double y) const
{
	return isXInMap(g2message, x) && isYInMap(g2message, y);
	/*    if (Dj < 0)
	return x>=Lo1 && y<=La1 && x<=Lo1+(Ni-1)*Di && y>=La1+(Nj-1)*Dj;
	else
	return x>=Lo1 && y>=La1 && x<=Lo1+(Ni-1)*Di && y<=La1+(Nj-1)*Dj;*/
}

bool ncdfDataMessage::isXInMap(const ncdfDataMessage& g2message, double x) const
{
	wxDouble lastlon = g2message.lastGridPointLong;

	// Handle global data wrapping (GRIB pattern)
	// If data covers full 360°, accept any longitude
	double range = fabs(g2message.lastGridPointLong - g2message.firstGridPointLong);
	if (range + fabs(g2message.iDirectionIncr) >= 360)
		return true;

	if (g2message.firstGridPointLong > 180. && g2message.lastGridPointLong < 180.0)
		lastlon = g2message.lastGridPointLong + 360;

	if (g2message.iDirectionIncr > 0)
		return x >= g2message.firstGridPointLong && x <= lastlon;
	else
		return x >= lastlon && x <= g2message.firstGridPointLong;
}

bool ncdfDataMessage::isYInMap(const ncdfDataMessage& g2message, double y) const
{
	if (g2message.jDirectionIncr < 0)
		return y <= g2message.firstGridPointLat && y >= g2message.lastGridPointLat;
	else
		return y >= g2message.firstGridPointLat && y <= g2message.lastGridPointLat;
}

// Float version: bilinear interpolation from 2D float grid (row pointers)
double ncdfDataMessage::getInterpolatedValueFloat(float* const* grid, int ni, int nj,
                                                   double firstLat, double lastLat,
                                                   double firstLon, double lastLon,
                                                   double iIncr, double jIncr,
                                                   double px, double py) const
{
	if (!grid) return ncdf_NOTDEF;

	// Normalize longitude bounds once
	double lonMin = firstLon, lonMax = lastLon;
	if (lonMin > lonMax) { double t = lonMin; lonMin = lonMax; lonMax = t; }

	auto inX = [&](double x) -> bool {
		return x >= lonMin && x <= lonMax;
	};
	auto inY = [&](double y) -> bool {
		if (jIncr < 0) return y <= firstLat && y >= lastLat;
		return y >= firstLat && y <= lastLat;
	};

	if (!inX(px) || !inY(py)) {
		px += 360.0;
		if (!inX(px) || !inY(py)) {
			px -= 2 * 360.0;
			if (!inX(px) || !inY(py)) {
				FILE *dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_debug.log", "a");
				if (dbg) { fprintf(dbg, "[interp:float] BOUNDS FAIL: px=%.4f py=%.4f lonMin=%.4f lonMax=%.4f firstLat=%.4f lastLat=%.4f iIncr=%.6f jIncr=%.6f\n", px, py, lonMin, lonMax, firstLat, lastLat, iIncr, jIncr); fclose(dbg); }
				return ncdf_NOTDEF;
			}
		}
	}

	double dataEnd = lonMax + iIncr;
	while (px > dataEnd) px -= 360.0;
	while (px < lonMin) px += 360.0;

	if (fabs(iIncr) < 1e-10 || fabs(jIncr) < 1e-10) return ncdf_NOTDEF;

	double pi = (px - lonMin) / iIncr;
	double pj = (py - firstLat) / jIncr;
	int i0 = (int)pi;
	int j0 = (int)pj;
	double dx = pi - i0;
	double dy = pj - j0;

	int i1 = i0 + 1;
	double lonRange = lonMax - lonMin;
	if (i1 >= ni) {
		i1 = (lonRange + fabs(iIncr) >= 360) ? 0 : i0;
	}

	auto valAt = [&](int ii, int jj) -> float {
		if (ii < 0 || ii >= ni || jj < 0 || jj >= nj) return NAN;
		return grid[jj][ii];
	};

	float v00 = valAt(i0, j0);
	float v10 = valAt(i1, j0);
	float v01 = valAt(i0, j0 + 1);
	float v11 = valAt(i1, j0 + 1);

	int nbval = 0;
	if (isfinite(v00)) nbval++;
	if (isfinite(v10)) nbval++;
	if (isfinite(v01)) nbval++;
	if (isfinite(v11)) nbval++;

	if (nbval == 0) {
		FILE *dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_debug.log", "a");
		if (dbg) { fprintf(dbg, "[interp:float] ALL NAN: px=%.4f py=%.4f i0=%d j0=%d i1=%d ni=%d nj=%d v00=%.2f v10=%.2f v01=%.2f v11=%.2f\n", px, py, i0, j0, i1, (int)ni, (int)nj, v00, v10, v01, v11); fclose(dbg); }
		return ncdf_NOTDEF;
	}
	if (nbval < 4) {
		// Nearest neighbor fallback
		double minDist = 1e10;
		double bestVal = ncdf_NOTDEF;
		struct { float v; double ddx; double ddy; } pts[] = {
			{v00, dx, dy}, {v10, 1-dx, dy}, {v01, dx, 1-dy}, {v11, 1-dx, 1-dy}
		};
		for (auto& p : pts) {
			if (isfinite(p.v)) {
				double d = p.ddx * p.ddx + p.ddy * p.ddy;
				if (d < minDist) { minDist = d; bestVal = p.v; }
			}
		}
		return bestVal;
	}

	return (double)(v00 * (1-dx) * (1-dy) + v10 * dx * (1-dy)
	              + v01 * (1-dx) * dy + v11 * dx * dy);
}

// Float version: joint UV interpolation from cached float grids
bool ncdfDataMessage::getInterpolatedUVFloat(float* const* uGrid, float* const* vGrid,
                                              int ni, int nj,
                                              double firstLat, double lastLat,
                                              double firstLon, double lastLon,
                                              double iIncr, double jIncr,
                                              double px, double py,
                                              double &uOut, double &vOut) const
{
	if (!uGrid || !vGrid) return false;
	if (fabs(iIncr) < 1e-10 || fabs(jIncr) < 1e-10) return false;

	// Normalize longitude bounds
	double lonMin = firstLon, lonMax = lastLon;
	if (lonMin > lonMax) { double t = lonMin; lonMin = lonMax; lonMax = t; }

	// Boundary check with 360 wrapping
	auto inX = [&](double x) -> bool { return x >= lonMin && x <= lonMax; };
	auto inY = [&](double y) -> bool {
		if (jIncr < 0) return y <= firstLat && y >= lastLat;
		return y >= firstLat && y <= lastLat;
	};
	if (!inX(px) || !inY(py)) {
		px += 360.0;
		if (!inX(px) || !inY(py)) {
			px -= 2 * 360.0;
			if (!inX(px) || !inY(py)) return false;
		}
	}
	double dataEnd = lonMax + iIncr;
	while (px > dataEnd) px -= 360.0;
	while (px < lonMin) px += 360.0;

	double pi = (px - lonMin) / iIncr;
	double pj = (py - firstLat) / jIncr;
	int i0 = (int)pi, j0 = (int)pj;
	int i1 = i0 + 1, j1 = j0 + 1;

	if (i0 < 0 || j0 < 0 || i0 >= ni || j0 >= nj) return false;
	double lonRange = lonMax - lonMin;
	if (i1 >= ni) i1 = (lonRange + fabs(iIncr) >= 360) ? 0 : i0;
	if (j1 >= nj) j1 = j0;

	double dx = pi - i0, dy = pj - j0;
	int r0 = j0, r1 = j1;
	float u00 = uGrid[r0][i0], u10 = uGrid[r0][i1];
	float u01 = uGrid[r1][i0], u11 = uGrid[r1][i1];
	float v00 = vGrid[r0][i0], v10 = vGrid[r0][i1];
	float v01 = vGrid[r1][i0], v11 = vGrid[r1][i1];

	int nbval = 0;
	if (isfinite(u00) && isfinite(v00)) nbval++;
	if (isfinite(u10) && isfinite(v10)) nbval++;
	if (isfinite(u01) && isfinite(v01)) nbval++;
	if (isfinite(u11) && isfinite(v11)) nbval++;

	if (nbval == 0) return false;

	// Bilinear interpolation (same as getInterpolatedValueFloat)
	if (nbval == 4) {
		uOut = u00 * (1-dx) * (1-dy) + u10 * dx * (1-dy)
		     + u01 * (1-dx) * dy + u11 * dx * dy;
		vOut = v00 * (1-dx) * (1-dy) + v10 * dx * (1-dy)
		     + v01 * (1-dx) * dy + v11 * dx * dy;
		return true;
	}

	// Partial: nearest valid corner
	struct { float u, v; double d; } pts[] = {
		{u00, v00, dx*dx + dy*dy},
		{u10, v10, (1-dx)*(1-dx) + dy*dy},
		{u01, v01, dx*dx + (1-dy)*(1-dy)},
		{u11, v11, (1-dx)*(1-dx) + (1-dy)*(1-dy)}
	};
	double minD = 1e10;
	bool found = false;
	for (auto& p : pts) {
		if (isfinite(p.u) && isfinite(p.v) && p.d < minD) {
			minD = p.d; uOut = p.u; vOut = p.v; found = true;
		}
	}
	return found;
}



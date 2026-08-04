#include "ncdf_pi.h"
#include "ncdf_reader.h"
#include "ncdf.h"
#include "helper.h"
#include "ncdfdata.h"

class ncdfDataMessage;

extern void ncdfLog(const char* format, ...);

// FillGrid: neighbor mean filling for missing data (GRIB pattern)
// Two passes: vertical neighbors, then horizontal neighbors
// Fills ncdf_NOTDEF cells with mean of 2+ valid neighbors
static void FillGrid(double **grid, int ni, int nj)
{
    if (!grid || ni < 3 || nj < 3) return;

    // Pass 1: vertical neighbors
    for (int i = 0; i < ni; i++) {
        for (int j = 1; j < nj - 1; j++) {
            if (grid[j][i] == ncdf_NOTDEF) {
                double acc = 0;
                int cnt = 0;
                if (grid[j - 1][i] != ncdf_NOTDEF) { acc += grid[j - 1][i]; cnt++; }
                if (grid[j + 1][i] != ncdf_NOTDEF) { acc += grid[j + 1][i]; cnt++; }
                if (cnt >= 2) grid[j][i] = acc / cnt;
            }
        }
    }

    // Pass 2: horizontal neighbors
    for (int j = 0; j < nj; j++) {
        for (int i = 1; i < ni - 1; i++) {
            if (grid[j][i] == ncdf_NOTDEF) {
                double acc = 0;
                int cnt = 0;
                if (grid[j][i - 1] != ncdf_NOTDEF) { acc += grid[j][i - 1]; cnt++; }
                if (grid[j][i + 1] != ncdf_NOTDEF) { acc += grid[j][i + 1]; cnt++; }
                if (cnt >= 2) grid[j][i] = acc / cnt;
            }
        }
    }
}

// FillAllGrids: unified pass over all grids (avoids 4 separate FillGrid calls)
// Single iteration over ni×nj is more cache-friendly than 4 separate passes
static void FillAllGrids(double **gridu, double **gridv,
                         double **gridSST, double **gridSalinity,
                         int ni, int nj)
{
    if (ni < 3 || nj < 3) return;

    // Pass 1: vertical neighbors (all grids in one pass)
    for (int i = 0; i < ni; i++) {
        for (int j = 1; j < nj - 1; j++) {
            // Helper lambda-like macro: fill one cell if it's ncdf_NOTDEF
            #define FILL_V(g) \
                if (g && g[j][i] == ncdf_NOTDEF) { \
                    double acc = 0; int cnt = 0; \
                    if (g[j-1][i] != ncdf_NOTDEF) { acc += g[j-1][i]; cnt++; } \
                    if (g[j+1][i] != ncdf_NOTDEF) { acc += g[j+1][i]; cnt++; } \
                    if (cnt >= 2) g[j][i] = acc / cnt; \
                }
            FILL_V(gridu)
            FILL_V(gridv)
            FILL_V(gridSST)
            FILL_V(gridSalinity)
            #undef FILL_V
        }
    }

    // Pass 2: horizontal neighbors (all grids in one pass)
    for (int j = 0; j < nj; j++) {
        for (int i = 1; i < ni - 1; i++) {
            #define FILL_H(g) \
                if (g && g[j][i] == ncdf_NOTDEF) { \
                    double acc = 0; int cnt = 0; \
                    if (g[j][i-1] != ncdf_NOTDEF) { acc += g[j][i-1]; cnt++; } \
                    if (g[j][i+1] != ncdf_NOTDEF) { acc += g[j][i+1]; cnt++; } \
                    if (cnt >= 2) g[j][i] = acc / cnt; \
                }
            FILL_H(gridu)
            FILL_H(gridv)
            FILL_H(gridSST)
            FILL_H(gridSalinity)
            #undef FILL_H
        }
    }
}

ncdfReader::ncdfReader(MainDialog *md)
{
	gui = md;
	ncdfMessage1.data = NULL;
	ncdfMessage1.bmpMask = NULL;
	ncdfData1 = NULL;

	isReading = false;
}

ncdfReader::~ncdfReader()
{
	if(ncdfData1)
		delete ncdfData1;
}

void ncdfReader::readncdfFile(const ncdfDataMessage& dataMessage)
{
	bool hasCurrent = (dataMessage.ucurr && dataMessage.vcurr);
	bool hasSST = (dataMessage.sst != NULL) || dataMessage.hasSeaTemp;
	bool hasSal = (dataMessage.salinity != NULL) || dataMessage.hasSalinity;

	if (!hasCurrent && !hasSST && !hasSal) {
		return;
	}

	ncdfLog("[ncdf] readncdfFile: entering, meridian=%d, parallel=%d, points=%d, hasCurrent=%d, hasSST=%d, hasSal=%d\n",
		(int)dataMessage.noPointsMeridian, (int)dataMessage.noPointsParallel,
		dataMessage.numberOfPoints, (int)hasCurrent, (int)hasSST, (int)hasSal);

	// Save OLD grid pointers BEFORE overwriting (atomic swap pattern)
	double **oldGridu = gui->gridu;
	double **oldGridv = gui->gridv;
	double **oldGridMag = gui->gridMag;
	double **oldGridSST = gui->gridSST;
	double **oldGridSalinity = gui->gridSalinity;
	wxUint32 oldMeridian = gui->myMessage.noPointsMeridian;
	gui->gridu = NULL;
	gui->gridv = NULL;
	gui->gridMag = NULL;
	gui->gridSST = NULL;
	gui->gridSalinity = NULL;

	// Free old coordinate arrays before assignment
	if (gui->myMessage.latValues) { free(gui->myMessage.latValues); gui->myMessage.latValues = NULL; }
	if (gui->myMessage.lonValues) { free(gui->myMessage.lonValues); gui->myMessage.lonValues = NULL; }
	if (gui->myMessage.timeValues) { free(gui->myMessage.timeValues); gui->myMessage.timeValues = NULL; }

	gui->myMessage = dataMessage;
	ncdfLog("[ncdf] readncdfFile: myMessage copied\n");

	// Build new current grids (old grids already NULL, no cleanup needed)
	if (hasCurrent) {
		gui->gridu = new double*[dataMessage.noPointsMeridian];
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			gui->gridu[i] = new double[dataMessage.noPointsParallel];
		}
		int c = 0;
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; i++) {
			for (wxUint32 j = 0; j < dataMessage.noPointsParallel; j++) {
				gui->gridu[i][j] = dataMessage.ucurr[c];
				c++;
			}
		}
		ncdfLog("[ncdf] readncdfFile: gridu built\n");

		gui->gridv = new double*[dataMessage.noPointsMeridian];
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			gui->gridv[i] = new double[dataMessage.noPointsParallel];
		}
		c = 0;
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; i++) {
			for (wxUint32 j = 0; j < dataMessage.noPointsParallel; j++) {
				gui->gridv[i][j] = dataMessage.vcurr[c];
				c++;
			}
		}
		ncdfLog("[ncdf] readncdfFile: gridv built\n");

		// Build magnitude grid: sqrt(u² + v²)
		gui->gridMag = new double*[dataMessage.noPointsMeridian];
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			gui->gridMag[i] = new double[dataMessage.noPointsParallel];
		}
		ncdfLog("[ncdf] readncdfFile: gridMag allocated\n");

		// FillGrid is deferred: called once after ALL grids are built (see below)
	}

	// Build new SST grid
	gui->hasSeaTemp = hasSST;
	if (hasSST && dataMessage.sst) {
		gui->gridSST = new double*[dataMessage.noPointsMeridian];
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			gui->gridSST[i] = new double[dataMessage.noPointsParallel];
		}
		int c_sst = 0;
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; i++) {
			for (wxUint32 j = 0; j < dataMessage.noPointsParallel; j++) {
				gui->gridSST[i][j] = dataMessage.sst[c_sst];
				c_sst++;
			}
		}
		ncdfLog("[ncdf] readncdfFile: gridSST built\n");
	}

	// Build new salinity grid
	gui->hasSalinity = hasSal;
	ncdfLog("[ncdf] readncdfFile: salinity check: salinity=%p hasSalinity=%d hasSal=%d\n",
		(void*)dataMessage.salinity, (int)dataMessage.hasSalinity, (int)hasSal);
	if (hasSal && dataMessage.salinity) {
		gui->gridSalinity = new double*[dataMessage.noPointsMeridian];
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			gui->gridSalinity[i] = new double[dataMessage.noPointsParallel];
		}
		int c_sal = 0;
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; i++) {
			for (wxUint32 j = 0; j < dataMessage.noPointsParallel; j++) {
				gui->gridSalinity[i][j] = dataMessage.salinity[c_sal];
				c_sal++;
			}
		}
		ncdfLog("[ncdf] readncdfFile: gridSalinity built\n");
	}

	// Unified FillGrid: single pass over all grids (cache-friendly)
	FillAllGrids(gui->gridu, gui->gridv, gui->gridSST, gui->gridSalinity,
	             dataMessage.noPointsParallel, dataMessage.noPointsMeridian);
	ncdfLog("[ncdf] readncdfFile: FillAllGrids done\n");

	// Build magnitude grid: sqrt(u² + v²) — AFTER FillGrid so u/v are smoothed
	if (hasCurrent && gui->gridMag) {
		for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
			for (wxUint32 j = 0; j < dataMessage.noPointsParallel; ++j) {
				double u = gui->gridu[i][j], v = gui->gridv[i][j];
				gui->gridMag[i][j] = (u != ncdf_NOTDEF && v != ncdf_NOTDEF) ? sqrt(u*u + v*v) : ncdf_NOTDEF;
			}
		}
		ncdfLog("[ncdf] readncdfFile: gridMag built\n");
	}

	// Free OLD grids AFTER new ones are built (atomic swap complete)
	if (oldGridu) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridu[i];
		delete[] oldGridu;
	}
	if (oldGridv) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridv[i];
		delete[] oldGridv;
	}
	if (oldGridMag) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridMag[i];
		delete[] oldGridMag;
	}
	if (oldGridSST) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridSST[i];
		delete[] oldGridSST;
	}
	if (oldGridSalinity) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridSalinity[i];
		delete[] oldGridSalinity;
	}

	wxDateTime ddt;
	ddt = dataMessage.dataDateTime;
	ncdfLog("[ncdf] readncdfFile: dataDateTime valid=%d\n", (int)ddt.IsValid());

	wxString ls;
	if (ddt.IsValid()) {
		ls = ddt.Format(_T("%Y-%m-%d %H:00"));
	} else {
		ls = _T("No date/time");
	}
	gui->m_staticTextDateTime->SetLabel(ls);
	ncdfLog("[ncdf] readncdfFile: datetime label set, calling reset/setData...\n");

	gui->pPlugIn->GetncdfOverlayFactory()->reset();
	gui->pPlugIn->GetncdfOverlayFactory()->setData(gui,
							   gui->pPlugIn,
							   dataMessage,
							   dataMessage.numberOfPoints,
							   dataMessage.firstGridPointLat,
							   dataMessage.firstGridPointLong,
							   dataMessage.lastGridPointLat,
							   dataMessage.lastGridPointLong
							   );
	isReading = false;

	ncdfLog("[ncdf] readncdfFile: completed, readyToRender=%d, points=%d, meridian=%d, parallel=%d\n",
		gui->pPlugIn->GetncdfOverlayFactory()->isReadyToRender(),
		dataMessage.numberOfPoints,
		dataMessage.noPointsMeridian,
		dataMessage.noPointsParallel);
}




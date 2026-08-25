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

ncdfReader::ncdfReader(MainDialog *md)
{
	gui = md;
	isReading = false;
}

ncdfReader::~ncdfReader()
{
}

void ncdfReader::readncdfFile(const ncdfDataMessage& dataMessage)
{
	FILE *dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: ENTER\n"); fflush(dbg); fclose(dbg); }

	bool hasCurrent = dataMessage.hasCurrent();
	bool hasSST = dataMessage.hasSSTData();
	bool hasSal = dataMessage.hasSalData();

	// Free old coordinate arrays before deep copy
	if (gui->myMessage.latValues) { free(gui->myMessage.latValues); gui->myMessage.latValues = NULL; }
	if (gui->myMessage.lonValues) { free(gui->myMessage.lonValues); gui->myMessage.lonValues = NULL; }
	if (gui->myMessage.timeValues) { free(gui->myMessage.timeValues); gui->myMessage.timeValues = NULL; }

	dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: A-before copy\n"); fflush(dbg); fclose(dbg); }

	gui->myMessage = dataMessage;  // deep copy

	dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: B-after copy, calling reset, factory=%p plugin=%p\n",
		(void*)gui->pPlugIn->GetncdfOverlayFactory(), (void*)gui->pPlugIn); fflush(dbg); fclose(dbg); }

	ncdfOverlayFactory *pof = gui->pPlugIn->GetncdfOverlayFactory();
	if (pof) {
		pof->m_bReadyToRender = false;
		dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
		if (dbg) { fprintf(dbg, "readncdfFile: B2-readyToRender=false\n"); fflush(dbg); fclose(dbg); }
	}

	dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: C-after reset\n"); fflush(dbg); fclose(dbg); }

	if (!hasCurrent && !hasSST && !hasSal) {
		gui->pPlugIn->GetncdfOverlayFactory()->setData(gui,
								   gui->pPlugIn,
								   dataMessage,
								   0, 0, 0, 0, 0);
		isReading = false;
		return;
	}

	wxDateTime ddt;
	ddt = dataMessage.dataDateTime;

	wxString ls;
	if (ddt.IsValid()) {
		ls = ddt.Format(_T("%Y-%m-%d %H:00"));
	} else {
		ls = _T("No date/time");
	}
	gui->m_staticTextDateTime->SetLabel(ls);

	dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: D-before setData\n"); fflush(dbg); fclose(dbg); }

	gui->pPlugIn->GetncdfOverlayFactory()->setData(gui,
							   gui->pPlugIn,
							   dataMessage,
							   dataMessage.numberOfPoints,
							   dataMessage.firstGridPointLat,
							   dataMessage.firstGridPointLong,
							   dataMessage.lastGridPointLat,
							   dataMessage.lastGridPointLong
							   );

	// Precompute all overlay data (grids + B-spline coefficients) and free CPU buffers
	gui->pPlugIn->GetncdfOverlayFactory()->prepareAllOverlays();

	isReading = false;

	dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "readncdfFile: E-DONE\n"); fflush(dbg); fclose(dbg); }
}




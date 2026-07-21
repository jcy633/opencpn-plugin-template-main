#include "ncdf_pi.h"
#include "ncdf_reader.h"
#include "ncdf.h"
#include "helper.h"
#include "ncdfdata.h"

class ncdfDataMessage;

extern void ncdfLog(const char* format, ...);

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

	if (!hasCurrent && !hasSST) {
		return;
	}

	ncdfLog("[ncdf] readncdfFile: entering, meridian=%d, parallel=%d, points=%d, hasCurrent=%d, hasSST=%d\n",
		(int)dataMessage.noPointsMeridian, (int)dataMessage.noPointsParallel,
		dataMessage.numberOfPoints, (int)hasCurrent, (int)hasSST);

	// Save OLD grid pointers BEFORE overwriting (atomic swap pattern)
	double **oldGridu = gui->gridu;
	double **oldGridv = gui->gridv;
	double **oldGridSST = gui->gridSST;
	wxUint32 oldMeridian = gui->myMessage.noPointsMeridian;
	gui->gridu = NULL;
	gui->gridv = NULL;
	gui->gridSST = NULL;

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

	// Free OLD grids AFTER new ones are built (atomic swap complete)
	if (oldGridu) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridu[i];
		delete[] oldGridu;
	}
	if (oldGridv) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridv[i];
		delete[] oldGridv;
	}
	if (oldGridSST) {
		for (wxUint32 i = 0; i < oldMeridian; ++i) delete[] oldGridSST[i];
		delete[] oldGridSST;
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




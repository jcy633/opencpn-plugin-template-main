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
	
	// Save old grid dimensions before overwriting gui->myMessage
	wxUint32 oldNoPointsMeridian = gui->gridu ? gui->myMessage.noPointsMeridian : 0;
	
	ncdfLog("[ncdf] readncdfFile: copying myMessage...\n");
	gui->myMessage = dataMessage;
	ncdfLog("[ncdf] readncdfFile: myMessage copied\n");

	// Clean up old current grids
	if (gui->gridu) {
		for (wxUint32 i = 0; i < oldNoPointsMeridian; ++i) {
			delete[] gui->gridu[i];
		}
		delete[] gui->gridu;
		gui->gridu = NULL;
	}
	if (gui->gridv) {
		for (wxUint32 i = 0; i < oldNoPointsMeridian; ++i) {
			delete[] gui->gridv[i];
		}
		delete[] gui->gridv;
		gui->gridv = NULL;
	}

	// Build current grids only if u/v data is available
	if (hasCurrent) {
	gui->gridu = new double*[dataMessage.noPointsMeridian];

	for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) { // = NLAT
		gui->gridu[i] = new double[dataMessage.noPointsParallel];	// = NLON
	}
	ncdfLog("[ncdf] readncdfFile: gridu allocated\n");
	int c = 0;
	for (wxUint32 i = 0; i <dataMessage.noPointsMeridian; i++)    //This loops on the rows.Nj
	{
		for (wxUint32 j = 0; j<dataMessage.noPointsParallel; j++) //This loops on the columns.Ni
		{
				gui->gridu[i][j] = dataMessage.ucurr[c];
				c++;
		}
	}
	ncdfLog("[ncdf] readncdfFile: gridu data copied, about to allocate gridv...\n");
	gui->gridv = new double*[dataMessage.noPointsMeridian];
	ncdfLog("[ncdf] readncdfFile: gridv row pointers allocated\n");

	for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
		gui->gridv[i] = new double[dataMessage.noPointsParallel];
	}
	ncdfLog("[ncdf] readncdfFile: gridv arrays allocated\n");
	c = 0;
	for (wxUint32 i = 0; i <dataMessage.noPointsMeridian; i++)    //This loops on the rows.Nj
	{
		for (wxUint32 j = 0; j<dataMessage.noPointsParallel; j++) //This loops on the columns.Ni
		{

			gui->gridv[i][j] = dataMessage.vcurr[c];
			c++;
		}
	}
	ncdfLog("[ncdf] readncdfFile: gridv data copied, c=%d\n", c);
	} // end if (hasCurrent)

	// Build SST grid if available
	ncdfLog("[ncdf] readncdfFile: checking SST: hasSeaTemp=%d sst=%p\n",
		(int)dataMessage.hasSeaTemp, (void*)dataMessage.sst);
	gui->hasSeaTemp = (dataMessage.sst != NULL) || dataMessage.hasSeaTemp;
	if (gui->gridSST) {
		for (wxUint32 i = 0; i < gui->myMessage.noPointsMeridian; ++i) {
			delete[] gui->gridSST[i];
		}
		delete[] gui->gridSST;
		gui->gridSST = NULL;
	}
	if (dataMessage.hasSeaTemp && dataMessage.sst) {
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

	// Update hasSeaTemp based on current data
	gui->hasSeaTemp = (dataMessage.sst != NULL) || dataMessage.hasSeaTemp;

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




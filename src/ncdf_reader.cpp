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
	if (!dataMessage.ucurr || !dataMessage.vcurr) {
		return;
	}
	
	ncdfLog("[ncdf] readncdfFile: entering, meridian=%d, parallel=%d, points=%d\n",
		(int)dataMessage.noPointsMeridian, (int)dataMessage.noPointsParallel, dataMessage.numberOfPoints);
	
	// Save old grid dimensions before overwriting gui->myMessage
	wxUint32 oldNoPointsMeridian = gui->gridu ? gui->myMessage.noPointsMeridian : 0;
	
	ncdfLog("[ncdf] readncdfFile: copying myMessage...\n");
	gui->myMessage = dataMessage;
	ncdfLog("[ncdf] readncdfFile: myMessage copied\n");
	
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
	ncdfLog("[ncdf] readncdfFile: gridu data copied\n");
	gui->gridv = new double*[dataMessage.noPointsMeridian];

	for (wxUint32 i = 0; i < dataMessage.noPointsMeridian; ++i) {
		gui->gridv[i] = new double[dataMessage.noPointsParallel];
	}
	c = 0;
	for (wxUint32 i = 0; i <dataMessage.noPointsMeridian; i++)    //This loops on the rows.Nj
	{
		for (wxUint32 j = 0; j<dataMessage.noPointsParallel; j++) //This loops on the columns.Ni
		{

			gui->gridv[i][j] = dataMessage.vcurr[c];
			c++;
		}
	}

	wxDateTime ddt;
	ddt = dataMessage.dataDateTime;

    wxString ls = ddt.Format(_T("%a %d %b %Y %H:%M"));
	gui->m_staticTextDateTime->SetLabel(ls);
	ncdfLog("[ncdf] readncdfFile: calling reset/setData...\n");
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




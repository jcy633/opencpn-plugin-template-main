#ifndef ncdfREADER_H
#define ncdfREADER_H

#include <wx/wx.h>

class MainDialog;
class ncdfData;
class ncdfDataMessage;

class ncdfDataMessage
{
public:
	ncdfDataMessage() : ucurr(NULL), vcurr(NULL), uvlats(NULL), uvlons(NULL),
		sst(NULL), hasSeaTemp(false),
		latValues(NULL), lonValues(NULL), timeValues(NULL), /*depthValues(NULL),*/
		timeIndex(-1), /*depthIndex(0),*/ timeValid(false) {}
	
	ncdfDataMessage(const ncdfDataMessage& other) :
		ucurr(NULL), vcurr(NULL), uvlats(NULL), uvlons(NULL),
		sst(NULL), hasSeaTemp(false),
		latValues(NULL), lonValues(NULL), timeValues(NULL), timeIndex(-1) {
		copyFrom(other);
	}
	
	ncdfDataMessage& operator=(const ncdfDataMessage& other) {
		if (this != &other) {
			clear();
			copyFrom(other);
		}
		return *this;
	}
	
	~ncdfDataMessage() {
		freeAll();
	}

	void clear() {
		if (ucurr) { free(ucurr); ucurr = NULL; }
		if (vcurr) { free(vcurr); vcurr = NULL; }
		if (uvlats) { free(uvlats); uvlats = NULL; }
		if (uvlons) { free(uvlons); uvlons = NULL; }
		if (sst) { free(sst); sst = NULL; }
		// Note: latValues, lonValues, timeValues are NOT freed here.
		// clear() is used for reassignment (operator=) where copyFrom()
		// will immediately overwrite these pointers. The destructor calls
		// freeAll() to properly release all memory including coordinates.
	}

	// Free everything including coordinate arrays (used by destructor)
	void freeAll() {
		clear();
		if (latValues) { free(latValues); latValues = NULL; }
		if (lonValues) { free(lonValues); lonValues = NULL; }
		if (timeValues) { free(timeValues); timeValues = NULL; }
	}
	
private:
	void copyFrom(const ncdfDataMessage& other) {
		latLength = other.latLength;
		lonLength = other.lonLength;
		timeLength = other.timeLength;
		
		version = other.version;
		length = other.length;
		discipline = other.discipline;
		masterTableVersion = other.masterTableVersion;
		localTableVersion = other.localTableVersion;
		referenceTime = other.referenceTime;
		dt = other.dt;
		productionState = other.productionState;
		dataType = other.dataType;
		templateNo3 = other.templateNo3;
		noSectors = other.noSectors;
		noPointsParallel = other.noPointsParallel;
		noPointsMeridian = other.noPointsMeridian;
		firstGridPointLat = other.firstGridPointLat;
		firstGridPointLong = other.firstGridPointLong;
		lastGridPointLat = other.lastGridPointLat;
		lastGridPointLong = other.lastGridPointLong;
		iDirectionIncr = other.iDirectionIncr;
		jDirectionIncr = other.jDirectionIncr;
		resCompFlag = other.resCompFlag;
		scanMode = other.scanMode;
		templateNo4 = other.templateNo4;
		paramCategory = other.paramCategory;
		paramNo = other.paramNo;
		typeOfProcess = other.typeOfProcess;
		hoursAfterRefTime = other.hoursAfterRefTime;
		minutesAfterRefTime = other.minutesAfterRefTime;
		indicatorTimeRange = other.indicatorTimeRange;
		forcastTimeUnits = other.forcastTimeUnits;
		templateNo5 = other.templateNo5;
		referenceVal = other.referenceVal;
		binaryScaleFacor = other.binaryScaleFacor;
		decimalScaleFactor = other.decimalScaleFactor;
		noBits = other.noBits;
		typeOrgFieldValues = other.typeOrgFieldValues;
		scaledValueSurface1 = other.scaledValueSurface1;
		compressionType = other.compressionType;
		compressionRatio = other.compressionRatio;
		bmpMask = NULL;
		bmpSize = 0;
		data = NULL;
		dataDateTime = other.dataDateTime;
		minutesAfterStart = other.minutesAfterStart;
		numberOfPoints = other.numberOfPoints;
		fileName = other.fileName;
		timeIndex = other.timeIndex;
		timeValid = other.timeValid;
		/*depthIndex = other.depthIndex;
		depthLength = other.depthLength;*/
		
		if (other.latValues) {
			latValues = (wxDouble*)calloc(latLength, sizeof(wxDouble));
			memcpy(latValues, other.latValues, latLength * sizeof(wxDouble));
		}
		if (other.lonValues) {
			lonValues = (wxDouble*)calloc(lonLength, sizeof(wxDouble));
			memcpy(lonValues, other.lonValues, lonLength * sizeof(wxDouble));
		}
		if (other.timeValues) {
			timeValues = (double*)calloc(timeLength, sizeof(double));
			memcpy(timeValues, other.timeValues, timeLength * sizeof(double));
		}
		/*if (other.depthValues) {
			depthValues = (wxDouble*)calloc(depthLength, sizeof(wxDouble));
			memcpy(depthValues, other.depthValues, depthLength * sizeof(wxDouble));
		}*/
		
		size_t nbr_uv = latLength * lonLength;
		if (other.ucurr) {
			ucurr = (double*)calloc(nbr_uv, sizeof(double));
			memcpy(ucurr, other.ucurr, nbr_uv * sizeof(double));
		}
		if (other.vcurr) {
			vcurr = (double*)calloc(nbr_uv, sizeof(double));
			memcpy(vcurr, other.vcurr, nbr_uv * sizeof(double));
		}
		if (other.uvlats) {
			uvlats = (double*)calloc(nbr_uv, sizeof(double));
			memcpy(uvlats, other.uvlats, nbr_uv * sizeof(double));
		}
		if (other.uvlons) {
			uvlons = (double*)calloc(nbr_uv, sizeof(double));
			memcpy(uvlons, other.uvlons, nbr_uv * sizeof(double));
		}
		if (other.sst) {
			sst = (double*)calloc(nbr_uv, sizeof(double));
			memcpy(sst, other.sst, nbr_uv * sizeof(double));
		}
		hasSeaTemp = other.hasSeaTemp;
	}
	
public:
	double getInterpolatedValue(const ncdfDataMessage& g2message, double** grid, double px, double py, bool numericalInterpolation) const;
	bool isPointInMap(const ncdfDataMessage& g2message, double x, double y) const;
	bool isXInMap(const ncdfDataMessage& g2message, double x) const;
	bool isYInMap(const ncdfDataMessage& g2message, double y) const;
	bool hasValue(double** grid, wxUint32 nolat, wxUint32 nolon, unsigned int indexlon, unsigned int indexlat) const;
	bool		timeValid;
	wxUint8		version;
	wxUint64 	length;
	wxUint8		discipline;
	wxUint8		masterTableVersion;
	wxUint8		localTableVersion;
	wxUint8  	referenceTime;
	wxDateTime	dt;
	wxUint8		productionState;
	wxUint8		dataType;
	wxUint16	templateNo3;
	wxUint32	noSectors;
	wxUint32	noPointsParallel;
	wxUint32	noPointsMeridian;
	wxDouble	firstGridPointLat;
	wxDouble	firstGridPointLong;
	wxDouble	lastGridPointLat;
	wxDouble	lastGridPointLong;
	wxDouble	iDirectionIncr; // i = parallel
	wxDouble	jDirectionIncr; // j = Meridian;
	wxUint8		resCompFlag;
	wxUint8		scanMode;
	wxUint16	templateNo4;
	wxUint8		paramCategory;
	wxUint8		paramNo;
	wxUint8		typeOfProcess;
	wxUint16	hoursAfterRefTime;
	wxUint8		minutesAfterRefTime;
	wxUint8		indicatorTimeRange;
	wxUint32	forcastTimeUnits;
	wxUint16	templateNo5;
	wxFloat32	referenceVal;
	wxUint16	binaryScaleFacor;
	wxUint16	decimalScaleFactor;
	wxUint8		noBits;
	wxUint8		typeOrgFieldValues;
	wxUint32	scaledValueSurface1;
	wxUint8		compressionType;
	wxUint8		compressionRatio;
	wxInt8*		bmpMask;
	wxInt32		bmpSize;
	wxDouble*	data;
	wxDouble*    ucurr;
	wxDouble*    vcurr;
	wxDateTime  dataDateTime;
	int			minutesAfterStart;
	wxDouble* uvlats;
	wxDouble* uvlons;
	wxDouble* sst;
	bool hasSeaTemp;
	int			numberOfPoints;
	wxString   fileName;
	
	wxDouble* latValues;
	wxDouble* lonValues;
	double* timeValues;
	wxDouble* depthValues;
	size_t latLength;
	size_t lonLength;
	size_t timeLength;
	size_t depthLength;
	int timeIndex;
	int depthIndex;
};


struct message
{
	wxUint8		version;
	wxUint64 	length;
	wxUint8		discipline;
	// end Sector 0
	wxUint8		masterTableVersion;
	wxUint8		localTableVersion;
	wxUint8  	referenceTime;
	wxDateTime	dt;
	wxUint8		productionState;
	wxUint8		dataType;
	// end Secstor 1
	wxUint16	templateNo3;
	wxUint32	noSectors;
	wxUint32	noPointsParallel;
	wxUint32	noPointsMeridian;
	wxDouble	firstGridPointLat;
	wxDouble	firstGridPointLong;
	wxDouble	lastGridPointLat;
	wxDouble	lastGridPointLong;
	wxDouble	iDirectionIncr; // i = parallel
	wxDouble	jDirectionIncr; // j = Meridian;
	wxUint8		resCompFlag;
	wxUint8		scanMode;
	// end Secstor 3
	wxUint16	templateNo4;
	wxUint8		paramCategory;
	wxUint8		paramNo;
	wxUint8		typeOfProcess;
	wxUint16	hoursAfterRefTime;
	wxUint8		minutesAfterRefTime;
	wxUint8		indicatorTimeRange;
	wxUint32	forcastTimeUnits;
	// end Secstor 4
	wxUint16	templateNo5;
	wxFloat32	referenceVal;
	wxUint16	binaryScaleFacor;
	wxUint16	decimalScaleFactor;
	wxUint8		noBits;
	wxUint8		typeOrgFieldValues;
	wxUint32	scaledValueSurface1;
	wxUint8		compressionType;
	wxUint8		compressionRatio;
	// end Secstor 5
	wxInt8*		bmpMask;
	wxInt32		bmpSize;
	// end Secstor 6
	wxDouble*	data;
	wxDouble    ucurr;
	wxDouble    vcurr;
	// end Sector 7
};
typedef struct message ncdfMessage;

class ncdfReader {

public:
	ncdfReader(MainDialog *dlg);
	~ncdfReader();

	void readncdfFile(const ncdfDataMessage& dataMessage);


	ncdfData *ncdfData1;
	ncdfMessage ncdfMessage1;
	ncdfDataMessage ncdf2DataMessage;
	bool isReading;
	bool gotData = false;


private:
	MainDialog *gui;
	double **gridu;
	double **gridv;
};

#endif // ncdfREADER_H

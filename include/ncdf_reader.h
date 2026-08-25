#ifndef ncdfREADER_H
#define ncdfREADER_H

#include <wx/wx.h>
#include <vector>
#include <cstring>

#ifndef ncdf_NOTDEF
#define ncdf_NOTDEF -999999999
#endif

class MainDialog;
class ncdfData;

class ncdfDataMessage
{
public:
	ncdfDataMessage() :
		hasSeaTemp(false), hasSalinity(false),
		latValues(NULL), lonValues(NULL), timeValues(NULL),
		timeIndex(-1), timeValid(false),
		numberOfPoints(0), minutesAfterStart(0),
		noPointsParallel(0), noPointsMeridian(0),
		firstGridPointLat(0), firstGridPointLong(0),
		lastGridPointLat(0), lastGridPointLong(0),
		iDirectionIncr(0), jDirectionIncr(0),
		latLength(0), lonLength(0), timeLength(0) {}

	ncdfDataMessage(const ncdfDataMessage& other) :
		hasSeaTemp(false), hasSalinity(false),
		latValues(NULL), lonValues(NULL), timeValues(NULL),
		timeIndex(-1) {
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
		clear();
	}

	// Clear data arrays only (keep coordinate metadata for re-read)
	void clearData() {
		ucurr.clear(); vcurr.clear();
		sst.clear(); salinity.clear();
		hasSeaTemp = false;
		hasSalinity = false;
	}

	// Clear everything including coordinate arrays (for destructor/file switch)
	void clear() {
		clearData();
		if (latValues) { free(latValues); latValues = NULL; }
		if (lonValues) { free(lonValues); lonValues = NULL; }
		if (timeValues) { free(timeValues); timeValues = NULL; }
	}

	// Direct grid access — no intermediate 2D array needed
	double getU(int i, int j) const {
		if (ucurr.empty() || j < 0 || j >= (int)latLength || i < 0 || i >= (int)lonLength) return ncdf_NOTDEF;
		return ucurr[j * lonLength + i];
	}
	double getV(int i, int j) const {
		if (vcurr.empty() || j < 0 || j >= (int)latLength || i < 0 || i >= (int)lonLength) return ncdf_NOTDEF;
		return vcurr[j * lonLength + i];
	}
	double getSST(int i, int j) const {
		if (sst.empty() || j < 0 || j >= (int)latLength || i < 0 || i >= (int)lonLength) return ncdf_NOTDEF;
		return sst[j * lonLength + i];
	}
	double getSal(int i, int j) const {
		if (salinity.empty() || j < 0 || j >= (int)latLength || i < 0 || i >= (int)lonLength) return ncdf_NOTDEF;
		return salinity[j * lonLength + i];
	}

	// Check if data type has valid data
	bool hasCurrent() const { return !ucurr.empty() && !vcurr.empty(); }
	bool hasSSTData() const { return !sst.empty() && hasSeaTemp; }
	bool hasSalData() const { return !salinity.empty() && hasSalinity; }

private:
	void copyFrom(const ncdfDataMessage& other) {
		latLength = other.latLength;
		lonLength = other.lonLength;
		timeLength = other.timeLength;

		noPointsParallel = other.noPointsParallel;
		noPointsMeridian = other.noPointsMeridian;
		firstGridPointLat = other.firstGridPointLat;
		firstGridPointLong = other.firstGridPointLong;
		lastGridPointLat = other.lastGridPointLat;
		lastGridPointLong = other.lastGridPointLong;
		iDirectionIncr = other.iDirectionIncr;
		jDirectionIncr = other.jDirectionIncr;

		dataDateTime = other.dataDateTime;
		minutesAfterStart = other.minutesAfterStart;
		numberOfPoints = other.numberOfPoints;
		fileName = other.fileName;
		timeIndex = other.timeIndex;
		timeValid = other.timeValid;

		// Coordinate arrays (deep copy, raw pointer)
		if (other.latValues) {
			latValues = (wxDouble*)calloc(latLength, sizeof(wxDouble));
			if (latValues) memcpy(latValues, other.latValues, latLength * sizeof(wxDouble));
		}
		if (other.lonValues) {
			lonValues = (wxDouble*)calloc(lonLength, sizeof(wxDouble));
			if (lonValues) memcpy(lonValues, other.lonValues, lonLength * sizeof(wxDouble));
		}
		if (other.timeValues) {
			timeValues = (double*)calloc(timeLength, sizeof(double));
			if (timeValues) memcpy(timeValues, other.timeValues, timeLength * sizeof(double));
		}

		// Data arrays — assign directly, avoid shrink_to_fit memory churn
		ucurr = other.ucurr;
		vcurr = other.vcurr;
		sst = other.sst;
		hasSeaTemp = other.hasSeaTemp;
		salinity = other.salinity;
		hasSalinity = other.hasSalinity;
	}

public:
	// Bilinear interpolation from flat array
	double getInterpolatedValue(const ncdfDataMessage& msg, const double* data,
	                            double px, double py, bool numericalInterpolation) const;

	// Joint UV interpolation — one boundary check, one index calc, Hermite smoothing
	bool getInterpolatedUV(const ncdfDataMessage& msg, double px, double py,
	                       double &uOut, double &vOut) const;

	bool isPointInMap(const ncdfDataMessage& g2message, double x, double y) const;
	bool isXInMap(const ncdfDataMessage& g2message, double x) const;
	bool isYInMap(const ncdfDataMessage& g2message, double y) const;

	bool		timeValid;

	// Grid geometry
	wxUint32	noPointsParallel;
	wxUint32	noPointsMeridian;
	wxDouble	firstGridPointLat;
	wxDouble	firstGridPointLong;
	wxDouble	lastGridPointLat;
	wxDouble	lastGridPointLong;
	wxDouble	iDirectionIncr; // longitude step
	wxDouble	jDirectionIncr; // latitude step

	// Data arrays — vector for automatic memory management
	std::vector<double> ucurr;
	std::vector<double> vcurr;
	std::vector<double> sst;
	bool hasSeaTemp;
	std::vector<double> salinity;
	bool hasSalinity;

	wxDateTime  dataDateTime;
	int			minutesAfterStart;
	int			numberOfPoints;
	wxString   fileName;

	wxDouble* latValues;
	wxDouble* lonValues;
	double* timeValues;
	size_t latLength;
	size_t lonLength;
	size_t timeLength;
	int timeIndex;
};


class ncdfReader {

public:
	ncdfReader(MainDialog *dlg);
	~ncdfReader();

	void readncdfFile(const ncdfDataMessage& dataMessage);

	bool isReading;
	bool gotData = false;

private:
	MainDialog *gui;
};

#endif // ncdfREADER_H

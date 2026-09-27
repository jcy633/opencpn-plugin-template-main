#ifndef NCDF_FEATURE_H
#define NCDF_FEATURE_H

#include <vector>
#include <wx/string.h>

class MainDialog;

// Feature kinds (P1: FRONT / CONVERG / DIVERG / SHEAR; P2+ add others)
enum FeatureKind {
    FK_FRONT = 0, FK_EDDY, FK_JET, FK_CONVERG, FK_DIVERG, FK_SHEAR, FK_UPWELL, FK_COUNT
};

// Row-block float grid — per-row allocation avoids huge contiguous blocks
// (32-bit heap fragmentation lesson; same pattern as cachedBaseGrid).
// Non-copyable.
struct FeatRowGrid {
    std::vector<float*> rows;
    int ni, nj;
    FeatRowGrid() : ni(0), nj(0) {}
    ~FeatRowGrid() { release(); }
    void allocate(int ni_, int nj_) {
        if (ni == ni_ && nj == nj_ && rows.size() == (size_t)nj_) return;  // reuse capacity
        release();
        ni = ni_; nj = nj_;
        rows.resize((size_t)nj);
        for (int j = 0; j < nj; j++) rows[(size_t)j] = new float[(size_t)ni]();
    }
    void release() {
        for (size_t j = 0; j < rows.size(); j++) delete[] rows[j];
        rows.clear(); ni = 0; nj = 0;
    }
    float* operator[](int j) { return rows[(size_t)j]; }
    const float* operator[](int j) const { return rows[(size_t)j]; }
private:
    FeatRowGrid(const FeatRowGrid&);
    FeatRowGrid& operator=(const FeatRowGrid&);
};

// Front polyline feature (density front; cls marks degradation)
struct FrontFeature {
    int   vOff, vCnt;      // range in FeatureSet::arena (lon,lat float pairs)
    float bbox[4];         // lonMin, latMin, lonMax, latMax
    float strength;        // 0..1 (NGM percentile of segment mean)
    float dSigma;          // cross-front density difference (kg/m3-ish scaled)
    float dTemp;           // cross-front temperature difference (deg C)
    float gradPerKm;       // density gradient across front (per km)
    float lengthKm;
    int   cls;             // 0 = density (T+S), 1 = temperature only (degraded), 2 = salt only (degraded)
    float shearAcross;     // cross-front |V| difference (m/s)
};

// Strong current axis (P3-prep): speed-ridge jet axis (Kuroshio/Gulf Stream class)
struct JetFeature {
    int   vOff, vCnt;      // axis polyline in arena
    float bbox[4];
    float meanSpeed, maxSpeed;   // m/s
    float width_m;               // half-max full width at mid axis
    float strength;              // 0..1 (mean speed based)
};

// Areal threshold-zone feature (convergence / divergence / shear)
struct ZoneFeature {
    int   vOff, vCnt;      // polygon ring in arena
    float bbox[4];
    int   kind;            // FK_CONVERG / FK_DIVERG / FK_SHEAR
    float strength;        // 0..1
    float meanVal, peakVal;// physical units: 1e-6 /s
    float areaKm2;
};

// Mesoscale eddy (P2): closed-contour candidate + Liutex rotation confirm
struct EddyFeature {
    int   vOff, vCnt;      // contour ring (per-azimuth edge points) in arena
    float bbox[4];
    double clon, clat;     // center
    float radius_m;        // mean ray radius
    float amplitude_m;     // center vs outer ring (m)
    int   polarity;        // +1 anticyclonic/warm (high zos), -1 cyclonic/cold
    float coreTAnom, coreSAnom;
    float meanVort;        // mean vorticity at core (1/s)
    float confidence;      // 0..1 (Liutex/vorticity sign consistency)
    float strength;        // 0..1 (amplitude-based, for line width / declutter)
};

// All detected features for the current time step. Vertices live in one flat
// arena (lon,lat pairs) to avoid per-feature heap churn (C9).
struct FeatureSet {
    std::vector<float>        arena;
    std::vector<FrontFeature> fronts;
    std::vector<EddyFeature>  eddies;
    std::vector<JetFeature>   jets;
    std::vector<ZoneFeature>  zones;
    int detected[FK_COUNT];   // per-kind detected counts (legend)
    FeatureSet() { for (int i = 0; i < FK_COUNT; i++) detected[i] = 0; }
    void clearKeepCapacity() {
        arena.clear(); fronts.clear(); eddies.clear(); jets.clear(); zones.clear();
        for (int i = 0; i < FK_COUNT; i++) detected[i] = 0;
    }
};

// Feature detection engine: 3-layer pipeline (derivatives -> derived scalars ->
// feature objects). Compute runs only on data-update events; rendering and hit
// testing consume the immutable FeatureSet with zero computation (C2/C10).
class FeatureEngine {
public:
    FeatureEngine();
    ~FeatureEngine();

    // Configuration (set from UI / config; takes effect on next computeAll)
    void setRegion(double latMin, double latMax, double lonMin, double lonMax);
    void setSensitivity(int pct);          // 70..95, percentile base
    void setEnabled(int kind, bool on);

    // Input requirements — feed readTimeStepData's need flags
    bool needUV() const;   // convergence / divergence / shear / eddy (Liutex) need U/V
    bool needTS() const;   // fronts need T+S (density); eddy core typing uses T/S
    bool needZos() const;  // eddy closed-contour detection needs zos
    bool anyEnabled() const;

    void resetFile();      // file switch: free all buffers

    // Wave selection for partial recompute (v1.1)
    enum { WAVE_FLOW = 1, WAVE_RHO = 2, WAVE_EDDY = 4, WAVE_JET = 8, WAVE_ALL = 15 };

    // Main entry: compute enabled features from gui->myMessage (waves bitmask:
    // WAVE_FLOW = convergence/divergence/shear, WAVE_RHO = density fronts).
    // Requires myMessage to hold the needed fields (see needUV/needTS).
    void computeAll(MainDialog *gui, int waves = WAVE_ALL);

    const FeatureSet& features() const { return m_set; }
    bool spotPending() const { return !m_spotDone; }   // probe wants all fields loaded

    // Hover hit test: returns true and fills 'out' with a summary line.
    bool hitTest(double lon, double lat, wxString &out) const;
    // Body hit test: returns kind (0 eddy, 1 jet, 2 front, 3 zone, -1 none) + index.
    int hitTestAt(double lon, double lat, int &idx) const;

private:
    // ---- L1/L2: derivative + derived scalar passes (transient scratch) ----
    void computeFlowDeriv(MainDialog *gui, int i0, int i1, int j0, int j1); // div/strain/vort
    void computeRhoNGM(MainDialog *gui, int i0, int i1, int j0, int j1);    // NGM of density
    // ---- L3: object extraction ----
    void extractZones(MainDialog *gui, int i0, int i1, int j0, int j1);
    void extractFronts(MainDialog *gui, int i0, int i1, int j0, int j1);
    void extractEddies(MainDialog *gui, int i0, int i1, int j0, int j1);
    void extractJets(MainDialog *gui, int i0, int i1, int j0, int j1);
    void rebuildArena(bool keepFronts, bool keepEddies, bool keepJets, bool keepZones);

    // helpers
    float percentileOf(const FeatRowGrid &g, int i0, int i1, int j0, int j1, float pct) const; // pct 0..100, ignores NaN
    static void douglasPeucker(std::vector<double> &xy, double tolLonLat);
    bool splitSignGate(int kind) const;   // is this zone kind enabled by user?
    void spotReport(MainDialog *gui);     // real-data verification probe (once per file)
    double m_latMin, m_latMax, m_lonMin, m_lonMax;
    int    m_sensPct;      // 70..95
    bool   m_enabled[FK_COUNT];

    FeatureSet m_set;
    bool m_spotDone = false;   // real-data spot report runs once per file

    // Transient derived fields (row-block; capacity reused across time steps)
    FeatRowGrid m_div, m_strain, m_vort;
    FeatRowGrid m_ngm, m_rho;    // normalized gradient magnitude + density proxy
    FeatRowGrid m_bimodal;   // window verdict map (1 = bimodal window)
    FeatRowGrid m_mask;      // scratch binary mask (values 0/1)
    FeatRowGrid m_visited;   // scratch visit stamps
    int m_visitStamp;

    // Jet axes cache (ALWAYS computed when eddies on -- meander filtering --
    // regardless of the display checkbox; copied into m_set only when shown)
    std::vector<JetFeature> m_jetCache;
    std::vector<float>      m_jetArena;

    // scratch vectors reused every extraction (capacity kept)
    std::vector<int>   m_stack;
    std::vector<int>   m_compPixels;
    std::vector<float> m_tmpXY;
};

#endif // NCDF_FEATURE_H

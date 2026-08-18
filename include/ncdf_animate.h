#ifndef NCDF_ANIMATE_H
#define NCDF_ANIMATE_H

#include <vector>

class ncdf_pi;
class MainDialog;

class ncdfAnimate {
public:
    ncdfAnimate();
    ~ncdfAnimate();

    bool Init(ncdf_pi* plugin, MainDialog* gui);
    void Cleanup();
    bool IsInitialized() const { return m_initialized; }
    bool HasDisplacement() const { return m_hasDisp; }

    void SetPeriod(int f) { m_period = f; }
    int GetPeriod() const { return m_period; }

    bool ComputeDisplacementMap();

    // Get advected grids for current frame (u and v separately)
    // Returns speed grid (computed from advected u/v) for RenderGridOverlay
    double** GetAdvectedGrid();

    // Get the advected u and v grids (for vector mode texture upload)
    double** GetAdvectedUGrid() const { return m_advectedU; }
    double** GetAdvectedVGrid() const { return m_advectedV; }

    void AdvanceFrame();

private:
    bool m_initialized;
    ncdf_pi* m_plugin;
    MainDialog* m_gui;

    int m_period;
    float m_power;
    float m_maxDisp;
    int m_frame;

    // Displacement map
    float** m_dispX;
    float** m_dispY;
    bool m_hasDisp;
    int m_dispW, m_dispH;

    // Dual coordinate fields (patent: C1, C2)
    float** m_c1x; float** m_c1y;
    float** m_c2x; float** m_c2y;
    bool m_hasCoordFields;
    int m_coordW, m_coordH;

    // Source fields (u and v separately)
    float** m_srcU;       // raw u values
    float** m_srcV;       // raw v values
    int m_srcW, m_srcH;
    float m_speedMin, m_speedMax;  // speed range for normalization

    // B-spline coefficient fields (pre-filtered from srcU/srcV)
    float** m_coeffU;
    float** m_coeffV;
    bool m_hasCoeffs;

    // Advected output grids
    double** m_advectedU;
    double** m_advectedV;
    double** m_advectedSpeed;  // sqrt(u²+v²), for scalar mode
    int m_advW, m_advH;

    void FreeFloatGrid(float**& grid, int h);
    bool AllocateFloatGrid(float**& grid, int h, int w);
    void FreeGrid(double**& grid, int h);
    bool AllocateGrid(double**& grid, int h, int w);

    void BicubicSplinePrefilter(float** dst, float** src, int h, int w);
    float SampleCoordField(float** field, float x, float y, int w, int h);
    float SampleSplineCoeff(float** coeff, float x, float y);

    float ClampX(float x) const { return x < 0 ? 0 : (x > m_srcW - 1 ? (float)(m_srcW - 1) : x); }
    float ClampY(float y) const { return y < 0 ? 0 : (y > m_srcH - 1 ? (float)(m_srcH - 1) : y); }

    bool BuildSourceFields();
    void InitCoordFields();
    void UpdateCoordFields();
    void SampleAndBlend();
    void ResetToIdentity(float** cx, float** cy);
};

#endif // NCDF_ANIMATE_H

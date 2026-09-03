#ifndef NCDF_ANIMATE_H
#define NCDF_ANIMATE_H

#include <vector>

class ncdf_pi;
class MainDialog;
struct PlugIn_ViewPort;

// GL types (avoid including GL headers to prevent conflicts with wxWidgets)
typedef unsigned int GLuint;

class ncdfAnimate {
public:
    ncdfAnimate();
    ~ncdfAnimate();

    bool Init(ncdf_pi* plugin, MainDialog* gui);
    void Cleanup();
    bool IsInitialized() const { return m_initialized; }
    bool HasDisplacement() const { return m_hasDisp; }
    bool HasCoeffs() const { return m_hasCoeffs; }
    bool HasCoordFields() const { return m_hasCoordFields; }

    void SetPeriod(int f) { m_period = f; }
    int GetPeriod() const { return m_period; }
    int GetFrame() const { return m_frame; }

    bool ComputeDisplacementMap();
    // Overload: use float grids directly (for new data pipeline where myMessage.ucurr is cleared)
    bool ComputeDisplacementMap(float* const* uGrid, float* const* vGrid, int ni, int nj);

    // GPU animation: update coord fields + advect + display
    // Called from DoRenderncdfOverlay when animation is active
    // scalarTexID: GL texture ID of the B-spline coefficient texture
    // scalarW, scalarH: coefficient texture dimensions
    // lutTexID: color lookup table texture
    // coefMin, coefMax: coefficient range for denormalization
    bool UploadCoordFields();
    void RenderAnimationFrame(PlugIn_ViewPort *vp, GLuint scalarTexID, int scalarW, int scalarH,
                              GLuint lutTexID, float coefMin, float coefMax,
                              float lutMin, float lutRange);

    // Legacy CPU path (kept for compatibility)
    double** GetAdvectedGrid();
    double** GetAdvectedUGrid() const { return m_advectedU; }
    double** GetAdvectedVGrid() const { return m_advectedV; }

    // Scalar advection: sample source scalar field at advected coordinates
    // Computes B-spline coefficients from srcGrid once, then samples each frame
    double** GetAdvectedScalarGrid(float* const* srcGrid, int ni, int nj);

    // Set external U/V grids for source fields (when myMessage has no current data)
    void SetExternalSourceGrids(float* const* uGrid, float* const* vGrid, int ni, int nj) {
        m_extUGrid = uGrid; m_extVGrid = vGrid; m_extNi = ni; m_extNj = nj;
    }

    // Get the GPU-rendered animation result texture (from RenderAnimationFrame)
    GLuint GetAnimResultTexture() const { return m_texResult; }
    GLuint GetFBO() const { return m_fbo; }
    int GetAnimResultW() const { return m_dispW; }
    int GetAnimResultH() const { return m_dispH; }

    // Debug: sample displacement and coordinate fields at a position
    void GetCoordSample(int i, int j, float& dispX, float& dispY,
                        float& c1x, float& c1y, float& c2x, float& c2y) const {
        dispX = (m_dispX && j >= 0 && j < m_dispH && i >= 0 && i < m_dispW) ? m_dispX[j][i] : 0;
        dispY = (m_dispY && j >= 0 && j < m_dispH && i >= 0 && i < m_dispW) ? m_dispY[j][i] : 0;
        c1x = (m_c1x && j >= 0 && j < m_coordH && i >= 0 && i < m_coordW) ? m_c1x[j][i] : 0;
        c1y = (m_c1y && j >= 0 && j < m_coordH && i >= 0 && i < m_coordW) ? m_c1y[j][i] : 0;
        c2x = (m_c2x && j >= 0 && j < m_coordH && i >= 0 && i < m_coordW) ? m_c2x[j][i] : 0;
        c2y = (m_c2y && j >= 0 && j < m_coordH && i >= 0 && i < m_coordW) ? m_c2y[j][i] : 0;
    }

    void AdvanceFrame();

    // GPU path: initialize coordinate fields without BuildSourceFields dependency
    void InitCoordFields(int ni, int nj);

private:
    bool m_initialized;
    ncdf_pi* m_plugin;
    MainDialog* m_gui;

    int m_period;
    float m_power;
    float m_maxDisp;
    int m_frame;

    // CPU: Displacement map (RK4, computed once per steady flow)
    float** m_dispX;
    float** m_dispY;
    bool m_hasDisp;
    int m_dispW, m_dispH;

    // CPU: Dual coordinate fields
    float** m_c1x; float** m_c1y;
    float** m_c2x; float** m_c2y;
    bool m_hasCoordFields;
    int m_coordW, m_coordH;

    // CPU: Source fields
    float** m_srcU;
    float** m_srcV;
    int m_srcW, m_srcH;
    float m_speedMin, m_speedMax;

    // CPU: Scalar advection (B-spline coefficients + output)
    float** m_scalarCoeff;
    float** m_scalarSrc;
    int m_scalarW, m_scalarH;
    double** m_advectedScalar;
    int m_advScalarW, m_advScalarH;

    // External source grids (for overlays without myMessage current data)
    float* const* m_extUGrid;
    float* const* m_extVGrid;
    int m_extNi, m_extNj;

    // DEBUG: RK4 tracer points
    struct TracerSeed { float nx, ny; };  // normalized [0,1] coordinates
    static const int MAX_TRACERS = 5;
    TracerSeed m_tracerSeeds[MAX_TRACERS];
    int m_tracerSeedCount;
    bool m_tracerInitialized;
    float m_tracerCurX[MAX_TRACERS], m_tracerCurY[MAX_TRACERS];  // current grid positions

    // CPU: B-spline coefficient fields
    float** m_coeffU;
    float** m_coeffV;
    bool m_hasCoeffs;

    // CPU: Advected output (legacy path)
    double** m_advectedU;
    double** m_advectedV;
    double** m_advectedSpeed;
    int m_advW, m_advH;

    // GPU: Animation textures and FBOs
    bool m_gpuReady;
    bool m_gpuDeletePending;   // deferred GL cleanup flag
    GLuint m_texDisp;
    GLuint m_texCoordA1;
    GLuint m_texCoordA2;
    GLuint m_texCoordB1;
    GLuint m_texCoordB2;
    GLuint m_texResult;
    GLuint m_fbo;

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
    bool BuildSourceFields(float* const* extUGrid, float* const* extVGrid, int extNi, int extNj);
    void InitCoordFields();
    // Coordinate field update (GPU only — CPU path removed)
    bool UpdateCoordFieldsGPU();
    void SampleAndBlend();
    void ResetToIdentity(float** cx, float** cy);

    // GPU helpers
    bool InitGPU(int w, int h);
    void CleanupGPU();
    void FlushPendingGLDeletes();  // deferred GL cleanup in render thread
    bool UploadDisplacementMap();

    unsigned char* m_readbackBuf;  // reusable readback buffer
    int m_readbackBufSize;
};

#endif // NCDF_ANIMATE_H

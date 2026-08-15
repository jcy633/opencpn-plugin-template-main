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

    // Compute displacement map (CPU, one-time)
    bool ComputeDisplacementMap();

    // Get advected scalar grid for current frame
    // Returns the grid pointer (owned by ncdfAnimate, don't free)
    double** GetAdvectedGrid();
    void AdvanceFrame();

private:
    bool m_initialized;
    ncdf_pi* m_plugin;
    MainDialog* m_gui;

    int m_period;
    float m_power;
    float m_maxDisp;
    int m_frame;

    // Displacement map (dx, dy per grid cell in grid units)
    double** m_dispX;
    double** m_dispY;
    bool m_hasDisp;
    int m_dispW, m_dispH;

    // Advected output grid
    double** m_advectedGrid;
    int m_advW, m_advH;

    // Source scalar grid reference (not owned)
    double** m_sourceGrid;

    void FreeGrid(double**& grid, int h);
    bool AllocateGrid(double**& grid, int h, int w);
    void ComputeAdvectedGrid();
};

#endif

/*
    Copyright (c) 2011, Konni <email>
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
        * Neither the name of the <organization> nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY Konni <email> ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL Konni <email> BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#define PI 3.14159265

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "IsoLine2.h"
#include "shader/ncdf_shader.h"
#include <chrono>
#include <thread>
#include <wx/colour.h>
#include <wx/dynarray.h>

extern void ncdfLog(const char* format, ...);

#ifdef __WXMSW__
#define snprintf _snprintf
#endif // __WXMSW__

#define NUM_CURRENT_ARROW_POINTS 9
static wxPoint CurrentArrowArray[NUM_CURRENT_ARROW_POINTS] = { wxPoint(0, 0), wxPoint(0, -10),
wxPoint(55, -10), wxPoint(55, -25), wxPoint(100, 0), wxPoint(55, 25), wxPoint(55,
10), wxPoint(0, 10), wxPoint(0, 0)};

//----------------------------------------------------------------------------------------------------------
//    ncdf Overlay Factory Implementation
//----------------------------------------------------------------------------------------------------------
ncdfOverlayFactory::ncdfOverlayFactory()
{
      hash = NULL;
      
      m_pbm_current 	= NULL;     
      
      m_last_vp_latMax = -99999.0;
      rect = NULL;
      
      space[0]=30; space[1]=50; space[2]=100; space[3]=200; space[4]=400; space[5]=600; space[6]=1200;
	  m_bReadyToRender = false;
      m_space = 0;
      m_ParticleMap = nullptr;
      m_currentInterpMode = 0;
      m_currentDataMin = 0.0f;
      m_currentDataMax = 1.0f;
      m_glDispTexture = 0;
      m_bHasDispTexture = false;

      // Animation timer: triggers periodic redraws when animation is active
      m_animateTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
          GetOCPNCanvasWindow()->Refresh(false);
      });

      // Animation textures (independent from static)
      m_glAnimColorTexture = 0;
      m_bHasAnimColorTexture = false;
      m_animTexDataDim[0] = m_animTexDataDim[1] = 0;
      m_animTexGLDim[0] = m_animTexGLDim[1] = 0;
      m_glAnimSeaTempTexture = 0;
      m_bHasAnimSeaTempTexture = false;
      m_animSstTexDataDim[0] = m_animSstTexDataDim[1] = 0;
      m_animSstTexGLDim[0] = m_animSstTexGLDim[1] = 0;
      m_glAnimSalinityTexture = 0;
      m_bHasAnimSalinityTexture = false;
      m_animSalTexDataDim[0] = m_animSalTexDataDim[1] = 0;
      m_animSalTexGLDim[0] = m_animSalTexGLDim[1] = 0;
      m_currentOverlay.Init();
      m_seaTempOverlay.Init();
      m_salinityOverlay.Init();

      // Pre-compute arrow shape (GRIB LineBuffer pattern)
      // Shaft: from -10 to +10
      m_ArrowBuffer.pushLine(-10, 0, 10, 0);
      // Head: V shape
      m_ArrowBuffer.pushLine(-10, 0, -3, 5);
      m_ArrowBuffer.pushLine(-10, 0, -3, -5);
      m_ArrowBuffer.Finalize();

      // Timer for particle animation (GRIB-style ONE_SHOT)
      m_tParticleTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
          m_bUpdateParticles = true;
          GetOCPNCanvasWindow()->Refresh(false);
      });
      m_bUpdateParticles = true;
      m_particleBurstCount = 0;
}

ncdfOverlayFactory::~ncdfOverlayFactory()
{
    m_bReadyToRender = false;
	renderSelectionRectangle = false;
    if (m_tParticleTimer.IsRunning()) m_tParticleTimer.Stop();
    // Only clear B-spline coefficients (data-dependent). Preserve grids for reuse.
    m_currentOverlay.clearCoefficients();
    m_seaTempOverlay.clearCoefficients();
    m_salinityOverlay.clearCoefficients();
    ClearParticles();
    DeleteDispTexture();
    if (m_animateTimer.IsRunning()) m_animateTimer.Stop();
    // Clean up animation textures
    if (m_bHasAnimColorTexture && m_glAnimColorTexture) { glDeleteTextures(1, &m_glAnimColorTexture); }
    if (m_bHasAnimSeaTempTexture && m_glAnimSeaTempTexture) { glDeleteTextures(1, &m_glAnimSeaTempTexture); }
    if (m_bHasAnimSalinityTexture && m_glAnimSalinityTexture) { glDeleteTextures(1, &m_glAnimSalinityTexture); }
    ncdf_shader_cleanup();
    DeleteDispTexture();
}

void ncdfOverlayFactory::DeleteDispTexture()
{
    if (m_bHasDispTexture && m_glDispTexture) {
        glDeleteTextures(1, &m_glDispTexture);
        m_glDispTexture = 0;
        m_bHasDispTexture = false;
    }
}

void ncdfOverlayFactory::SetBicubicMode(bool enable)
{
    // Only invalidate the currently visible overlay (avoid expensive rebuilds)
    if (plugin && plugin->m_bShowCurrentForce)
        m_currentOverlay.Invalidate();
    else if (plugin && plugin->m_bShowSeaTemp)
        m_seaTempOverlay.Invalidate();
    else if (plugin && plugin->m_bShowSalinity)
        m_salinityOverlay.Invalidate();
}

void ncdfOverlayFactory::setData(MainDialog *gui, ncdf_pi *plugin, const ncdfDataMessage& g2data, int numberOfPoints, wxDouble tlat, wxDouble tlon, wxDouble blat, wxDouble blon)
{
	ncdfLog("[setData] ENTER: hasCurr=%d hasSST=%d hasSal=%d | showCurr=%d showSST=%d showSal=%d\n",
		(int)g2data.hasCurrent(), (int)g2data.hasSSTData(), (int)g2data.hasSalData(),
		(int)(plugin && plugin->m_bShowCurrentForce), (int)(plugin && plugin->m_bShowSeaTemp), (int)(plugin && plugin->m_bShowSalinity));

	// Clear B-spline coefficients (data-dependent). Preserve grids for reuse.
	m_currentOverlay.clearCoefficients();
	m_seaTempOverlay.clearCoefficients();
	m_salinityOverlay.clearCoefficients();
	ClearParticles();
	m_last_vp_scale = -1;
	m_last_vp_latMax = -99999.0;
	m_bUpdateParticles = true;
	m_particleBurstCount = 30;

	this->numberOfPoints = numberOfPoints;
	this->gui = gui;
	this->plugin = plugin;

	this->tlat = wxMax(tlat, blat);
	this->blat = wxMin(tlat, blat);
	this->tlon = wxMin(tlon, blon);
	this->blon = wxMax(tlon, blon);

	ncdfLog("[setData] bounds: lat[%.2f,%.2f] lon[%.2f,%.2f] ni=%d nj=%d\n",
		this->tlat, this->blat, this->tlon, this->blon,
		(int)gui->myMessage.lonLength, (int)gui->myMessage.latLength);
	// Don't set m_bReadyToRender here — callers manage it explicitly
}

void ncdfOverlayFactory::setSelectionRectangle(Selection *rect)
{
    this->rect = rect;
}

void ncdfOverlayFactory::prepareAllOverlays()
{
    if (!gui) return;
    ncdfLog("[render] prepareAllOverlays: ENTER, hasCurrent=%d hasSST=%d hasSal=%d\n",
        (int)gui->myMessage.hasCurrent(), (int)gui->myMessage.hasSSTData(), (int)gui->myMessage.hasSalData());

    auto t0 = std::chrono::high_resolution_clock::now();

    // setData() already Cleanup()'d all caches — no need to clear here
    // Only prepare overlays that are currently visible (GRIB pattern)
    bool hasCurr = gui->myMessage.hasCurrent() && plugin && plugin->m_bShowCurrentForce;
    bool hasSST  = gui->myMessage.hasSSTData()  && plugin && plugin->m_bShowSeaTemp;
    bool hasSal  = gui->myMessage.hasSalData()  && plugin && plugin->m_bShowSalinity;
    ncdfLog("[prepare] hasCurr=%d hasSST=%d hasSal=%d (visible only)\n",
        (int)hasCurr, (int)hasSST, (int)hasSal);

    if (hasCurr) { ncdfLog("[prepare] current START\n"); m_currentOverlay.prepareData(gui, plugin, this); ncdfLog("[prepare] current DONE needsRebuild=%d\n", (int)m_currentOverlay.needsRebuild); }
    if (hasSST)  { ncdfLog("[prepare] sst START\n"); m_seaTempOverlay.prepareData(gui, plugin, this); ncdfLog("[prepare] sst DONE needsRebuild=%d\n", (int)m_seaTempOverlay.needsRebuild); }
    if (hasSal)  { ncdfLog("[prepare] sal START\n"); m_salinityOverlay.prepareData(gui, plugin, this); ncdfLog("[prepare] sal DONE needsRebuild=%d\n", (int)m_salinityOverlay.needsRebuild); }

    // Free CPU-side raw data — all computations are now cached in overlay structs
    // Texture upload will happen lazily on first RenderColorMap (needs GL context)
    // NOTE: Do NOT clear myMessage vectors here — DoRenderncdfOverlay checks
    // myMessage.hasSSTData()/hasCurrent()/hasSalData() as render gate.
    // Vectors will be cleared when the next time step loads via myMessage.clearData().

    auto t1 = std::chrono::high_resolution_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    ncdfLog("[perf] prepareAllOverlays: TOTAL=%lldms (sequential)\n", (long long)totalMs);
    ncdfLog("[render] prepareAllOverlays: DONE, CPU data freed\n");
}

void ncdfOverlayFactory::prepareAllGrids()
{
    if (!gui) return;
    ncdfLog("[render] prepareAllGrids: ENTER\n");
    auto t0 = std::chrono::high_resolution_clock::now();

    // P1: Only prepare grids for CHECKED (visible) types.
    // Unchecked types will be prepared on-demand when the user clicks the checkbox.
    if (plugin && plugin->m_bShowCurrentForce && gui->myMessage.hasCurrent()) {
        m_currentOverlay.prepareGrid(gui);
        m_currentOverlay.dataReady = true;
    }
    if (plugin && plugin->m_bShowSeaTemp && gui->myMessage.hasSSTData()) {
        m_seaTempOverlay.prepareGrid(gui);
        m_seaTempOverlay.dataReady = true;
    }
    if (plugin && plugin->m_bShowSalinity && gui->myMessage.hasSalData()) {
        m_salinityOverlay.prepareGrid(gui);
        m_salinityOverlay.dataReady = true;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] prepareAllGrids: TOTAL=%lldms\n",
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

void ncdfOverlayFactory::updateTimeStep()
{
    ncdfLog("[updateTimeStep] ENTER — same-file time step switch\n");
    // Only clear B-spline coefficients (data-dependent). Preserve grids for reuse.
    m_currentOverlay.clearCoefficients();
    m_seaTempOverlay.clearCoefficients();
    m_salinityOverlay.clearCoefficients();
    m_last_vp_scale = -1;
    m_last_vp_latMax = -99999.0;
    m_bUpdateParticles = true;
    m_particleBurstCount = 30;
    ClearParticles();
}

void ncdfOverlayFactory::reset()
{
    this->m_bReadyToRender = false;
    clearBmp();
    // Only invalidate overlays that are currently visible
    // Hidden overlays keep their textures cached (no expensive rebuild)
    if (plugin && plugin->m_bShowCurrentForce)
        m_currentOverlay.Invalidate();
    if (plugin && plugin->m_bShowSeaTemp)
        m_seaTempOverlay.Invalidate();
    if (plugin && plugin->m_bShowSalinity)
        m_salinityOverlay.Invalidate();
}


bool ncdfOverlayFactory::RenderGLncdfOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
	m_pdc = NULL;  // inform lower layers that this is OpenGL render
	return DoRenderncdfOverlay(vp);
}


bool ncdfOverlayFactory::RenderncdfOverlay(wxDC &dc, PlugIn_ViewPort *vp)
{
#if wxUSE_GRAPHICS_CONTEXT
	wxMemoryDC *pmdc;
	pmdc = wxDynamicCast(&dc, wxMemoryDC);
	wxGraphicsContext *pgc = wxGraphicsContext::Create(*pmdc);
	m_gdc = pgc;
#endif
	m_pdc = &dc;
	return DoRenderncdfOverlay(vp);
}


bool ncdfOverlayFactory::DoRenderncdfOverlay(PlugIn_ViewPort *vp )
{
    this->vp = vp;
    if (!vp) return false;

    // Guard: must have valid gui and at least one overlay with prepared data
    if (!gui) return false;
    bool hasCurrentGrid = m_currentOverlay.dataReady;
    bool hasSSTGrid = m_seaTempOverlay.dataReady;
    bool hasSalinityGrid = m_salinityOverlay.dataReady;
    ncdfLog("[render] DoRenderncdfOverlay: ENTER, hasCurr=%d hasSST=%d hasSal=%d ready=%d\n",
        (int)hasCurrentGrid, (int)hasSSTGrid, (int)hasSalinityGrid, (int)m_bReadyToRender);
    if (!hasCurrentGrid && !hasSSTGrid && !hasSalinityGrid) {
        return false;
    }
    if (gui->myMessage.lonLength < 2 || gui->myMessage.latLength < 2) return false;
    if (!m_bReadyToRender) return false;

    // Must have at least one rendering feature enabled
    if (!plugin->m_bShowCurrentForce && !plugin->m_bShowCurrentDir &&
        !plugin->m_bShowParticles && !plugin->m_bShowSeaTemp &&
        !plugin->m_bShowSeaTempIso && !plugin->m_bShowSalinity) return false;

    // Lazy shader compile (first render call, GL context is available)
    // Use ncdf_shader_initialized() to handle factory recreation
    if (!ncdf_shader_initialized()) {
        ncdf_shader_init();
    }

    static bool s_shaderDbg = false;
    if (!s_shaderDbg) {
        s_shaderDbg = true;
        wxLogMessage(_T("[shader] initialized=%d"),
                     (int)ncdf_shader_initialized());
    }

    // Animation: get advected grids if any type's animation is active
    double** animGrid = NULL;
    double** animUGrid = NULL;
    double** animVGrid = NULL;
    bool animateActive = (plugin->m_settingsCurrent.animate && plugin->m_bShowCurrentForce) ||
                         (plugin->m_settingsSeaTemp.animate && plugin->m_bShowSeaTemp) ||
                         (plugin->m_settingsSalinity.animate && plugin->m_bShowSalinity);
    if (animateActive && m_animate.IsInitialized()) {
        if (!m_animate.HasDisplacement()) {
            m_animate.ComputeDisplacementMap();
        }
        animGrid = m_animate.GetAdvectedGrid();
        animUGrid = m_animate.GetAdvectedUGrid();
        animVGrid = m_animate.GetAdvectedVGrid();
    }

    static int s_frameDbg = 0;
    if (s_frameDbg < 5) {
        wxLogMessage(_T("[render] frame %d ucurr.size=%zu vcurr.size=%zu sst.size=%zu sal.size=%zu"), s_frameDbg, gui->myMessage.ucurr.size(), gui->myMessage.vcurr.size(), gui->myMessage.sst.size(), gui->myMessage.salinity.size());
        s_frameDbg++;
    }

    if(vp->view_scale_ppm != m_last_vp_scale)
    {
      if(vp->view_scale_ppm < 0.001135)
	  m_space = space[0];
      else if(vp->view_scale_ppm <= 0.001135)
	  m_space = space[1];
      else if(vp->view_scale_ppm <= 0.018165)
	  m_space = space[2];
      else if(vp->view_scale_ppm <= 0.072659)
	  m_space = space[3];

    }
	// No need to clear on viewport change - texture is cached
	m_last_vp_latMax = vp->lat_max;

    static int s_renderDbg = 0;
    if (s_renderDbg < 10) {
		wxLogMessage(_T("[render] ready=%d gui=%p ucurr.size=%zu ni=%d showF=%d showD=%d showP=%d"),
			(int)m_bReadyToRender, gui,
			gui ? gui->myMessage.ucurr.size() : (size_t)0,
			gui ? (int)gui->myMessage.lonLength : 0,
			plugin ? (int)plugin->m_bShowCurrentForce : -1,
			plugin ? (int)plugin->m_bShowCurrentDir : -1,
			plugin ? (int)plugin->m_bShowParticles : -1);
		s_renderDbg++;
	}

    ncdfLog("[render] dispatch: showCurr=%d showSST=%d showSal=%d showDir=%d showP=%d | cur[dataR=%d hasTex=%d] sst[dataR=%d hasTex=%d] sal[dataR=%d hasTex=%d]\n",
        (int)plugin->m_bShowCurrentForce, (int)plugin->m_bShowSeaTemp, (int)plugin->m_bShowSalinity,
        (int)plugin->m_bShowCurrentDir, (int)plugin->m_bShowParticles,
        (int)m_currentOverlay.dataReady, (int)m_currentOverlay.hasTexture,
        (int)m_seaTempOverlay.dataReady, (int)m_seaTempOverlay.hasTexture,
        (int)m_salinityOverlay.dataReady, (int)m_salinityOverlay.hasTexture);
    RenderCurrentOverlay(vp, animGrid, animUGrid, animVGrid);

    if(plugin->m_bShowCurrentDir) {
        RenderncdfCurrent();
    }

    if(plugin->m_bShowParticles) {
        RenderParticles(vp);
    } else {
        ClearParticles();
    }

    RenderSeaTempOverlay(vp, animGrid);
    RenderSalinityOverlay(vp, animGrid);

    // Color legend
    {
        static const ColorStop tempStops[] = {
            {-2, 0x80, 0x00, 0xc0}, {2, 0x40, 0x30, 0xff}, {7, 0x00, 0x90, 0xfa},
            {12, 0x00, 0xd8, 0xb0}, {17, 0x10, 0xbb, 0x20}, {22, 0x90, 0xd0, 0x00},
            {26, 0xf0, 0xd0, 0x00}, {30, 0xf0, 0x70, 0x00}, {32, 0xff, 0x00, 0x00}
        };
        static const ColorStop salStops[] = {
            {30, 0x87, 0xce, 0xeb}, {31, 0x60, 0xb0, 0xe0}, {32, 0x40, 0x90, 0xd0},
            {33, 0x20, 0x70, 0xc0}, {34, 0x10, 0x50, 0xa0}, {35, 0x00, 0x80, 0x80},
            {35.5, 0x20, 0xa0, 0x70}, {36, 0x40, 0xc0, 0x60}, {36.5, 0x80, 0xc0, 0x40},
            {37, 0xc0, 0xb0, 0x20}, {37.5, 0xe0, 0xa0, 0x10}, {38, 0xe0, 0x70, 0x20},
            {38.5, 0xd0, 0x40, 0x20}, {39, 0xc0, 0x20, 0x20}
        };
        static const ColorStop currStops[] = {
            {0.00, 20, 20, 180}, {0.10, 30, 80, 220}, {0.25, 0, 180, 220},
            {0.50, 0, 200, 80}, {0.75, 220, 220, 20}, {1.00, 240, 100, 20},
            {1.50, 220, 20, 20}
        };
        if (plugin->m_bShowSeaTemp && gui && m_seaTempOverlay.dataReady) {
            m_legend.SetData(tempStops, 9, "\xC2\xB0" "C", "Sea Temperature");
            m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
        } else if (plugin->m_bShowSalinity && gui && m_salinityOverlay.dataReady) {
            m_legend.SetData(salStops, 14, "PSU", "Salinity");
            m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
        } else if (plugin->m_bShowCurrentForce && gui && m_currentOverlay.dataReady) {
            m_legend.SetData(currStops, 7, "m/s", "Current Speed");
            m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
        }
    }

    m_last_vp_scale = vp->view_scale_ppm;
    m_last_vp_latMax = vp->lat_max;

    // Reset GL state to avoid corrupting other plugins (GRIB, OpenCPN core)
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);
    glBindTexture(GL_TEXTURE_2D, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    return true;
}

void ncdfOverlayFactory::RenderCurrentOverlay(PlugIn_ViewPort *vp, double **animGrid, double **animUGrid, double **animVGrid)
{
    if(plugin->m_bShowCurrentForce && m_currentOverlay.dataReady && !m_pdc) {
#ifdef ocpnUSE_GL
        ncdfLog("[render:current] ENTER hasTex=%d needsRebuild=%d dataReady=%d cachedU=%p cachedCoeffU=%p\n",
            (int)m_currentOverlay.hasTexture, (int)m_currentOverlay.needsRebuild,
            (int)m_currentOverlay.dataReady, (void*)m_currentOverlay.cachedU, (void*)m_currentOverlay.cachedCoeffU);
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_currentInterpMode = 0;
        m_currentOverlay.RenderColorMap(vp, gui, plugin, this, animGrid, animUGrid, animVGrid);
        m_currentDataMin = m_currentOverlay.cachedDataMin;
        m_currentDataMax = m_currentOverlay.cachedDataMax;
        ncdfLog("[render] RenderCurrentOverlay: EXIT\n");
#endif
    }
}

void ncdfOverlayFactory::RenderSeaTempOverlay(PlugIn_ViewPort *vp, double **animGrid)
{
    if (plugin->m_bShowSeaTemp && gui && m_seaTempOverlay.dataReady && !m_pdc) {
#ifdef ocpnUSE_GL
        ncdfLog("[render:sst] ENTER hasTex=%d needsRebuild=%d dataReady=%d cachedCoeff=%p cachedBaseGrid=%d\n",
            (int)m_seaTempOverlay.hasTexture, (int)m_seaTempOverlay.needsRebuild,
            (int)m_seaTempOverlay.dataReady, (void*)m_seaTempOverlay.cachedCoeff, (int)(bool)m_seaTempOverlay.cachedBaseGrid);
        m_currentInterpMode = 0;
        m_seaTempOverlay.RenderColorMap(vp, gui, plugin, this, animGrid);
        m_currentDataMin = m_seaTempOverlay.cachedDataMin;
        m_currentDataMax = m_seaTempOverlay.cachedDataMax;
        ncdfLog("[render] RenderSeaTempOverlay: EXIT\n");
#endif
    }

    if (plugin->m_bShowSeaTempIso && gui && m_seaTempOverlay.dataReady) {
        m_seaTempOverlay.RenderIsoLines(vp, gui);
    }
}

void ncdfOverlayFactory::RenderSalinityOverlay(PlugIn_ViewPort *vp, double **animGrid)
{
    if (plugin->m_bShowSalinity && gui && m_salinityOverlay.dataReady && !m_pdc) {
#ifdef ocpnUSE_GL
        ncdfLog("[render:sal] ENTER hasTex=%d needsRebuild=%d dataReady=%d cachedCoeff=%p cachedBaseGrid=%d\n",
            (int)m_salinityOverlay.hasTexture, (int)m_salinityOverlay.needsRebuild,
            (int)m_salinityOverlay.dataReady, (void*)m_salinityOverlay.cachedCoeff, (int)(bool)m_salinityOverlay.cachedBaseGrid);
        m_currentInterpMode = 0;
        m_salinityOverlay.RenderColorMap(vp, gui, plugin, this, animGrid);
        m_currentDataMin = m_salinityOverlay.cachedDataMin;
        m_currentDataMax = m_salinityOverlay.cachedDataMax;
#endif
    }

    if (plugin->m_settingsSalinity.showIsoLines && plugin->m_bShowSalinity && gui && m_salinityOverlay.dataReady) {
        m_salinityOverlay.RenderIsoLines(vp, gui);
    }
}

void ncdfOverlayFactory::clearBmp()
{
    // Don't delete color texture - it persists across view changes
    // Only delete bitmap cache (for DC rendering)
    if(m_pbm_current) delete m_pbm_current;
    m_pbm_current = NULL;
}

void ncdfOverlayFactory::RenderSelectionRectangle()
{
    wxPoint ptop, pbottom;
    wxInt32 width,height;
    
    GetCanvasPixLL(vp, &ptop, rect->topLat, rect->topLon);
    if(rect->bottomLat != ncdf_NOTDEF)
    {
      GetCanvasPixLL(vp, &pbottom, rect->bottomLat, rect->bottomLon);
		width = pbottom.x  - ptop.x;
		height = pbottom.y - ptop.y;
    
		pmdc->DrawLine(ptop.x,ptop.y, pbottom.x,ptop.y);
		pmdc->DrawLine(pbottom.x,ptop.y, pbottom.x,pbottom.y);
		pmdc->DrawLine(pbottom.x,pbottom.y,ptop.x,pbottom.y);
		pmdc->DrawLine(ptop.x,pbottom.y,ptop.x,ptop.y);  
    }
}




void ncdfOverlayFactory::DrawOLBitmap(const wxBitmap &bitmap, wxCoord x, wxCoord y, bool usemask)
{
	wxBitmap bmp;
	if (x < 0 || y < 0) {
		int dx = (x < 0 ? -x : 0);
		int dy = (y < 0 ? -y : 0);
		int w = bitmap.GetWidth() - dx;
		int h = bitmap.GetHeight() - dy;
		/* picture is out of viewport */
		if (w <= 0 || h <= 0) return;
		wxBitmap newBitmap = bitmap.GetSubBitmap(wxRect(dx, dy, w, h));
		x += dx;
		y += dy;
		bmp = newBitmap;
	}
	else {
		bmp = bitmap;
	}
	if (m_pdc)
		m_pdc->DrawBitmap(bmp, x, y, usemask);
	else {
		wxImage image = bmp.ConvertToImage();
		int w = image.GetWidth(), h = image.GetHeight();

		if (usemask) {
			unsigned char *d = image.GetData();
			unsigned char *a = image.GetAlpha();

			unsigned char mr, mg, mb;
			if (!image.GetOrFindMaskColour(&mr, &mg, &mb) && !a) printf(
				"trying to use mask to draw a bitmap without alpha or mask\n");

			unsigned char *e = new unsigned char[4 * w * h];
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) {
						unsigned char r, g, b;
						int off = (y * image.GetWidth() + x);
						r = d[off * 3 + 0];
						g = d[off * 3 + 1];
						b = d[off * 3 + 2];

						e[off * 4 + 0] = r;
						e[off * 4 + 1] = g;
						e[off * 4 + 2] = b;

						e[off * 4 + 3] =
							a ? a[off] : ((r == mr) && (g == mg) && (b == mb) ? 0 : 255);
					}
			}

			glColor4f(1, 1, 1, 1);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glRasterPos2i(x, y);
			glPixelZoom(1, -1);
			glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, e);
			glPixelZoom(1, 1);
			glDisable(GL_BLEND);

			delete[](e);
		}
		else {
			glRasterPos2i(x, y);
			glPixelZoom(1, -1); /* draw data from top to bottom */
			glDrawPixels(w, h, GL_RGB, GL_UNSIGNED_BYTE, image.GetData());
			glPixelZoom(1, 1);
		}
	}
}

void ncdfOverlayFactory::DrawAllCurrentsInViewPort(double dlat, double dlon, double ddir, double dfor, wxDC &myDC, PlugIn_ViewPort *myVP)
{
	
	if (myVP->chart_scale > 1000000){
		return;
	}

	wxDC *m_dc = &myDC;

	double rot_vp = myVP->rotation * 180 / M_PI;

	// Set up the scaler
	int mmx, mmy;
	wxDisplaySizeMM(&mmx, &mmy);

	int sx, sy;
	wxDisplaySize(&sx, &sy);

	double m_pix_per_mm = ((double)sx) / ((double)mmx);

	int mm_per_knot = 10;
	float current_draw_scaler = mm_per_knot * m_pix_per_mm * 100 / 100.0;

	// End setting up scaler

	float tcvalue, dir;

	tcvalue = dfor;
	dir = ddir;
	
	double lat, lon;

	lat = dlat;
	lon = dlon;

	int pixxc, pixyc;
	wxPoint cpoint;
	GetCanvasPixLL(myVP, &cpoint, lat, lon);
	
	pixxc = cpoint.x;
	pixyc = cpoint.y;


	//    Adjust drawing size using logarithmic scale
	double a1 = fabs(tcvalue) * 10;
	a1 = wxMax(1.0, a1);      // Current values less than 0.1 knot
	// will be displayed as 0
	double a2 = log10(a1);
	double scale = current_draw_scaler * a2;
	
	drawCurrentArrow(pixxc, pixyc, dir - 90 + rot_vp, scale / 100, tcvalue, myDC, myVP);
	
	int shift = 0;

	if (!m_dc){

		wxColour colour;
		colour = GetSeaCurrentGraphicColor(tcvalue);
		c_GLcolour = colour;  // for filling GL arrows

		drawGLPolygons(this, m_dc, vp, DrawGLPolygon(), lat, lon, shift);
	}

	shift = 5;

	if (!m_dc){		
		
		DrawGLLabels(this, m_dc, vp,  DrawGLText(fabs(tcvalue), 1), lat, lon, 0);
		shift = 13;
		
		DrawGLLabels(this, m_dc,vp, DrawGLTextDir(dir, 0), lat, lon, shift);		
	}

	char sbuf[20];
	
	wxFont pTCFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false,
		wxString(_T("Eurostile Extended")));

	if (m_dc)
	{
		m_dc->SetFont(pTCFont);
		snprintf(sbuf, 19, "%3.1f", fabs(tcvalue));
		m_dc->DrawText(wxString(sbuf, wxConvUTF8), pixxc, pixyc);
		shift = 13;
	}

	if (m_dc)
	{
		snprintf(sbuf, 19, "%03.0f", dir);
		m_dc->DrawText(wxString(sbuf, wxConvUTF8), pixxc, pixyc + shift);
	}



}

void ncdfOverlayFactory::DrawAllGLCurrentsInViewPort(double dlat, double dlon, double ddir, double dfor, wxGLContext *pcontext, PlugIn_ViewPort *myVP)
{

	if (myVP->chart_scale > 1000000){
		return;
	}

	m_pdc = NULL;
	

	double rot_vp = myVP->rotation * 180 / M_PI;

	// Set up the scaler
	int mmx, mmy;
	wxDisplaySizeMM(&mmx, &mmy);

	int sx, sy;
	wxDisplaySize(&sx, &sy);

	double m_pix_per_mm = ((double)sx) / ((double)mmx);

	int mm_per_knot = 10;
	float current_draw_scaler = mm_per_knot * m_pix_per_mm * 100 / 100.0;

	// End setting up scaler

	float tcvalue, dir;

	tcvalue = dfor;
	dir = ddir;

	double lat, lon;

	lat = dlat;
	lon = dlon;

	int pixxc, pixyc;
	wxPoint cpoint;
	GetCanvasPixLL(myVP, &cpoint, lat, lon);

	pixxc = cpoint.x;
	pixyc = cpoint.y;


	//    Adjust drawing size using logarithmic scale
	double a1 = fabs(tcvalue) * 10;
	a1 = wxMax(1.0, a1);      // Current values less than 0.1 knot
	// will be displayed as 0
	double a2 = log10(a1);
	double scale = current_draw_scaler * a2;

	drawCurrentArrow(pixxc, pixyc, dir - 90 + rot_vp, scale / 100, tcvalue, *m_pdc, myVP);

	int shift = 0;

	

		wxColour colour;
		colour = GetSeaCurrentGraphicColor(tcvalue);
		c_GLcolour = colour;  // for filling GL arrows

		drawGLPolygons(this, m_pdc, myVP, DrawGLPolygon(), lat, lon, shift);
	
		DrawGLLabels(this, m_pdc, myVP, DrawGLText(fabs(tcvalue), 1), lat, lon, 0);
		shift = 13;

		DrawGLLabels(this, m_pdc, myVP, DrawGLTextDir(dir, 0), lat, lon, shift);

}

void ncdfOverlayFactory::drawCurrentArrow(int x, int y, double rot_angle, double scale, double rate, wxDC &dc, PlugIn_ViewPort *vp)
{
	double m_rate = fabs(rate);
	wxPoint p[9];

	wxColour colour;
	colour = GetSeaCurrentGraphicColor(m_rate);

	c_GLcolour = colour;  // for filling GL arrows

	wxPen pen(colour, 2);
	wxBrush brush(colour);

	myDC = &dc;

	if (myDC) {
		myDC->SetPen(pen);
		myDC->SetBrush(brush);
	}
	

	if (scale > 1e-2) {

		float sin_rot = sin(rot_angle * PI / 180.);
		float cos_rot = cos(rot_angle * PI / 180.);

		// Move to the first point

		float xt = CurrentArrowArray[0].x;
		float yt = CurrentArrowArray[0].y;

		float xp = (xt * cos_rot) - (yt * sin_rot);
		float yp = (xt * sin_rot) + (yt * cos_rot);
		int x1 = (int)(xp * scale);
		int y1 = (int)(yp * scale);

		p[0].x = x;
		p[0].y = y;

		p_basic[0].x = 100;
		p_basic[0].y = 100;

		// Walk thru the point list
		for (int ip = 1; ip < NUM_CURRENT_ARROW_POINTS; ip++) {
			xt = CurrentArrowArray[ip].x;
			yt = CurrentArrowArray[ip].y;

			float xp = (xt * cos_rot) - (yt * sin_rot);
			float yp = (xt * sin_rot) + (yt * cos_rot);
			int x2 = (int)(xp * scale);
			int y2 = (int)(yp * scale);

			p_basic[ip].x = 100 + x2;
			p_basic[ip].y = 100 + y2;
			
			

			if (myDC){
				myDC->DrawLine(x1 + x, y1 + y, x2 + x, y2 + y);
			}
			else{
				DrawGLLine(x1 + x, y1 + y, x2 + x, y2 + y, 2, colour);
			}

			p[ip].x = x1 + x;
			p[ip].y = y1 + y;

			x1 = x2;
			y1 = y2;
		}
		
		if (myDC){
			myDC->SetBrush(brush);
			myDC->DrawPolygon(9, p);
		}
		
	}
}

wxImage &ncdfOverlayFactory::DrawGLPolygon(){

	wxString labels;
	labels = _T("");  // dummy label for drawing with

	wxColour c_orange = c_GLcolour;

	wxPen penText(c_orange);
	wxBrush backBrush(c_orange);

	wxMemoryDC mdc(wxNullBitmap);

	wxFont mfont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mdc.SetFont(mfont);

	int w, h;
	mdc.GetTextExtent(labels, &w, &h);

	w = 200;
	h = 200;

	wxBitmap bm(w, h);
	mdc.SelectObject(bm);
	mdc.Clear();

	mdc.SetPen(penText);
	mdc.SetBrush(backBrush);
	mdc.SetTextForeground(c_orange);
	mdc.SetTextBackground(c_orange);

	int xd = 0;
	int yd = 0;

	mdc.DrawPolygon(9, p_basic, 0);

	mdc.SelectObject(wxNullBitmap);

	m_labelCacheText[labels] = bm.ConvertToImage();

	m_labelCacheText[labels].InitAlpha();

	wxImage &image = m_labelCacheText[labels];

	unsigned char *d = image.GetData();
	unsigned char *a = image.GetAlpha();

	w = image.GetWidth(), h = image.GetHeight();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int r, g, b;
			int ioff = (y * w + x);
			r = d[ioff * 3 + 0];
			g = d[ioff * 3 + 1];
			b = d[ioff * 3 + 2];

			a[ioff] = 255 - (r + g + b) / 3;
		}

	return m_labelCacheText[labels];

}
void ncdfOverlayFactory::drawGLPolygons(ncdfOverlayFactory *pof, wxDC *dc,
	PlugIn_ViewPort *vp, wxImage &imageLabel, double myLat, double myLon, int offset)
{

	//---------------------------------------------------------
	// Ecrit les labels
	//---------------------------------------------------------

	wxPoint ab;
	GetCanvasPixLL(vp, &ab, myLat, myLon);

	wxPoint cd;
	GetCanvasPixLL(vp, &cd, myLat, myLon);

	int w = imageLabel.GetWidth();
	int h = imageLabel.GetHeight();

	int label_offset = 0;
	int xd = (ab.x + cd.x - (w + label_offset * 2)) / 2;
	int yd = (ab.y + cd.y - h) / 2 + offset;

	if (dc) {
		/* don't use alpha for isobars, for some reason draw bitmap ignores
		the 4th argument (true or false has same result) */
		wxImage img(w, h, imageLabel.GetData(), true);
		dc->DrawBitmap(img, xd, yd, false);
	}
	else { /* opengl */

		int w = imageLabel.GetWidth(), h = imageLabel.GetHeight();

		unsigned char *d = imageLabel.GetData();
		unsigned char *a = imageLabel.GetAlpha();

		unsigned char mr, mg, mb;
		if (!imageLabel.GetOrFindMaskColour(&mr, &mg, &mb) && !a) wxMessageBox(_T(
			"trying to use mask to draw a bitmap without alpha or mask\n"));

		unsigned char *e = new unsigned char[4 * w * h];
		{
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					unsigned char r, g, b;
					int off = (y * imageLabel.GetWidth() + x);
					r = d[off * 3 + 0];
					g = d[off * 3 + 1];
					b = d[off * 3 + 2];

					e[off * 4 + 0] = r;
					e[off * 4 + 1] = g;
					e[off * 4 + 2] = b;

					e[off * 4 + 3] =
						a ? a[off] : ((r == mr) && (g == mg) && (b == mb) ? 0 : 255);
				}
		}

		glColor4f(1, 1, 1, 1);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glRasterPos2i(xd, yd);
		glPixelZoom(1, -1);
		glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, e);
		glPixelZoom(1, 1);
		glDisable(GL_BLEND);

		delete[](e);

	}


}

void ncdfOverlayFactory::DrawGLLabels(ncdfOverlayFactory *pof, wxDC *dc,
	PlugIn_ViewPort *vp, wxImage &imageLabel, double myLat, double myLon, int offset)
{

	//---------------------------------------------------------
	// Ecrit les labels
	//---------------------------------------------------------

	wxPoint ab;
	GetCanvasPixLL(vp, &ab, myLat, myLon);

	wxPoint cd;
	GetCanvasPixLL(vp, &cd, myLat, myLon);

	int w = imageLabel.GetWidth();
	int h = imageLabel.GetHeight();

	int label_offset = 0;
	int xd = (ab.x + cd.x - (w + label_offset * 2)) / 2;
	int yd = (ab.y + cd.y - h) / 2 + offset;

	if (dc) {
		/* don't use alpha for isobars, for some reason draw bitmap ignores
		the 4th argument (true or false has same result) */
		wxImage img(w, h, imageLabel.GetData(), true);
		dc->DrawBitmap(img, xd, yd, false);
	}
	else { /* opengl */

		int w = imageLabel.GetWidth(), h = imageLabel.GetHeight();

		unsigned char *d = imageLabel.GetData();
		unsigned char *a = imageLabel.GetAlpha();

		unsigned char mr, mg, mb;
		if (!imageLabel.GetOrFindMaskColour(&mr, &mg, &mb) && !a) wxMessageBox(_T(
			"trying to use mask to draw a bitmap without alpha or mask\n"));

		unsigned char *e = new unsigned char[4 * w * h];
		{
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					unsigned char r, g, b;
					int off = (y * imageLabel.GetWidth() + x);
					r = d[off * 3 + 0];
					g = d[off * 3 + 1];
					b = d[off * 3 + 2];

					e[off * 4 + 0] = r;
					e[off * 4 + 1] = g;
					e[off * 4 + 2] = b;

					e[off * 4 + 3] =
						a ? a[off] : ((r == mr) && (g == mg) && (b == mb) ? 0 : 255);
				}
		}

		glColor4f(1, 1, 1, 1);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glRasterPos2i(xd, yd);
		glPixelZoom(1, -1);
		glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, e);
		glPixelZoom(1, 1);
		glDisable(GL_BLEND);

		delete[](e);

	}


}


wxImage &ncdfOverlayFactory::DrawGLText(double value, int precision){

	wxString labels;

	int p = precision;

	labels.Printf(_T("%.*f"), p, value);

	wxMemoryDC mdc(wxNullBitmap);

	wxFont pTCFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false,
		wxString(_T("Eurostile Extended")));
	mdc.SetFont(pTCFont);

	int w, h;
	mdc.GetTextExtent(labels, &w, &h);

	int label_offset = 10;   //5

	wxBitmap bm(w + label_offset * 2, h + 1);
	mdc.SelectObject(bm);
	mdc.Clear();

	wxColour text_color;

	GetGlobalColor(_T("UINFD"), &text_color);
	wxPen penText(text_color);
	mdc.SetPen(penText);

	mdc.SetBrush(*wxTRANSPARENT_BRUSH);
	mdc.SetTextForeground(text_color);
	mdc.SetTextBackground(wxTRANSPARENT);

	int xd = 0;
	int yd = 0;

	mdc.DrawText(labels, label_offset + xd, yd + 1);

	mdc.SelectObject(wxNullBitmap);

	m_labelCache[value] = bm.ConvertToImage();

	m_labelCache[value].InitAlpha();

	wxImage &image = m_labelCache[value];

	unsigned char *d = image.GetData();
	unsigned char *a = image.GetAlpha();

	w = image.GetWidth(), h = image.GetHeight();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int r, g, b;
			int ioff = (y * w + x);
			r = d[ioff * 3 + 0];
			g = d[ioff * 3 + 1];
			b = d[ioff * 3 + 2];

			a[ioff] = 255 - (r + g + b) / 3;
		}

	return m_labelCache[value];
}

wxImage &ncdfOverlayFactory::DrawGLTextDir(double value, int precision){

	wxString labels;

	int p = precision;

	labels.Printf(_T("%03.*f"), p, value);

	wxMemoryDC mdc(wxNullBitmap);

	wxFont pTCFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false,
		wxString(_T("Eurostile Extended")));

	mdc.SetFont(pTCFont);

	int w, h;
	mdc.GetTextExtent(labels, &w, &h);

	int label_offset = 10;   //5

	wxBitmap bm(w + label_offset * 2, h + 1);
	mdc.SelectObject(bm);
	mdc.Clear();

	wxColour text_color;

	GetGlobalColor(_T("UINFD"), &text_color);
	wxPen penText(text_color);
	mdc.SetPen(penText);

	mdc.SetBrush(*wxTRANSPARENT_BRUSH);
	mdc.SetTextForeground(text_color);
	mdc.SetTextBackground(wxTRANSPARENT);

	int xd = 0;
	int yd = 0;

	mdc.DrawText(labels, label_offset + xd, yd + 1);

	mdc.SelectObject(wxNullBitmap);

	m_labelCache[value] = bm.ConvertToImage();

	m_labelCache[value].InitAlpha();

	wxImage &image = m_labelCache[value];

	unsigned char *d = image.GetData();
	unsigned char *a = image.GetAlpha();

	w = image.GetWidth(), h = image.GetHeight();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int r, g, b;
			int ioff = (y * w + x);
			r = d[ioff * 3 + 0];
			g = d[ioff * 3 + 1];
			b = d[ioff * 3 + 2];

			a[ioff] = 255 - (r + g + b) / 3;
		}

	return m_labelCache[value];
}

wxImage &ncdfOverlayFactory::DrawGLTextString(wxString myText){

	wxString labels;
	labels = myText;

	wxMemoryDC mdc(wxNullBitmap);

	wxFont pTCFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false,
		wxString(_T("Eurostile Extended")));
	mdc.SetFont(pTCFont);

	int w, h;
	mdc.GetTextExtent(labels, &w, &h);

	int label_offset = 10;   //5

	wxBitmap bm(w + label_offset * 2, h + 1);
	mdc.SelectObject(bm);
	mdc.Clear();

	wxColour text_color;

	GetGlobalColor(_T("UINFD"), &text_color);
	wxPen penText(text_color);
	mdc.SetPen(penText);

	mdc.SetBrush(*wxTRANSPARENT_BRUSH);
	mdc.SetTextForeground(text_color);
	mdc.SetTextBackground(wxTRANSPARENT);

	int xd = 0;
	int yd = 0;

	mdc.DrawText(labels, label_offset + xd, yd + 1);
	mdc.SelectObject(wxNullBitmap);

	m_labelCacheText[myText] = bm.ConvertToImage();

	m_labelCacheText[myText].InitAlpha();

	wxImage &image = m_labelCacheText[myText];

	unsigned char *d = image.GetData();
	unsigned char *a = image.GetAlpha();

	w = image.GetWidth(), h = image.GetHeight();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int r, g, b;
			int ioff = (y * w + x);
			r = d[ioff * 3 + 0];
			g = d[ioff * 3 + 1];
			b = d[ioff * 3 + 2];

			a[ioff] = 255 - (r + g + b) / 3;
		}

	return m_labelCacheText[myText];
}

void ncdfOverlayFactory::drawPetiteBarbule(wxDC *pmdc, wxPen pen, bool south,
                                 double si, double co, int di, int dj, int b)
{
      if (south)
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+2, -5);
      else
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+2, 5);
}

void ncdfOverlayFactory::drawGrandeBarbule(wxDC *pmdc, wxPen pen, bool south,
                                 double si, double co, int di, int dj, int b)
{
      if (south)
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+4,-10);
      else
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+4,10);
}


void ncdfOverlayFactory::drawTriangle(wxDC *pmdc, wxPen pen, bool south,
                            double si, double co, int di, int dj, int b)
{
      if (south) {
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+4,-10);
            drawTransformedLine(pen, si,co, di,dj,  b+8,0,  b+4,-10);
      }
      else {
            drawTransformedLine(pen, si,co, di,dj,  b,0,  b+4,10);
            drawTransformedLine(pen, si,co, di,dj,  b+8,0,  b+4,10);
      }
}


//===================================================================
// Line Integral Convolution (LIC) for current vector field
//===================================================================
wxColour ncdfOverlayFactory::GetSeaCurrentGraphicColor(double val_in)
{
    static const double stops[][4] = {
        {0.00,  20,  20, 180}, {0.10,  30,  80, 220}, {0.25,   0, 180, 220},
        {0.50,   0, 200,  80}, {0.75, 220, 220,  20}, {1.00, 240, 100,  20},
        {1.50, 220,  20,  20},
    };
    return InterpolateStops(stops, 7, wxMax(val_in, 0.0), false);
}



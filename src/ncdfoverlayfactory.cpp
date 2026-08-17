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
#include <wx/colour.h>
#include <wx/dynarray.h>

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
      m_useShader = false;
      m_currentInterpMode = 0;
      m_currentSmoothColors = false;
      m_currentSCurve = false;
      m_currentSlopeShading = false;
      m_currentDataMin = 0.0f;
      m_currentDataMax = 1.0f;
      m_currentSlopeMode = 0;
      m_glDispTexture = 0;
      m_bHasDispTexture = false;

      // Animation timer: triggers periodic redraws when animation is active
      m_animateTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
          GetOCPNCanvasWindow()->Refresh(false);
      });
      m_cachedVorticity = NULL;
      m_cachedVortNj = m_cachedVortNi = 0;
      m_glLICTexture = 0;
      m_bHasLICTexture = false;
      m_bNeedsLICRebuild = true;
      m_glVortTexture = 0;
      m_bHasVortTexture = false;

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
    m_currentOverlay.Cleanup();
    m_seaTempOverlay.Cleanup();
    m_salinityOverlay.Cleanup();
    ClearParticles();
    DeleteLICTexture();
    DeleteDispTexture();
    if (m_animateTimer.IsRunning()) m_animateTimer.Stop();
    // Clean up animation textures
    if (m_bHasAnimColorTexture && m_glAnimColorTexture) { glDeleteTextures(1, &m_glAnimColorTexture); }
    if (m_bHasAnimSeaTempTexture && m_glAnimSeaTempTexture) { glDeleteTextures(1, &m_glAnimSeaTempTexture); }
    if (m_bHasAnimSalinityTexture && m_glAnimSalinityTexture) { glDeleteTextures(1, &m_glAnimSalinityTexture); }
    if (m_bHasVortTexture && m_glVortTexture) { glDeleteTextures(1, &m_glVortTexture); m_glVortTexture = 0; m_bHasVortTexture = false; }
    FreeSharpenedGrid(m_cachedVorticity, m_cachedVortNj);
    m_cachedVorticity = NULL;
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
    // Force texture rebuild to switch between GL_NEAREST and GL_LINEAR
    m_currentOverlay.Invalidate();
    m_seaTempOverlay.Invalidate();
    m_salinityOverlay.Invalidate();
}

void ncdfOverlayFactory::setData(MainDialog *gui, ncdf_pi *plugin, const ncdfDataMessage& g2data, int numberOfPoints, wxDouble tlat, wxDouble tlon, wxDouble blat, wxDouble blon)
{
	// Mark textures for rebuild (GL deletion deferred to render)
	m_currentOverlay.Invalidate();
	m_seaTempOverlay.Invalidate();
	m_salinityOverlay.Invalidate();
	ClearParticles();
	m_last_vp_scale = -1;
	m_last_vp_latMax = -99999.0;
	m_bUpdateParticles = true;
	m_particleBurstCount = 30;

	// Don't deep copy data — use gui->myMessage directly (avoids 140MB copy for global data)
	this->numberOfPoints = numberOfPoints;
    this->gui = gui;
    this->plugin = plugin;

    // Use the bounds passed from readncdfFile (from the actual data)
    // Sort to ensure correct ordering: north > south, west < east
    this->tlat = wxMax(tlat, blat);
    this->blat = wxMin(tlat, blat);
    this->tlon = wxMin(tlon, blon);
    this->blon = wxMax(tlon, blon);

	wxLogMessage(_T("[setData] bounds: lat[%.2f,%.2f] lon[%.2f,%.2f] ni=%d nj=%d"),
        this->tlat, this->blat, this->tlon, this->blon,
        (int)gui->myMessage.lonLength, (int)gui->myMessage.latLength);

    this->m_bReadyToRender = true;
}

void ncdfOverlayFactory::setSelectionRectangle(Selection *rect)
{
    this->rect = rect;
}

void ncdfOverlayFactory::reset()
{
    this->m_bReadyToRender = false;
    clearBmp();
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

    // Guard: must have valid gui and at least one grid
    if (!gui) return false;
    bool hasCurrentGrid = gui->myMessage.hasCurrent();
    bool hasSSTGrid = gui->myMessage.hasSSTData();
    bool hasSalinityGrid = gui->myMessage.hasSalData();
    if (!hasCurrentGrid && !hasSSTGrid && !hasSalinityGrid) {
        static int s_nullDbg = 0;
        if (s_nullDbg < 5) { wxLogMessage(_T("[render] SKIP: ucurr.size=%zu vcurr.size=%zu sst.size=%zu sal.size=%zu"), gui->myMessage.ucurr.size(), gui->myMessage.vcurr.size(), gui->myMessage.sst.size(), gui->myMessage.salinity.size()); s_nullDbg++; }
        return false;
    }
    if (gui->myMessage.lonLength < 2 || gui->myMessage.latLength < 2) return false;
    if (!m_bReadyToRender) return false;

    // Must have at least one rendering feature enabled
    if (!plugin->m_bShowCurrentForce && !plugin->m_bShowCurrentDir &&
        !plugin->m_bShowParticles && !plugin->m_bShowSeaTemp &&
        !plugin->m_bShowSeaTempIso && !plugin->m_bShowSalinity) return false;

    // Lazy shader compile (first render call, GL context is available)
    // Shader is compiled once; m_useShader toggles via checkbox
    static bool s_shaderCompiled = false;
    static bool s_shaderOk = false;
    if (!s_shaderCompiled) {
        s_shaderCompiled = true;
        s_shaderOk = ncdf_shader_init();
    }
    // m_useShader is set per data type below
    m_useShader = false;

    static bool s_shaderDbg = false;
    if (!s_shaderDbg) {
        s_shaderDbg = true;
        wxLogMessage(_T("[shader] compiled=%d ok=%d m_useShader=%d"),
                     (int)s_shaderCompiled, (int)s_shaderOk, (int)m_useShader);
    }

    // Animation: DISABLED for now — needs per-type advection to avoid
    // cross-contaminating SST/salinity with current data.
    // TODO: re-enable with per-type animGrid (one per data type)
    double** animGrid = NULL;

    // ... normal rendering uses animGrid when available ...

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

	// Current color map overlay (delegated to per-type overlay)
	if(plugin->m_bShowCurrentForce && gui->myMessage.hasCurrent() && !m_pdc) {
#ifdef ocpnUSE_GL
	      glDisable(GL_TEXTURE_2D);
	      glBindTexture(GL_TEXTURE_2D, 0);
	      m_useShader = s_shaderOk && (plugin->m_settingsCurrent.interpMode >= 1);
	      m_currentInterpMode = plugin->m_settingsCurrent.interpMode;
	      m_currentSmoothColors = plugin->m_settingsCurrent.smoothColors;
	      m_currentSCurve = plugin->m_settingsCurrent.sCurve;
	      m_currentSlopeShading = plugin->m_settingsCurrent.slopeShading;
	      m_currentSlopeMode = 0;
	      m_currentDataMin = m_currentOverlay.cachedDataMin;
	      m_currentDataMax = m_currentOverlay.cachedDataMax;
	      m_currentOverlay.RenderColorMap(vp, gui, plugin, this, m_useShader, m_currentInterpMode);
#endif
	}


	// Arrows
	if(plugin->m_bShowCurrentDir) {
      RenderncdfCurrent();
	}

	// Particles
	if(plugin->m_bShowParticles) {
      RenderParticles(vp);
	} else {
      ClearParticles();
	}

	// Sea temperature color map overlay
	if (plugin->m_bShowSeaTemp && gui && gui->myMessage.hasSSTData() && !m_pdc) {
#ifdef ocpnUSE_GL
		m_useShader = s_shaderOk && (plugin->m_settingsSeaTemp.interpMode >= 1);
		m_currentInterpMode = plugin->m_settingsSeaTemp.interpMode;
		m_currentSmoothColors = plugin->m_settingsSeaTemp.smoothColors;
		m_currentSCurve = plugin->m_settingsSeaTemp.sCurve;
		m_currentSlopeShading = plugin->m_settingsSeaTemp.slopeShading;
		m_currentSlopeMode = 1;
		m_currentDataMin = m_seaTempOverlay.cachedDataMin;
		m_currentDataMax = m_seaTempOverlay.cachedDataMax;
		m_seaTempOverlay.RenderColorMap(vp, gui, plugin, this, m_useShader, m_currentInterpMode);
#endif
	}

	// Sea temperature isolines
	if (plugin->m_bShowSeaTempIso && gui && gui->myMessage.hasSSTData()) {
		m_seaTempOverlay.RenderIsoLines(vp, gui);
	}

	// Salinity color map overlay
	if (plugin->m_bShowSalinity && gui && gui->myMessage.hasSalData() && !m_pdc) {
#ifdef ocpnUSE_GL
		m_useShader = s_shaderOk && (plugin->m_settingsSalinity.interpMode >= 1);
		m_currentInterpMode = plugin->m_settingsSalinity.interpMode;
		m_currentSmoothColors = plugin->m_settingsSalinity.smoothColors;
		m_currentSCurve = plugin->m_settingsSalinity.sCurve;
		m_currentSlopeShading = plugin->m_settingsSalinity.slopeShading;
		m_currentSlopeMode = 0;
		m_currentDataMin = m_salinityOverlay.cachedDataMin;
		m_currentDataMax = m_salinityOverlay.cachedDataMax;
		m_salinityOverlay.RenderColorMap(vp, gui, plugin, this, m_useShader, m_currentInterpMode);
#endif
	}

	// Salinity isolines
	if (plugin->m_settingsSalinity.showIsoLines && plugin->m_bShowSalinity && gui && gui->myMessage.hasSalData()) {
		m_salinityOverlay.RenderIsoLines(vp, gui);
	}

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
		if (plugin->m_bShowSeaTemp && gui && gui->myMessage.hasSSTData()) {
			m_legend.SetData(tempStops, 9, "\xC2\xB0" "C", "Sea Temperature");
			m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
		} else if (plugin->m_bShowSalinity && gui && gui->myMessage.hasSalData()) {
			m_legend.SetData(salStops, 14, "PSU", "Salinity");
			m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
		} else if (plugin->m_bShowCurrentForce && gui && gui->myMessage.hasCurrent()) {
			m_legend.SetData(currStops, 7, "m/s", "Current Speed");
			m_legend.Draw((int)vp->pix_width, (int)vp->pix_height);
		}
	}

    m_last_vp_scale = vp->view_scale_ppm;
    m_last_vp_latMax = vp->lat_max;

    // Advance animation frame
    if (animGrid) m_animate.AdvanceFrame();

    // Reset GL state to avoid corrupting other plugins (GRIB, OpenCPN core)
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);
    glBindTexture(GL_TEXTURE_2D, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    return true;
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
// Adaptive Laplacian sharpening based on gradient magnitude
//===================================================================
double** ncdfOverlayFactory::BuildSharpenedGrid(double** grid, int nj, int ni)
{
    if (!grid || ni < 3 || nj < 3) return NULL;
    double** out = new(std::nothrow) double*[nj];
    if (!out) return NULL;
    for (int j = 0; j < nj; j++) {
        out[j] = new(std::nothrow) double[ni];
        if (!out[j]) { for (int k = 0; k < j; k++) delete[] out[k]; delete[] out; return NULL; }
    }

    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            double v = grid[j][i];
            if (v == ncdf_NOTDEF || v != v || !isfinite(v)) {
                out[j][i] = v;
                continue;
            }
            // Boundary: copy original
            if (j == 0 || j == nj-1 || i == 0 || i == ni-1) {
                out[j][i] = v;
                continue;
            }
            // Check 4-neighbors for valid data
            double vn = grid[j-1][i], vs = grid[j+1][i];
            double vw = grid[j][i-1], ve = grid[j][i+1];
            if (vn == ncdf_NOTDEF || vs == ncdf_NOTDEF ||
                vw == ncdf_NOTDEF || ve == ncdf_NOTDEF) {
                out[j][i] = v;
                continue;
            }
            // Gradient magnitude
            double gx = ve - vw;
            double gy = vs - vn;
            double mag = sqrt(gx * gx + gy * gy);
            // Laplacian
            double lap = vn + vs + vw + ve - 4.0 * v;
            // Adaptive strength: stronger at edges (0.5~2.0)
            double strength = 0.5 + 1.5 * (1.0 - exp(-mag * 2.0));
            out[j][i] = v - strength * lap;
        }
    }
    return out;
}

void ncdfOverlayFactory::FreeSharpenedGrid(double** grid, int nj)
{
    if (!grid) return;
    for (int j = 0; j < nj; j++) delete[] grid[j];
    delete[] grid;
}

//===================================================================
// Vorticity: curl of velocity field (∂v/∂x - ∂u/∂y)
//===================================================================
double** ncdfOverlayFactory::BuildVorticityGrid(double** uGrid, double** vGrid, int nj, int ni)
{
    if (!uGrid || !vGrid || ni < 3 || nj < 3) return NULL;
    double** out = new(std::nothrow) double*[nj];
    if (!out) return NULL;
    for (int j = 0; j < nj; j++) {
        out[j] = new(std::nothrow) double[ni];
        if (!out[j]) { for (int k = 0; k < j; k++) delete[] out[k]; delete[] out; return NULL; }
    }
    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            if (uGrid[j][i] == ncdf_NOTDEF || vGrid[j][i] == ncdf_NOTDEF ||
                j == 0 || j == nj-1 || i == 0 || i == ni-1) {
                out[j][i] = 0;
                continue;
            }
            double dVdx = vGrid[j][i+1] - vGrid[j][i-1];
            double dUdy = uGrid[j+1][i] - uGrid[j-1][i];
            out[j][i] = dVdx - dUdy;
        }
    }
    return out;
}

//===================================================================
// Guided Anisotropic Diffusion (Perona-Malik, edge-preserving)
//===================================================================
double** ncdfOverlayFactory::BuildAnisoDiffusedGrid(double** grid, int nj, int ni)
{
    if (!grid || ni < 3 || nj < 3) return NULL;

    // Allocate two buffers and 4 coefficient arrays (one per direction)
    double** bufA = new(std::nothrow) double*[nj];
    double** bufB = new(std::nothrow) double*[nj];
    float* cN = new(std::nothrow) float[nj * ni];
    float* cS = new(std::nothrow) float[nj * ni];
    float* cW = new(std::nothrow) float[nj * ni];
    float* cE = new(std::nothrow) float[nj * ni];
    if (!bufA || !bufB || !cN || !cS || !cW || !cE) {
        delete[] bufA; delete[] bufB; delete[] cN; delete[] cS; delete[] cW; delete[] cE;
        return NULL;
    }
    for (int j = 0; j < nj; j++) {
        bufA[j] = new(std::nothrow) double[ni];
        bufB[j] = new(std::nothrow) double[ni];
        if (!bufA[j] || !bufB[j]) {
            for (int k = 0; k <= j; k++) { delete[] bufA[k]; delete[] bufB[k]; }
            delete[] bufA; delete[] bufB; delete[] cN; delete[] cS; delete[] cW; delete[] cE;
            return NULL;
        }
    }

    // Copy original data
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++)
            bufA[j][i] = grid[j][i];

    // Compute k from data range
    double dMin = 1e30, dMax = -1e30;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            double v = grid[j][i];
            if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                if (v < dMin) dMin = v;
                if (v > dMax) dMax = v;
            }
        }
    double k = (dMax - dMin) * 0.1;
    if (k < 1e-10) k = 1.0;
    double invK2 = 1.0 / (k * k);
    double lambda = 0.2;

    // Pre-compute diffusion coefficients from original data (guided)
    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            int idx = j * ni + i;
            double v = grid[j][i];
            if (v == ncdf_NOTDEF || v != v || !isfinite(v) ||
                j == 0 || j == nj-1 || i == 0 || i == ni-1) {
                cN[idx] = cS[idx] = cW[idx] = cE[idx] = 0.0f;
                continue;
            }
            double vn = grid[j-1][i], vs = grid[j+1][i];
            double vw = grid[j][i-1], ve = grid[j][i+1];
            if (vn == ncdf_NOTDEF || vs == ncdf_NOTDEF ||
                vw == ncdf_NOTDEF || ve == ncdf_NOTDEF) {
                cN[idx] = cS[idx] = cW[idx] = cE[idx] = 0.0f;
                continue;
            }
            double dn = vn - v, ds = vs - v, dw = vw - v, de = ve - v;
            cN[idx] = (float)exp(-dn * dn * invK2);
            cS[idx] = (float)exp(-ds * ds * invK2);
            cW[idx] = (float)exp(-dw * dw * invK2);
            cE[idx] = (float)exp(-de * de * invK2);
        }
    }

    // Iterate: coefficients are fixed (guided), only values change
    const int ITERATIONS = 5;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        double** src = (iter % 2 == 0) ? bufA : bufB;
        double** dst = (iter % 2 == 0) ? bufB : bufA;

        for (int j = 1; j < nj - 1; j++) {
            for (int i = 1; i < ni - 1; i++) {
                int idx = j * ni + i;
                if (cN[idx] == 0.0f && cS[idx] == 0.0f &&
                    cW[idx] == 0.0f && cE[idx] == 0.0f) {
                    dst[j][i] = src[j][i];
                    continue;
                }
                double v = src[j][i];
                dst[j][i] = v + lambda * (
                    cN[idx] * (src[j-1][i] - v) +
                    cS[idx] * (src[j+1][i] - v) +
                    cW[idx] * (src[j][i-1] - v) +
                    cE[idx] * (src[j][i+1] - v));
            }
        }
        // Copy borders
        for (int i = 0; i < ni; i++) { dst[0][i] = src[0][i]; dst[nj-1][i] = src[nj-1][i]; }
        for (int j = 0; j < nj; j++) { dst[j][0] = src[j][0]; dst[j][ni-1] = src[j][ni-1]; }
    }

    // Result is in the buffer that was last written to
    double** result = (ITERATIONS % 2 == 0) ? bufA : bufB;
    double** other  = (ITERATIONS % 2 == 0) ? bufB : bufA;
    for (int j = 0; j < nj; j++) delete[] other[j];
    delete[] other;
    delete[] cN; delete[] cS; delete[] cW; delete[] cE;

    return result;
}

//===================================================================
// Line Integral Convolution (LIC) for current vector field
//===================================================================
void ncdfOverlayFactory::DeleteLICTexture()
{
    if (m_bHasLICTexture && m_glLICTexture) {
        glDeleteTextures(1, &m_glLICTexture);
        m_glLICTexture = 0;
        m_bHasLICTexture = false;
    }
}

void ncdfOverlayFactory::BuildLICTexture(int ni, int nj)
{
    if (!gui || !gui->myMessage.hasCurrent() || ni < 3 || nj < 3) return;

    // Generate float noise field (padded)
    const int STEPS = 40;
    const float STEPSIZE = 0.002f;  // step in normalized texture space
    int pad = 20;
    int padW = ni + 2 * pad;
    int padH = nj + 2 * pad;
    float* noiseField = new(std::nothrow) float[padH * padW];
    if (!noiseField) return;
    srand(42);
    for (int k = 0; k < padH * padW; k++) noiseField[k] = (float)rand() / RAND_MAX;

    unsigned char* licData = new(std::nothrow) unsigned char[ni * nj * 4];
    if (!licData) { delete[] noiseField; return; }

    // Access vector field directly from message (no intermediate grid needed)

    // Normalize vector field to [-1, 1] texture space
    double maxSpeed = 0;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            double uval = gui->myMessage.getU(i, j);
            double vval = gui->myMessage.getV(i, j);
            if (uval == ncdf_NOTDEF || vval == ncdf_NOTDEF) continue;
            double sp = sqrt(uval*uval + vval*vval);
            if (maxSpeed < sp) maxSpeed = sp;
        }
    if (maxSpeed < 1e-10) maxSpeed = 1.0;

    // Helper: bilinear noise lookup in padded field
    // coord in normalized [0,1] → padded field index
    auto sampleNoise = [&](float u, float v) -> float {
        // Map [0,1] to padded grid coordinates
        float px = u * (ni - 1) + pad;
        float py = v * (nj - 1) + pad;
        int xi = (int)px, yi = (int)py;
        if (xi < 0) xi = 0; if (xi >= padW - 1) xi = padW - 2;
        if (yi < 0) yi = 0; if (yi >= padH - 1) yi = padH - 2;
        float fx = px - xi, fy = py - yi;
        return (1-fx)*(1-fy)*noiseField[yi*padW + xi]
             + fx*(1-fy)*noiseField[yi*padW + xi+1]
             + (1-fx)*fy*noiseField[(yi+1)*padW + xi]
             + fx*fy*noiseField[(yi+1)*padW + xi+1];
    };

    // Helper: bilinear vector field lookup
    auto sampleVector = [&](float u, float v, float& outU, float& outV) -> bool {
        float px = u * (ni - 1);
        float py = v * (nj - 1);
        int xi = (int)px, yi = (int)py;
        if (xi < 0 || xi >= ni - 1 || yi < 0 || yi >= nj - 1) return false;
        float fx = px - xi, fy = py - yi;
        double uu = (1-fx)*(1-fy)*gui->myMessage.getU(xi, yi) + fx*(1-fy)*gui->myMessage.getU(xi+1, yi)
                   + (1-fx)*fy*gui->myMessage.getU(xi, yi+1) + fx*fy*gui->myMessage.getU(xi+1, yi+1);
        double vv = (1-fx)*(1-fy)*gui->myMessage.getV(xi, yi) + fx*(1-fy)*gui->myMessage.getV(xi+1, yi)
                   + (1-fx)*fy*gui->myMessage.getV(xi, yi+1) + fx*fy*gui->myMessage.getV(xi+1, yi+1);
        if (uu == ncdf_NOTDEF || vv == ncdf_NOTDEF) return false;
        outU = (float)(uu / maxSpeed);
        outV = (float)(vv / maxSpeed);
        return true;
    };

    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            int off = (j * ni + i) * 4;

            // Land: white (no modulation)
            if (gui->myMessage.getU(i, j) == ncdf_NOTDEF || gui->myMessage.getV(i, j) == ncdf_NOTDEF) {
                licData[off] = licData[off+1] = licData[off+2] = 255;
                licData[off+3] = 255;
                continue;
            }

            // Start at this pixel's normalized position
            float u0 = (float)i / (ni - 1);
            float v0 = (float)j / (nj - 1);

            float runningTotal = 0.0f;
            int sampleCount = 0;

            // Trace forward
            float cu = u0, cv = v0;
            for (int s = 0; s < STEPS; s++) {
                float vu, vv;
                if (!sampleVector(cu, cv, vu, vv)) break;
                cu += vu * STEPSIZE;
                cv += vv * STEPSIZE;
                if (cu < 0 || cu > 1 || cv < 0 || cv > 1) break;
                runningTotal += sampleNoise(cu, cv);
                sampleCount++;
            }

            // Trace backward
            cu = u0; cv = v0;
            for (int s = 0; s < STEPS; s++) {
                float vu, vv;
                if (!sampleVector(cu, cv, vu, vv)) break;
                cu -= vu * STEPSIZE;
                cv -= vv * STEPSIZE;
                if (cu < 0 || cu > 1 || cv < 0 || cv > 1) break;
                runningTotal += sampleNoise(cu, cv);
                sampleCount++;
            }

            // Average (exclude center pixel, like reference implementation)
            float avgValue = (sampleCount > 0) ? runningTotal / sampleCount : 0.5f;

            // Brightness: 0.3 (dark) to 1.0 (white)
            float brightness = 0.3f + 0.7f * avgValue;
            unsigned char bri = (unsigned char)(brightness * 255.0f);
            licData[off] = licData[off+1] = licData[off+2] = bri;
            licData[off+3] = 255;
        }
    }

    delete[] noiseField;

    DeleteLICTexture();
    glGenTextures(1, &m_glLICTexture);
    glBindTexture(GL_TEXTURE_2D, m_glLICTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ni, nj, 0, GL_RGBA, GL_UNSIGNED_BYTE, licData);
    m_bHasLICTexture = true;
    m_bNeedsLICRebuild = false;

    delete[] licData;
}


wxColour ncdfOverlayFactory::GetSeaCurrentGraphicColor(double val_in)
{
    static const double stops[][4] = {
        {0.00,  20,  20, 180}, {0.10,  30,  80, 220}, {0.25,   0, 180, 220},
        {0.50,   0, 200,  80}, {0.75, 220, 220,  20}, {1.00, 240, 100,  20},
        {1.50, 220,  20,  20},
    };
    return InterpolateStops(stops, 7, wxMax(val_in, 0.0), false);
}



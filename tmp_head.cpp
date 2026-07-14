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
#include <wx/colour.h>
#include <wx/dynarray.h>

extern void ncdfLog(const char* format, ...);

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifdef __WXMSW__
#define snprintf _snprintf
#endif // __WXMSW__

#define NUM_CURRENT_ARROW_POINTS 9
static wxPoint CurrentArrowArray[NUM_CURRENT_ARROW_POINTS] = { wxPoint(0, 0), wxPoint(0, -10),
wxPoint(55, -10), wxPoint(55, -25), wxPoint(100, 0), wxPoint(55, 25), wxPoint(55,
10), wxPoint(0, 10), wxPoint(0, 0)};

void LineBuffer::pushLine(float x0, float y0, float x1, float y1) {
    buffer.push_back(x0);
    buffer.push_back(y0);
    buffer.push_back(x1);
    buffer.push_back(y1);
}

void LineBuffer::pushPetiteBarbule(int b, int l) {
    int tilt = (l * 100) / 250;
    pushLine(b, 0, b + tilt, -l);
}

void LineBuffer::pushGrandeBarbule(int b, int l) {
    int tilt = (l * 100) / 250;
    pushLine(b, 0, b + tilt, -l);
}

void LineBuffer::pushTriangle(int b, int l) {
    int dim = (l * 100) / 250;
    pushLine(b, 0, b + dim, -l);
    pushLine(b + (dim * 2), 0, b + dim, -l);
}

void LineBuffer::Finalize() {
    count = buffer.size() / 4;
    lines = new float[buffer.size()];
    int i = 0;
    for (std::list<float>::iterator it = buffer.begin(); it != buffer.end(); it++)
        lines[i++] = *it;
}

inline double square(double x) { return x * x; }

inline int next_power_of_two(int n) {
    int p = 1;
    while (p < n) p *= 2;
    return p;
}

//----------------------------------------------------------------------------------------------------------
//    ncdf Overlay Factory Implementation
//----------------------------------------------------------------------------------------------------------
ncdfOverlayFactory::ncdfOverlayFactory()
{
      hash = NULL;
      
      m_pbm_current 	= NULL;     
      m_glTexture = 0;
      m_bHasGLTexture = false;
      m_texDim[0] = 0;
      m_texDim[1] = 0;
      m_texDataDim[0] = 0;
      m_texDataDim[1] = 0;

      m_glNumbersTexture = 0;
      m_bHasNumbersTexture = false;
      m_glParticlesTexture = 0;
      m_bHasParticlesTexture = false;
      m_pbm_numbers = NULL;
      m_pbm_particles = NULL;
      
      m_bFixedSpacing = false;
      m_colorMapType = COLOR_MAP_CURRENT;
      m_hiDefGraphics = false;
      m_iOverlayTransparency = 50;
      m_iArrowSpacing = 40;
      m_iNumberSpacing = 40;

      // Isoline initialization
      m_bIsoLinesDirty = true;
      m_isoLineSpacing = 0.5;
      m_magGrid = NULL;
      m_magGridNi = 0;
      m_magGridNj = 0;
      m_magGridStep = 1;
      m_magGridMax = 0;

      // Particle initialization
      m_bUpdateParticles = false;
      m_particleHistorySize = 4;
      m_frameCounter = 0;
      m_bIsoLinesCached = false;
      m_cachedArrowScale = -1;
      m_bArrowsDirty = true;

      // Connect particle timer using lambda
      m_tParticleTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
          m_bUpdateParticles = true;
          if (gui && gui->m_parent) RequestRefresh(gui->m_parent);
      });
      
      m_last_vp_latMax = -99999.0;
      m_last_vp_latMin = -99999.0;
      m_last_vp_lonMax = -99999.0;
      m_last_vp_lonMin = -99999.0;
      m_last_data_tlat = -99999.0;
      m_last_data_tlon = -99999.0;
      m_last_data_blat = -99999.0;
      m_last_data_blon = -99999.0;
      rect = NULL;
      
      space[0]=30; space[1]=50; space[2]=100; space[3]=200; space[4]=400; space[5]=600; space[6]=1200;
	  m_bReadyToRender = false;
	  m_bGridFilled = false;
      m_space = 0;
      
      if (wxGetDisplaySize().x > 0) {
          m_pixelMM = (double)PlugInGetDisplaySizeMM() /
                      wxMax(wxGetDisplaySize().x, wxGetDisplaySize().y);
          m_pixelMM = wxMax(.02, m_pixelMM);
      } else {
          m_pixelMM = 0.27;
      }
      
      if (m_pixelMM > 0.2) {
          m_arrowSize = 5.0 / m_pixelMM;
          m_arrowSize = wxMin(m_arrowSize, wxMax(wxGetDisplaySize().x, wxGetDisplaySize().y) / 20);
      } else {
          m_arrowSize = 26;
      }
      
      for (int i = 0; i < 2; i++) {
          int arrowSize;
          int dec2 = 2;
          int dec1 = 5;
          
          if (i == 0) {
              if (m_pixelMM > 0.2) {
                  arrowSize = 5.0 / m_pixelMM;
                  arrowSize = wxMin(arrowSize, wxMax(wxGetDisplaySize().x, wxGetDisplaySize().y) / 20);
                  dec1 = arrowSize / 6;
                  dec2 = arrowSize / 8;
              } else {
                  arrowSize = 26;
              }
          } else {
              arrowSize = 16;
          }
          
          int dec = -arrowSize / 2;
          
          m_SingleArrow[i].pushLine(dec, 0, dec + arrowSize, 0);
          m_SingleArrow[i].pushLine(dec - 2, 0, dec + dec1, dec1 + 1);
          m_SingleArrow[i].pushLine(dec - 2, 0, dec + dec1, -(dec1 + 1));
          m_SingleArrow[i].Finalize();
          
          m_DoubleArrow[i].pushLine(dec, -dec2, dec + arrowSize, -dec2);
          m_DoubleArrow[i].pushLine(dec, dec2, dec + arrowSize, +dec2);
          m_DoubleArrow[i].pushLine(dec - 2, 0, dec + dec1, dec1 + 1);
          m_DoubleArrow[i].pushLine(dec - 2, 0, dec + dec1, -(dec1 + 1));
          m_DoubleArrow[i].Finalize();
      }
}

ncdfOverlayFactory::~ncdfOverlayFactory()
{
    m_bReadyToRender = false;
	renderSelectionRectangle = false;
	if (m_tParticleTimer.IsRunning()) m_tParticleTimer.Stop();
	m_tParticleTimer.Disconnect();
}

void ncdfOverlayFactory::setData(MainDialog *gui, ncdf_pi *plugin, ncdfDataMessage g2data, int numberOfPoints, wxDouble tlat, wxDouble tlon, wxDouble blat, wxDouble blon)
{

	this->g2data = g2data;
	this->numberOfPoints = numberOfPoints;
    this->tlat = tlat; this->tlon = tlon; this->blat = blat; this->blon = blon;
    this->gui = gui;
    this->plugin = plugin;
    
    this->m_bReadyToRender = true;
}

void ncdfOverlayFactory::setSelectionRectangle(Selection *rect)
{
    this->rect = rect;
}

void ncdfOverlayFactory::reset()
{
    this->m_bReadyToRender = false;
    this->m_bGridFilled = false;
    this->m_bIsoLinesDirty = true;
    this->m_bIsoLinesCached = false;
    this->m_isoLineVertices.clear();
    this->m_Particles.clear();
    this->m_frameCounter = 0;
    clearBmp();
}


bool ncdfOverlayFactory::RenderGLncdfOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
	m_pdc = NULL;
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


// Fill coastal data gaps in ucurr/vcurr grids (following grib's FillGrid approach).
// Two-pass scan: first fill latitude gaps, then longitude gaps.
// Only fills cells between two valid neighbors (div > 1) to avoid filling inland.
void ncdfOverlayFactory::FillGridNcdf()
{
    if (m_bGridFilled) return;
    if (!gui || !gui->gridu || !gui->gridv) return;

    int ni = gui->myMessage.noPointsParallel;  // Longitude count
    int nj = gui->myMessage.noPointsMeridian;  // Latitude count

    if (ni <= 0 || nj <= 0) return;

    auto fillGrid = [&](double **grid) {
        // Pass 1: latitude direction (fill between valid north/south neighbors)
        for (int j = 0; j < ni; j++) {
            for (int i = 1; i < nj - 1; i++) {
                if (grid[i][j] == ncdf_NOTDEF) {
                    double acc = 0;
                    int div = 0;
                    if (grid[i - 1][j] != ncdf_NOTDEF) {
                        acc += grid[i - 1][j];
                        div++;
                    }
                    if (grid[i + 1][j] != ncdf_NOTDEF) {
                        acc += grid[i + 1][j];
                        div++;
                    }
                    if (div > 1) grid[i][j] = acc / div;
                }
            }
        }

        // Pass 2: longitude direction (fill between valid east/west neighbors)
        for (int i = 0; i < nj; i++) {
            for (int j = 1; j < ni - 1; j++) {
                if (grid[i][j] == ncdf_NOTDEF) {
                    double acc = 0;
                    int div = 0;
                    if (grid[i][j - 1] != ncdf_NOTDEF) {
                        acc += grid[i][j - 1];
                        div++;
                    }
                    if (grid[i][j + 1] != ncdf_NOTDEF) {
                        acc += grid[i][j + 1];
                        div++;
                    }
                    if (div > 1) grid[i][j] = acc / div;
                }
            }
        }
    };

    fillGrid(gui->gridu);
    fillGrid(gui->gridv);

    // Sync filled grids back to flat arrays (used by GL texture path)
    int c = 0;
    for (int i = 0; i < nj; i++) {
        for (int j = 0; j < ni; j++) {
            gui->myMessage.ucurr[c] = gui->gridu[i][j];
            gui->myMessage.vcurr[c] = gui->gridv[i][j];
            c++;
        }
    }

    m_bGridFilled = true;
}


bool ncdfOverlayFactory::DoRenderncdfOverlay(PlugIn_ViewPort *vp )
{
    if (!gui) return false;
    if (!gui->myMessage.ucurr || !gui->myMessage.vcurr) return false;
    
    this->vp = vp;
    
    if(vp->view_scale_ppm != m_last_vp_scale)
    {
      // Match spacing to zoom level - larger space at zoomed out views
      // to prevent arrows from being too dense (following grib's minspace logic)
      if(vp->view_scale_ppm < 0.0005)
          m_space = space[6];      // 1200 - very zoomed out
      else if(vp->view_scale_ppm < 0.001)
          m_space = space[5];      // 600
      else if(vp->view_scale_ppm < 0.002)
          m_space = space[4];      // 400
      else if(vp->view_scale_ppm < 0.005)
          m_space = space[3];      // 200
      else if(vp->view_scale_ppm < 0.02)
          m_space = space[2];      // 100
      else if(vp->view_scale_ppm < 0.08)
          m_space = space[1];      // 50
      else
          m_space = space[0];      // 30 - very zoomed in

    }
    
    if (m_last_vp_scale != vp->view_scale_ppm)
    {
        clearDcBmp();
    }

    // Only recreate texture when zoom level changes, not on every pan
    // The texture covers the full data area and is repositioned during panning
    if (m_last_vp_scale != vp->view_scale_ppm)
    {
        clearBmp();
    }

    // Check if data coordinates changed (e.g. switching to a different ncdf file)
    if (m_last_data_tlat != tlat || m_last_data_tlon != tlon ||
        m_last_data_blat != blat || m_last_data_blon != blon)
    {
        ncdfLog("[render] DATA COORDS CHANGED: old=(%.1f,%.1f)-(%.1f,%.1f) new=(%.1f,%.1f)-(%.1f,%.1f)\n",
            m_last_data_tlat, m_last_data_tlon, m_last_data_blat, m_last_data_blon,
            tlat, tlon, blat, blon);
        clearBmp();
        m_last_data_tlat = tlat;
        m_last_data_tlon = tlon;
        m_last_data_blat = blat;
        m_last_data_blon = blon;
    }

	if (!m_bReadyToRender) return false;

	// Fill coastal data gaps once per data load
	FillGridNcdf();

	if(plugin->m_bShowCurrentForce)
      RenderncdfCurrentBmp();

	if(plugin->m_bShowCurrentDir)
      RenderncdfCurrent();

	// Debug: log state once
	static bool s_renderLogged = false;
	if (!s_renderLogged) {
	    ncdfLog("[render] CurrentDir=%d CurrentForce=%d IsoLines=%d Numbers=%d Particles=%d\n",
	        (int)plugin->m_bShowCurrentDir, (int)plugin->m_bShowCurrentForce,
	        (int)plugin->m_bShowIsoLines, (int)plugin->m_bShowNumbers, (int)plugin->m_bShowParticles);
	    s_renderLogged = true;
	}

	// New features: draw every frame, generation is cached
	if(plugin->m_bShowIsoLines) {
      static int s_isoCount = 0;
      if (s_isoCount < 3) { ncdfLog("[render] RenderIsoLines: calling, count=%d\n", s_isoCount); s_isoCount++; }
      RenderIsoLines(vp);
	}

	if(plugin->m_bShowNumbers) {
      static int s_numCount = 0;
      if (s_numCount < 3) { ncdfLog("[render] RenderNumbers: calling, count=%d\n", s_numCount); s_numCount++; }
      RenderNumbers(vp);
	}

	if(plugin->m_bShowParticles) {
      // Start timer for particle updates (like GRIB)
      if (!m_tParticleTimer.IsRunning()) {
          m_tParticleTimer.Start(200, wxTIMER_CONTINUOUS);  // Slower updates for performance
      }
      RenderParticles(vp, m_bUpdateParticles);
      m_bUpdateParticles = false;
	} else {
      if (m_tParticleTimer.IsRunning()) m_tParticleTimer.Stop();
      m_Particles.clear();
	}    

    m_last_vp_scale = vp->view_scale_ppm;
    m_last_vp_latMax = vp->lat_max;    
    m_last_vp_latMin = vp->lat_min;
    m_last_vp_lonMax = vp->lon_max;
    m_last_vp_lonMin = vp->lon_min;
  
    return true;
}

void ncdfOverlayFactory::clearBmp()
{
      if(m_pbm_current) {
          delete m_pbm_current;
          m_pbm_current = NULL;
      }
      if(m_pbm_numbers) {
          delete m_pbm_numbers;
          m_pbm_numbers = NULL;
      }
      if(m_pbm_particles) {
          delete m_pbm_particles;
          m_pbm_particles = NULL;
      }

      if(m_bHasGLTexture && m_glTexture != 0) {
          glDeleteTextures(1, &m_glTexture);
          m_glTexture = 0;
          m_bHasGLTexture = false;
      }
      if(m_bHasNumbersTexture && m_glNumbersTexture != 0) {
          glDeleteTextures(1, &m_glNumbersTexture);
          m_glNumbersTexture = 0;
          m_bHasNumbersTexture = false;
      }
      if(m_bHasParticlesTexture && m_glParticlesTexture != 0) {
          glDeleteTextures(1, &m_glParticlesTexture);
          m_glParticlesTexture = 0;
          m_bHasParticlesTexture = false;
      }

      m_Particles.clear();
      m_bIsoLinesDirty = true;
      m_bIsoLinesCached = false;
      m_isoLineVertices.clear();
      m_cachedArrows.clear();
      m_bArrowsDirty = true;

      // Clean up cached magnitude grid
      if (m_magGrid) {
          for (int j = 0; j < m_magGridNj; j++) delete[] m_magGrid[j];
          delete[] m_magGrid;
          m_magGrid = NULL;
          m_magGridNi = 0;
          m_magGridNj = 0;
      }
      // Clean up cached IsoLine objects
      for (size_t k = 0; k < m_isoLineCache.size(); k++) {
          delete (IsoLine*)m_isoLineCache[k];
      }
      m_isoLineCache.clear();
}

void ncdfOverlayFactory::clearDcBmp()
{
      if(m_pbm_current) {
          delete m_pbm_current;
          m_pbm_current = NULL;
      }
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



void ncdfOverlayFactory::RenderncdfCurrent()
{
      if (!gui || !gui->gridu || !gui->gridv) return;

      // Debug: log once
      static bool s_logged = false;
      if (!s_logged) {
          ncdfLog("[arrows] RenderncdfCurrent called, ni=%d nj=%d\n",
              (int)gui->myMessage.lonLength, (int)gui->myMessage.latLength);
          s_logged = true;
      }

      // Arrow settings (like GRIB)
      int arrowWidth = 2;
      int arrowSizeIdx = 0;
      wxColour colour(40, 40, 40);

      // Minimum spacing mode (like GRIB's default)
      double minspace = (double)m_iArrowSpacing;
      double minspace2 = minspace * minspace;

      int ni = gui->myMessage.lonLength;
      int nj = gui->myMessage.latLength;

      double tlat = gui->myMessage.firstGridPointLat;
      double tlon = gui->myMessage.firstGridPointLong;
      double latStep = gui->myMessage.jDirectionIncr;
      double lonStep = gui->myMessage.iDirectionIncr;

#ifdef ocpnUSE_GL
      if (!m_pdc) {
          glEnable(GL_LINE_SMOOTH);
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
          glLineWidth(2);
          glEnableClientState(GL_VERTEX_ARRAY);
          glColor4ub(40, 40, 40, 255);
      }
#endif

      // Iterate data grid with minimum spacing filter (GRIB approach)
      wxPoint firstpx(-1000, -1000);
      wxPoint oldpx(-1000, -1000);
      wxPoint oldpy(-1000, -1000);

      for (int i = 0; i < ni; i++) {
          double lon = tlon + i * lonStep;
          // Quick check: skip columns far from viewport
          wxPoint pl;
          GetCanvasPixLL(vp, &pl, tlat + (nj/2) * latStep, lon);

          if (pl.x <= firstpx.x &&
              square(pl.x - firstpx.x) + square(pl.y - firstpx.y) < minspace2 / 1.44)
              continue;

          if (square(pl.x - oldpx.x) + square(pl.y - oldpx.y) < minspace2)
              continue;

          oldpx = pl;
          if (i == 0) firstpx = pl;

          for (int j = 0; j < nj; j++) {
              double lat = tlat + j * latStep;

              double u = gui->gridu[j][i];
              double v = gui->gridv[j][i];
              if (u == ncdf_NOTDEF || v == ncdf_NOTDEF) continue;

              double mag = sqrt(u * u + v * v);
              if (mag < 0.05) continue;

              wxPoint p;
              GetCanvasPixLL(vp, &p, lat, lon);

              // Minimum spacing check (like GRIB)
              if (square(p.x - oldpy.x) + square(p.y - oldpy.y) < minspace2)
                  continue;
              oldpy = p;

              double dir = atan2(v, -u);
              double scale = wxMax(1.0, mag);

              // Draw arrow
              if (m_pdc) {
                  wxPen pen(colour, arrowWidth);
                  m_pdc->SetPen(pen);
                  m_pdc->SetBrush(*wxTRANSPARENT_BRUSH);
                  drawLineBuffer(m_SingleArrow[arrowSizeIdx], p.x, p.y, dir + vp->rotation, scale);
              } else {
                  // For GL, collect into batch
                  drawLineBuffer(m_SingleArrow[arrowSizeIdx], p.x, p.y, dir + vp->rotation, scale);
              }
          }
      }

#ifdef ocpnUSE_GL
      if (!m_pdc) {
          glColor4f(1, 1, 1, 1);
          glDisableClientState(GL_VERTEX_ARRAY);
          glDisable(GL_LINE_SMOOTH);
          glDisable(GL_BLEND);
      }
#endif
}

void ncdfOverlayFactory::drawLineBuffer(LineBuffer &buffer, int x, int y, double ang, double scale, bool south, bool head)
{
    if (!buffer.lines || buffer.count == 0) return;
    
    float six = sinf(ang), cox = cosf(ang), siy, coy;
    if (south)
        siy = -six, coy = -cox;
    else
        siy = six, coy = cox;
    
    int count = buffer.count;
    if (!head) {
        count -= 2;
    }
    
    float vertexes[40];
    wxASSERT(sizeof vertexes / sizeof *vertexes >= (unsigned)count * 4);
    
    for (int i = 0; i < 2 * count; i++) {
        int j = i;
        if (!head && i > 1) j += 4;
        float *k = buffer.lines + 2 * j;
        vertexes[2 * i + 0] = k[0] * cox * scale + k[1] * siy * scale + x;
        vertexes[2 * i + 1] = k[0] * six * scale - k[1] * coy * scale + y;
    }
    
    if (m_pdc) {
        for (int i = 0; i < count; i++) {
            float *l = vertexes + 4 * i;
            m_pdc->DrawLine(l[0], l[1], l[2], l[3]);
        }
    } else {
#ifdef ocpnUSE_GL
        glVertexPointer(2, GL_FLOAT, 0, vertexes);
        glDrawArrays(GL_LINES, 0, 2 * count);
#endif
    }
}


bool ncdfOverlayFactory::RenderncdfCurrentBmp()
{
    if (g2data.ucurr == NULL || g2data.vcurr == NULL) {
        return false;
    }
    if (!gui || !gui->gridu || !gui->gridv) {
        return false;
    }
    
    if (m_pdc) {
        // Recalculate porg every frame - bitmap position depends on current viewport
        wxPoint porg;
        wxPoint pmin;
        GetCanvasPixLL(vp, &pmin, tlat, tlon);
        wxPoint pmax;
        GetCanvasPixLL(vp, &pmax, blat, blon);

        int width = abs(pmax.x - pmin.x);
        int height = abs(pmax.y - pmin.y);

        if (vp->pix_width < width) width = vp->pix_width;
        if (vp->pix_height < height) height = vp->pix_height;

        // Always set porg - use viewport top when data extends above viewport,
        // otherwise use data top-left corner (matching grib's approach)
        if (vp->lat_max < blat || vp->pix_height < abs(pmax.y - pmin.y)) {
            GetCanvasPixLL(vp, &porg, vp->lat_max, tlon);
        } else {
            GetCanvasPixLL(vp, &porg, tlat, tlon);
        }

        if (m_pbm_current == NULL) {
            if (width > m_ParentSize.GetWidth() || height > m_ParentSize.GetHeight()) {
                return false;
            }

            ::wxBeginBusyCursor();

            wxImage gr_image(width, height);
            gr_image.InitAlpha();

            int ncdf_pixel_size = 4;

            wxPoint p;

            for (int ipix = 0; ipix < (width - ncdf_pixel_size + 1); ipix += ncdf_pixel_size) {
                for (int jpix = 0; jpix < (height - ncdf_pixel_size + 1); jpix += ncdf_pixel_size) {
                    double lat, lon;
                    p.x = ipix + porg.x;
                    p.y = jpix + porg.y;
                    GetCanvasLLPix(vp, p, &lat, &lon);

                    if (!PointInLLBox(vp, lon, lat) && !PointInLLBox(vp, lon - 360.0, lat)) continue;

                    double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, lon, lat, true);
                    double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, lon, lat, true);

                    if ((vx != ncdf_NOTDEF) && (vy != ncdf_NOTDEF)) {
                        double mag = sqrt(vx * vx + vy * vy);
                        unsigned char a = (mag > 0) ? 255 : 0;

                        wxColour c = GetSeaCurrentGraphicColor(mag);
                        unsigned char r = (mag > 0) ? c.Red() : 0;
                        unsigned char g = (mag > 0) ? c.Green() : 0;
                        unsigned char b = (mag > 0) ? c.Blue() : 0;

                        for (int xp = 0; xp < ncdf_pixel_size; xp++)
                            for (int yp = 0; yp < ncdf_pixel_size; yp++) {
                                gr_image.SetRGB(ipix + xp, jpix + yp, r, g, b);
                                gr_image.SetAlpha(ipix + xp, jpix + yp, a);
                            }
                    } else {
                        // Land: black and fully transparent
                        for (int xp = 0; xp < ncdf_pixel_size; xp++)
                            for (int yp = 0; yp < ncdf_pixel_size; yp++) {
                                gr_image.SetRGB(ipix + xp, jpix + yp, 0, 0, 0);
                                gr_image.SetAlpha(ipix + xp, jpix + yp, 0);
                            }
                    }
                }
            }

            // Blur to smooth 4x4 pixel blocks and create gradual boundary transitions.
            // Don't restore original alpha - let blur naturally create soft edges
            // at ocean/land boundaries (matching grib's Blur(4) approach).
            wxImage bl_image = gr_image.Blur(4);

            // Force fully transparent pixels to pure black for clean mask creation
            if (!bl_image.HasAlpha()) {
                bl_image.InitAlpha();
            }
            unsigned char *alphaDst = bl_image.GetAlpha();
            unsigned char *rgbDst = bl_image.GetData();
            int imgW = bl_image.GetWidth();
            int imgH = bl_image.GetHeight();
            for (int i = 0; i < imgW * imgH; i++) {
                if (alphaDst[i] == 0) {
                    rgbDst[i * 3 + 0] = 0;
                    rgbDst[i * 3 + 1] = 0;
                    rgbDst[i * 3 + 2] = 0;
                }
            }

            // Create bitmap with mask (matching grib's approach exactly)
            m_pbm_current = new wxBitmap(bl_image);
            wxMask *gr_mask = new wxMask(*m_pbm_current, wxColour(0, 0, 0));
            m_pbm_current->SetMask(gr_mask);

            ::wxEndBusyCursor();
        }

        if (m_pbm_current) {
            m_pdc->DrawBitmap(*m_pbm_current, porg.x, porg.y, true);
        }
    } else {
#ifdef ocpnUSE_GL
        if (!m_bHasGLTexture) {
            ::wxBeginBusyCursor();

            CreateGLTexture(vp->pix_width, vp->pix_height);

            ::wxEndBusyCursor();
        }

        DrawGLTexture(vp);
#endif
    }

    return true;
}


void ncdfOverlayFactory::drawWaveArrow(int i, int j, double ang, wxColour arrowColor)
{
 double si=sin(ang * PI / 180.),  co=cos(ang * PI / 180.);

 wxPen pen(arrowColor, 1);
 if (m_pdc) {	
	 
		  m_pdc->SetPen(pen);
		  m_pdc->SetBrush(*wxTRANSPARENT_BRUSH);

#if wxUSE_GRAPHICS_CONTEXT
		  if (m_hiDefGraphics && m_gdc)
			  m_gdc->SetPen(pen);
#endif
			}
#ifdef ocpnUSE_GL
	  else
		  glColor3ub(arrowColor.Red(), arrowColor.Green(), arrowColor.Blue());
#endif
      int arrowSize = 26;
      int dec = -arrowSize/2;

      drawTransformedLine(pen, si,co, i,j,  dec,-2,  dec + arrowSize, -2);
	  drawTransformedLine(pen, si, co, i, j, dec, 2, dec + arrowSize, +2);

	  drawTransformedLine(pen, si, co, i, j, dec - 2, 0, dec + 5, 6);    // flèche
	  drawTransformedLine(pen, si, co, i, j, dec - 2, 0, dec + 5, -6);   // flèche

}

void ncdfOverlayFactory::drawTransformedLine( wxPen pen, double si, double co,int di, int dj, int i,int j, int k,int l)
{
      int ii, jj, kk, ll;
      ii = (int) (i*co-j*si +0.5) + di;
      jj = (int) (i*si+j*co +0.5) + dj;
      kk = (int) (k*co-l*si +0.5) + di;
      ll = (int) (k*si+l*co +0.5) + dj;

	  wxColor colour;
	  GetGlobalColor(_T("UBLCK"), &colour);

	  if (m_pdc){
		  m_pdc->SetPen(pen);
		  m_pdc->SetBrush(*wxTRANSPARENT_BRUSH);
		  m_pdc->DrawLine(ii, jj, kk, ll);

	  }
	  else{
		  DrawGLLine(ii, jj, kk, ll, 0.5, colour);
	  }
}

void ncdfOverlayFactory::DrawGLLine(double x1, double y1, double x2, double y2, double width, wxColour myColour)
{
    wxColour isoLineColor = myColour;
    glColor4ub(isoLineColor.Red(), isoLineColor.Green(), isoLineColor.Blue(), 255);
    glLineWidth(width);
    glBegin(GL_LINES);
    glVertex2d(x1, y1);
    glVertex2d(x2, y2);
    glEnd();
}

void ncdfOverlayFactory::DrawOLBitmap(const wxBitmap &bitmap, wxCoord x, wxCoord y, bool usemask)
{
	wxBitmap bmp;
	wxCoord draw_x = x;
	wxCoord draw_y = y;
	
	if (x < 0 || y < 0) {
		int dx = (x < 0 ? -x : 0);
		int dy = (y < 0 ? -y : 0);
		int w = bitmap.GetWidth() - dx;
		int h = bitmap.GetHeight() - dy;
		if (w <= 0 || h <= 0) return;
		wxBitmap newBitmap = bitmap.GetSubBitmap(wxRect(dx, dy, w, h));
		draw_x += dx;
		draw_y += dy;
		bmp = newBitmap;
	}
	else {
		bmp = bitmap;
	}
	
	if (m_pdc) {
		m_pdc->DrawBitmap(bmp, draw_x, draw_y, usemask);
	}
	else {
		if (!m_bHasGLTexture) {
			wxImage image = bmp.ConvertToImage();
			int w = image.GetWidth(), h = image.GetHeight();

			unsigned char *d = image.GetData();
			unsigned char *a = image.GetAlpha();

			unsigned char mr, mg, mb;
			bool hasMask = image.GetOrFindMaskColour(&mr, &mg, &mb);

			unsigned char *rgbaData = new unsigned char[4 * w * h];
			for (int yy = 0; yy < h; yy++) {
				for (int xx = 0; xx < w; xx++) {
					int off = (yy * image.GetWidth() + xx);
					unsigned char r = d[off * 3 + 0];
					unsigned char g = d[off * 3 + 1];
					unsigned char b = d[off * 3 + 2];

					rgbaData[off * 4 + 0] = r;
					rgbaData[off * 4 + 1] = g;
					rgbaData[off * 4 + 2] = b;

					if (a) {
						rgbaData[off * 4 + 3] = a[off];
					} else if (hasMask && r == mr && g == mg && b == mb) {
						rgbaData[off * 4 + 3] = 0;
					} else {
						rgbaData[off * 4 + 3] = 255;
					}
				}
			}

			glGenTextures(1, &m_glTexture);
			glBindTexture(GL_TEXTURE_2D, m_glTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
			
			delete[] rgbaData;
			m_bHasGLTexture = true;
		}

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBindTexture(GL_TEXTURE_2D, m_glTexture);

		glColor4f(1, 1, 1, 1);

		int w = bmp.GetWidth();
		int h = bmp.GetHeight();

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f); glVertex2i(draw_x, draw_y);
		glTexCoord2f(1.0f, 1.0f); glVertex2i(draw_x + w, draw_y);
		glTexCoord2f(1.0f, 0.0f); glVertex2i(draw_x + w, draw_y + h);
		glTexCoord2f(0.0f, 0.0f); glVertex2i(draw_x, draw_y + h);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);
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

	// Use a local wxMemoryDC to avoid NULL m_pdc dereference in downstream functions
	wxMemoryDC localDC(wxNullBitmap);
	m_pdc = &localDC;

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

// Is the given point in the vp ??
bool PointInLLBox(PlugIn_ViewPort *vp, double x, double y)
{


    if (  x >= (vp->lon_min) && x <= (vp->lon_max) &&
            y >= (vp->lat_min) && y <= (vp->lat_max) )
            return TRUE;
    return FALSE;
}



struct ColorMap {
    double val;
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

// Ocean current color map (values in m/s)
// Blue (calm) → Cyan → Green → Yellow → Orange → Red (strong)
static ColorMap CurrentMap[] = {
    {0,     0x00, 0x00, 0xff},  // 0 - deep blue (calm)
    {0.1,   0x00, 0x40, 0xff},  // 0.1 - blue
    {0.2,   0x00, 0x80, 0xff},  // 0.2 - medium blue
    {0.3,   0x00, 0xbf, 0xff},  // 0.3 - light blue
    {0.4,   0x00, 0xff, 0xff},  // 0.4 - cyan
    {0.5,   0x00, 0xff, 0x80},  // 0.5 - cyan-green
    {0.62,  0x00, 0xff, 0x00},  // 0.62 - green (1.2 knots)
    {0.8,   0x80, 0xff, 0x00},  // 0.8 - yellow-green
    {1.0,   0xff, 0xff, 0x00},  // 1.0 - yellow
    {1.2,   0xff, 0xc0, 0x00},  // 1.2 - orange-yellow
    {1.54,  0xff, 0x80, 0x00},  // 1.54 - orange (3.0 knots)
    {1.8,   0xff, 0x40, 0x00},  // 1.8 - deep orange
    {2.0,   0xff, 0x00, 0x00},  // 2.0 - red
    {2.5,   0xe0, 0x00, 0x20},  // 2.5 - red-purple
    {3.0,   0xc0, 0x00, 0x40},  // 3.0 - purple
    {4.0,   0xa0, 0x00, 0x80},  // 4.0 - deep purple
    {5.0,   0xe0, 0xe0, 0xff}}; // 5.0+ - purple-white (extreme)

static ColorMap WindMap[] = {
    {0, 0x28, 0x8c, 0xff},  {3, 0x00, 0xaf, 0xff},  {6, 0x00, 0xdc, 0xe1},
    {9, 0x00, 0xf7, 0xb0},  {12, 0x00, 0xea, 0x9c}, {15, 0x82, 0xf0, 0x59},
    {18, 0xf0, 0xf5, 0x03}, {21, 0xff, 0xed, 0x00}, {24, 0xff, 0xdb, 0x00},
    {27, 0xff, 0xc7, 0x00}, {30, 0xff, 0xb4, 0x00}, {33, 0xff, 0x98, 0x00},
    {36, 0xff, 0x7e, 0x00}, {39, 0xf7, 0x78, 0x00}, {42, 0xec, 0x78, 0x14},
    {45, 0xe4, 0x71, 0x1e}, {48, 0xe0, 0x61, 0x28}, {51, 0xdc, 0x51, 0x32},
    {54, 0xd5, 0x45, 0x3c}, {57, 0xcd, 0x3a, 0x46}, {60, 0xbe, 0x2c, 0x50},
    {63, 0xb4, 0x1a, 0x5a}, {66, 0xaa, 0x14, 0x64}, {70, 0x96, 0x28, 0x78},
    {75, 0x8c, 0x32, 0x8c}};

static ColorMap GenericMap[] = {
    {0, 0x00, 0xd9, 0x00},  {1, 0x2a, 0xd9, 0x00},  {2, 0x6e, 0xd9, 0x00},
    {3, 0xb2, 0xd9, 0x00},  {4, 0xd4, 0xd4, 0x00},  {5, 0xd9, 0xa6, 0x00},
    {7, 0xd9, 0x00, 0x00},  {9, 0xd9, 0x00, 0x40},  {12, 0xd9, 0x00, 0x60},
    {15, 0xae, 0x00, 0x80}, {18, 0x83, 0x00, 0xa0}, {21, 0x57, 0x00, 0xc0},
    {24, 0x00, 0x00, 0xd0}, {27, 0x04, 0x00, 0xe0}, {30, 0x08, 0x00, 0xe0},
    {36, 0xa0, 0x00, 0xe0}, {42, 0xc0, 0x04, 0xc0}, {48, 0xc0, 0x08, 0xa0},
    {56, 0xc0, 0xa0, 0x08}};

static ColorMap SeaTempMap[] = {
    {-2, 0xcc, 0x04, 0xae}, {2, 0x8f, 0x06, 0xe4},  {6, 0x48, 0x6a, 0xfa},
    {10, 0x00, 0xff, 0xff}, {15, 0x00, 0xd5, 0x4b}, {19, 0x59, 0xd8, 0x00},
    {23, 0xf2, 0xfc, 0x00}, {27, 0xff, 0x15, 0x00}, {32, 0xff, 0x00, 0x00},
    {36, 0xd8, 0x00, 0x00}, {40, 0xa9, 0x00, 0x00}, {44, 0x87, 0x00, 0x00},
    {48, 0x69, 0x00, 0x00}, {52, 0x55, 0x00, 0x00}, {56, 0x33, 0x00, 0x00}};

static const int CurrentMapSize = sizeof(CurrentMap) / sizeof(CurrentMap[0]);
static const int WindMapSize = sizeof(WindMap) / sizeof(WindMap[0]);
static const int GenericMapSize = sizeof(GenericMap) / sizeof(GenericMap[0]);
static const int SeaTempMapSize = sizeof(SeaTempMap) / sizeof(SeaTempMap[0]);

static int findColorMapIndex(double val, ColorMap *map, int mapSize) {
    if (val <= map[0].val) return 0;
    if (val >= map[mapSize - 1].val) return mapSize - 1;
    
    int low = 0, high = mapSize - 1;
    while (high - low > 1) {
        int mid = (low + high) / 2;
        if (map[mid].val <= val) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return high;
}

static int findColorMapIndex(double val) {
    return findColorMapIndex(val, CurrentMap, CurrentMapSize);
}

wxColour ncdfOverlayFactory::GetSeaCurrentGraphicColor(double val_in)
{
    double val = wxMax(val_in, 0.0);
    
    ColorMap *map = CurrentMap;
    int mapSize = CurrentMapSize;
    
    switch (m_colorMapType) {
        case COLOR_MAP_WIND:
            map = WindMap;
            mapSize = WindMapSize;
            break;
        case COLOR_MAP_GENERIC:
            map = GenericMap;
            mapSize = GenericMapSize;
            break;
        case COLOR_MAP_SEATEMP:
            map = SeaTempMap;
            mapSize = SeaTempMapSize;
            break;
        case COLOR_MAP_CURRENT:
        default:
            map = CurrentMap;
            mapSize = CurrentMapSize;
            break;
    }
    
    // Handle values below minimum
    if (val <= map[0].val) {
        return wxColour(map[0].r, map[0].g, map[0].b);
    }
    
    double cmax = map[mapSize - 1].val;
    
    if (val >= cmax) {
        ColorMap &m = map[mapSize - 1];
        return wxColour(m.r, m.g, m.b);
    }
    
    int idx = findColorMapIndex(val, map, mapSize);
    double vala = map[idx - 1].val;
    double valb = map[idx].val;
    
    double d = 0.0;
    if (valb - vala > 1e-10) {
        d = (val - vala) / (valb - vala);
    }
    unsigned char r = (1 - d) * map[idx - 1].r + d * map[idx].r;
    unsigned char g = (1 - d) * map[idx - 1].g + d * map[idx].g;
    unsigned char b = (1 - d) * map[idx - 1].b + d * map[idx].b;
    
    return wxColour(r, g, b);
}

void ncdfOverlayFactory::GetSeaCurrentGraphicColorRaw(double val_in, unsigned char *rgba)
{
    double val = wxMax(val_in, 0.0);
    
    ColorMap *map = CurrentMap;
    int mapSize = CurrentMapSize;
    
    switch (m_colorMapType) {
        case COLOR_MAP_WIND:
            map = WindMap;
            mapSize = WindMapSize;
            break;
        case COLOR_MAP_GENERIC:
            map = GenericMap;
            mapSize = GenericMapSize;
            break;
        case COLOR_MAP_SEATEMP:
            map = SeaTempMap;
            mapSize = SeaTempMapSize;
            break;
        case COLOR_MAP_CURRENT:
        default:
            map = CurrentMap;
            mapSize = CurrentMapSize;
            break;
    }
    
    // Handle values below minimum
    if (val <= map[0].val) {
        rgba[0] = map[0].r;
        rgba[1] = map[0].g;
        rgba[2] = map[0].b;
        rgba[3] = 255;
        return;
    }
    
    double cmax = map[mapSize - 1].val;
    
    if (val >= cmax) {
        ColorMap &m = map[mapSize - 1];
        rgba[0] = m.r;
        rgba[1] = m.g;
        rgba[2] = m.b;
        rgba[3] = 220;
        return;
    }
    
    int idx = findColorMapIndex(val, map, mapSize);
    double vala = map[idx - 1].val;
    double valb = map[idx].val;
    
    double d = 0.0;
    if (valb - vala > 1e-10) {
        d = (val - vala) / (valb - vala);
    }
    rgba[0] = (1 - d) * map[idx - 1].r + d * map[idx].r;
    rgba[1] = (1 - d) * map[idx - 1].g + d * map[idx].g;
    rgba[2] = (1 - d) * map[idx - 1].b + d * map[idx].b;
    rgba[3] = 220;
}

bool ncdfOverlayFactory::CreateGLTexture(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    
    if (ni <= 0 || nj <= 0) return false;
    
    bool repeat = false;
    if (gui->myMessage.lonValues && ni >= 2) {
        repeat = (gui->myMessage.lonValues[0] == 0 && 
                   gui->myMessage.lonValues[ni - 1] + fabs(gui->myMessage.lonValues[1] - gui->myMessage.lonValues[0]) >= 360.);
    }
    
    int tw, th, samples = 1;
    double delta = 0;
    
    if (ni > 1024 || nj > 1024) {
        samples = 0;
        tw = ni;
        th = nj;
        double dw = (tw > 1022) ? 1022. / tw : 1.;
        double dh = (th > 1022) ? 1022. / th : 1.;
        delta = wxMin(dw, dh);
        th = (int)(th * delta);
        tw = (int)(tw * delta);
        tw += 2 * !repeat;
        th += 2;
    } else {
        for (;;) {
            tw = samples * (ni - 1) + 1 + 2 * !repeat;
            th = samples * (nj - 1) + 1 + 2;
            if (tw >= 512 || th >= 512 || samples == 16) break;
            samples *= 2;
        }
    }
    
    if (tw > 1024 || th > 1024) return false;
    
    m_texDataDim[0] = tw;
    m_texDataDim[1] = th;
    
    int pot_tw = next_power_of_two(tw);
    int pot_th = next_power_of_two(th);
    
    unsigned char *data = new unsigned char[pot_tw * pot_th * 4];
    memset(data, 0, pot_tw * pot_th * 4);
    
    if (samples == 0) {
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                int idx = j * ni + i;
                double vx = gui->myMessage.ucurr[idx];
                double vy = gui->myMessage.vcurr[idx];
                
                int y = (j + 1) * delta;
                int x = (i + !repeat) * delta;
                int doff = 4 * (y * pot_tw + x);
                
                if (vx == ncdf_NOTDEF || vy == ncdf_NOTDEF) {
                    data[doff] = 255;
                    data[doff + 1] = 255;
                    data[doff + 2] = 255;
                    data[doff + 3] = 0;
                } else {
                    double mag = sqrt(vx * vx + vy * vy);
                    GetSeaCurrentGraphicColorRaw(mag, data + doff);
                    if (mag == 0) {
                        data[doff + 3] = 0;
                    }
                }
            }
        }
    } else if (samples == 1) {
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                int idx = j * ni + i;
                double vx = gui->myMessage.ucurr[idx];
                double vy = gui->myMessage.vcurr[idx];
                
                int y = j + 1;
                int x = i + !repeat;
                int doff = 4 * (y * pot_tw + x);
                
                if (vx == ncdf_NOTDEF || vy == ncdf_NOTDEF) {
                    data[doff] = 255;
                    data[doff + 1] = 255;
                    data[doff + 2] = 255;
                    data[doff + 3] = 0;
                } else {
                    double mag = sqrt(vx * vx + vy * vy);
                    GetSeaCurrentGraphicColorRaw(mag, data + doff);
                    if (mag == 0) {
                        data[doff + 3] = 0;
                    }
                }
            }
        }
    } else {
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                int idx = j * ni + i;
                double v00 = gui->myMessage.ucurr[idx], v01 = ncdf_NOTDEF;
                double v10 = ncdf_NOTDEF, v11 = ncdf_NOTDEF;
                
                double u00 = gui->myMessage.vcurr[idx], u01 = ncdf_NOTDEF;
                double u10 = ncdf_NOTDEF, u11 = ncdf_NOTDEF;
                
                if (i < ni - 1) {
                    int idx1 = j * ni + i + 1;
                    v01 = gui->myMessage.ucurr[idx1];
                    u01 = gui->myMessage.vcurr[idx1];
                    if (j < nj - 1) {
                        int idx2 = (j + 1) * ni + i + 1;
                        v11 = gui->myMessage.ucurr[idx2];
                        u11 = gui->myMessage.vcurr[idx2];
                    }
                }
                if (j < nj - 1) {
                    int idx3 = (j + 1) * ni + i;
                    v10 = gui->myMessage.ucurr[idx3];
                    u10 = gui->myMessage.vcurr[idx3];
                }
                
                for (int ys = 0; ys < samples; ys++) {
                    int y = j * samples + ys + 1;
                    double yd = (double)ys / samples;
                    double vx0, vx1;
                    double vy0, vy1;
                    double a0 = 1, a1 = 1;
                    
                    if (v10 == ncdf_NOTDEF) {
                        vx0 = v00;
                        vy0 = u00;
                        if (v00 == ncdf_NOTDEF)
                            a0 = 0;
                        else
                            a0 = 1 - yd;
                    } else if (v00 == ncdf_NOTDEF) {
                        vx0 = v10;
                        vy0 = u10;
                        a0 = yd;
                    } else {
                        vx0 = (1 - yd) * v00 + yd * v10;
                        vy0 = (1 - yd) * u00 + yd * u10;
                    }
                    
                    if (v11 == ncdf_NOTDEF) {
                        vx1 = v01;
                        vy1 = u01;
                        if (v01 == ncdf_NOTDEF)
                            a1 = 0;
                        else
                            a1 = 1 - yd;
                    } else if (v01 == ncdf_NOTDEF) {
                        vx1 = v11;
                        vy1 = u11;
                        a1 = yd;
                    } else {
                        vx1 = (1 - yd) * v01 + yd * v11;
                        vy1 = (1 - yd) * u01 + yd * u11;
                    }
                    
                    for (int xs = 0; xs < samples; xs++) {
                        int x = i * samples + xs + !repeat;
                        double xd = (double)xs / samples;
                        double vx, vy, a;
                        
                        if (vx1 == ncdf_NOTDEF) {
                            vx = vx0;
                            vy = vy0;
                            a = (1 - xd) * a0;
                        } else if (vx0 == ncdf_NOTDEF) {
                            vx = vx1;
                            vy = vy1;
                            a = xd * a1;
                        } else {
                            vx = (1 - xd) * vx0 + xd * vx1;
                            vy = (1 - xd) * vy0 + xd * vy1;
                            a = (1 - xd) * a0 + xd * a1;
                        }
                        
                        int doff = 4 * (y * pot_tw + x);
                        
                        if (vx == ncdf_NOTDEF || vy == ncdf_NOTDEF) {
                            data[doff] = 255;
                            data[doff + 1] = 255;
                            data[doff + 2] = 255;
                            data[doff + 3] = 0;
                        } else {
                            double mag = sqrt(vx * vx + vy * vy);
                            GetSeaCurrentGraphicColorRaw(mag, data + doff);
                            data[doff + 3] *= a;
                            if (mag == 0) {
                                data[doff + 3] = 0;
                            }
                        }
                        
                        if (i == ni - 1) break;
                    }
                    if (j == nj - 1) break;
                }
            }
        }
    }
    
    memcpy(data, data + 4 * pot_tw * 1, 4 * pot_tw);
    memcpy(data + 4 * pot_tw * (th - 1), data + 4 * pot_tw * (th - 2), 4 * pot_tw);
    for (int x = 0; x < pot_tw; x++) {
        int doff = 4 * x;
        data[doff + 3] = 0;
        doff = 4 * ((th - 1) * pot_tw + x);
        data[doff + 3] = 0;
    }
    
    if (!repeat) {
        for (int y = 0; y < th; y++) {
            int doff = 4 * y * pot_tw, soff = doff + 4;
            memcpy(data + doff, data + soff, 4);
            data[doff + 3] = 0;
            doff = 4 * (y * pot_tw + tw - 1), soff = doff - 4;
            memcpy(data + doff, data + soff, 4);
            data[doff + 3] = 0;
        }
    }
    
    if (m_bHasGLTexture && m_glTexture != 0) {
        glDeleteTextures(1, &m_glTexture);
    }
    
    glGenTextures(1, &m_glTexture);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pot_tw);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pot_tw, pot_th, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    glPopClientAttrib();
    
    delete[] data;
    
    m_bHasGLTexture = true;
    m_texDim[0] = pot_tw;
    m_texDim[1] = pot_th;
    
    return true;
}

void ncdfOverlayFactory::DrawGLTexture(PlugIn_ViewPort *vp)
{
    if (!m_bHasGLTexture || m_glTexture == 0) return;
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float alpha = m_iOverlayTransparency / 100.0f;
    glColor4f(1, 1, 1, alpha);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    
    if (ni <= 0 || nj <= 0) return;
    
    double lat_min = gui->myMessage.firstGridPointLat;
    double lat_max = gui->myMessage.lastGridPointLat;
    double lon_min = gui->myMessage.firstGridPointLong;
    double lon_max = gui->myMessage.lastGridPointLong;
    
    ncdfLog("[DrawGL] lat=(%.1f,%.1f) lon=(%.1f,%.1f) ni=%d nj=%d tex=%d\n",
        lat_min, lat_max, lon_min, lon_max, ni, nj, m_glTexture);
    
    bool repeat = false;
    if (gui->myMessage.lonValues && ni >= 2) {
        repeat = (lon_min == 0 && lon_max + fabs(gui->myMessage.lonValues[1] - gui->myMessage.lonValues[0]) >= 360.);
    }
    
    double pw = vp->view_scale_ppm * 1e6 / (pow(2, fabs(vp->clat) / 25));
    if (pw < 20) pw = 20;
    
    int xsquares = ceil(vp->pix_width / pw);
    int ysquares = ceil(vp->pix_height / pw);
    
    if (vp->rotation == 0 && vp->m_projection_type == PI_PROJECTION_MERCATOR)
        xsquares = 1;
    
    xsquares = wxMax(xsquares, 2);
    ysquares = wxMax(ysquares, 2);
    
    double xs = vp->pix_width / double(xsquares);
    double ys = vp->pix_height / double(ysquares);
    
    int tw = m_texDim[0];
    int th = m_texDim[1];
    
    // Match GRIB's latstep/lonstep calculation exactly
    // GRIB: latstep = fabs(Dj) / (th - 2 - 1) * (Nj - 1)
    // GRIB: lonstep = Di / (tw - 2*!repeat - 1) * (Ni - 1)
    double latstep = fabs(gui->myMessage.jDirectionIncr) / (th - 2 - 1) * (nj - 1);
    double lonstep = gui->myMessage.iDirectionIncr / (tw - 2 * !repeat - 1) * (ni - 1);
    
    double potNormX = (double)m_texDataDim[0] / tw;
    double potNormY = (double)m_texDataDim[1] / th;
    
    double clon = (lon_min + lon_max) / 2;
    
    typedef double mx[2][2];
    mx *lva = new mx[xsquares + 1];
    
    int j = 0;
    
    for (double y = 0; y < vp->pix_height + ys / 2; y += ys) {
        int i = 0;
        
        for (double x = 0; x < vp->pix_width + xs / 2; x += xs) {
            double lat, lon;
            wxPoint p(x, y);
            GetCanvasLLPix(vp, p, &lat, &lon);
            
            if (!repeat) {
                if (clon - lon > 180) lon += 360;
                else if (lon - clon > 180) lon -= 360;
            }
            
            lva[i][j][0] = (((lon - lon_min) / lonstep - repeat + 1.5) / tw) * potNormX;
            lva[i][j][1] = (((lat - lat_min) / latstep + 1.5) / th) * potNormY;
            
            if (x > 0 && y > 0) {
                double u0 = lva[i - 1][!j][0], v0 = lva[i - 1][!j][1];
                double u1 = lva[i][!j][0], v1 = lva[i][!j][1];
                double u2 = lva[i][j][0], v2 = lva[i][j][1];
                double u3 = lva[i - 1][j][0], v3 = lva[i - 1][j][1];
                
                if (repeat) {
                    if (u1 - u0 > .5) u1--;
                    else if (u0 - u1 > .5) u1++;
                    if (u2 - u0 > .5) u2--;
                    else if (u0 - u2 > .5) u2++;
                    if (u3 - u0 > .5) u3--;
                    else if (u0 - u3 > .5) u3++;
                }
                
                if ((repeat ||
                     ((u0 >= 0 || u1 >= 0 || u2 >= 0 || u3 >= 0) &&
                      (u0 <= 1 || u1 <= 1 || u2 <= 1 || u3 <= 1))) &&
                    (v0 >= 0 || v1 >= 0 || v2 >= 0 || v3 >= 0) &&
                    (v0 <= 1 || v1 <= 1 || v2 <= 1 || v3 <= 1)) {
                    // Prevent drawing quads with reversed texture coords
                    // (happens when longitude wraps, causing duplicate areas)
                    if (u1 > u0) {
                        glBegin(GL_QUADS);
                        glTexCoord2d(u3, v3); glVertex2f(x - xs, y);
                        glTexCoord2d(u2, v2); glVertex2f(x, y);
                        glTexCoord2d(u1, v1); glVertex2f(x, y - ys);
                        glTexCoord2d(u0, v0); glVertex2f(x - xs, y - ys);
                        glEnd();
                    }
                }
            }
            
            i++;
        }
        j = !j;
    }
    
    delete[] lva;
    
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}


//===================================================================
// RenderIsoLines - Contour lines for current magnitude
// Following GRIB pattern: generate IsoLine objects once, draw from cache
//===================================================================
void ncdfOverlayFactory::RenderIsoLines(PlugIn_ViewPort *vp)
{
    if (!gui || !gui->gridu || !gui->gridv) return;

    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni <= 1 || nj <= 1) return;

    // Generate isoline objects once (cached), like GRIB's pIsobarArray
    if (m_bIsoLinesDirty || m_isoLineCache.empty()) {
        for (size_t k = 0; k < m_isoLineCache.size(); k++)
            delete (IsoLine*)m_isoLineCache[k];
        m_isoLineCache.clear();

        int step = 1;
        if (ni > 200 || nj > 200) step = 4;
        else if (ni > 100 || nj > 100) step = 2;

        int sni = (ni - 1) / step + 1;
        int snj = (nj - 1) / step + 1;
        double tlat = gui->myMessage.firstGridPointLat;
        double tlon = gui->myMessage.firstGridPointLong;
        double incrLat = gui->myMessage.jDirectionIncr * step;
        double incrLon = gui->myMessage.iDirectionIncr * step;

        double **magGrid = new double*[snj];
        double maxMag = 0;
        for (int j = 0; j < snj; j++) {
            magGrid[j] = new double[sni];
            for (int i = 0; i < sni; i++) {
                int oj = j * step, oi = i * step;
                if (oj >= nj) oj = nj - 1;
                if (oi >= ni) oi = ni - 1;
                double u = gui->gridu[oj][oi], v = gui->gridv[oj][oi];
                if (u != ncdf_NOTDEF && v != ncdf_NOTDEF) {
                    magGrid[j][i] = sqrt(u * u + v * v);
                    if (magGrid[j][i] > maxMag) maxMag = magGrid[j][i];
                } else {
                    magGrid[j][i] = ncdf_NOTDEF;
                }
            }
        }

        if (maxMag < 0.01) {
            for (int j = 0; j < snj; j++) delete[] magGrid[j];
            delete[] magGrid;
            m_bIsoLinesDirty = false;
            return;
        }

        double spacing = 0.5;
        if (step > 1) spacing = 1.0;

        for (double val = spacing; val <= maxMag; val += spacing) {
            IsoLine *piso = new IsoLine(val, magGrid, snj, sni, tlat, tlon, incrLat, incrLon);
            if (piso->getNbSegments() > 0)
                m_isoLineCache.push_back(piso);
            else
                delete piso;
        }

        if (m_magGrid) {
            for (int j = 0; j < m_magGridNj; j++) delete[] m_magGrid[j];
            delete[] m_magGrid;
        }
        m_magGrid = magGrid;
        m_magGridNi = sni; m_magGridNj = snj;
        m_magGridStep = step; m_magGridMax = maxMag;
        m_magGridTlat = tlat; m_magGridTlon = tlon;
        m_magGridIncrLat = incrLat; m_magGridIncrLon = incrLon;
        m_bIsoLinesDirty = false;
    }

    if (m_isoLineCache.empty()) return;

    // Draw cached isolines every frame (like GRIB's draw loop)
    for (size_t k = 0; k < m_isoLineCache.size(); k++) {
        IsoLine *piso = (IsoLine*)m_isoLineCache[k];

        if (m_pdc) {
            // DC mode: draw isoline + labels (like GRIB)
            piso->drawIsoLine(*m_pdc, vp, false, false);
        }
#ifdef ocpnUSE_GL
        else {
            // GL mode: draw isoline segments + labels
            // Draw segments via cached magnitude grid marching squares
            if (!m_magGrid || m_magGridNi == 0) continue;

            double val = piso->getValue();
            unsigned char rgba[4];
            GetSeaCurrentGraphicColorRaw(val, rgba);

            glEnable(GL_LINE_SMOOTH);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(1);
            glColor4ub(rgba[0], rgba[1], rgba[2], 200);

            int sni = m_magGridNi, snj = m_magGridNj;
            double tlat = m_magGridTlat, tlon = m_magGridTlon;
            double ilat = m_magGridIncrLat, ilon = m_magGridIncrLon;

            glBegin(GL_LINES);
            for (int j = 0; j < snj - 1; j++) {
                for (int i = 0; i < sni - 1; i++) {
                    double v00 = m_magGrid[j][i], v10 = m_magGrid[j][i+1];
                    double v01 = m_magGrid[j+1][i], v11 = m_magGrid[j+1][i+1];
                    if (v00 == ncdf_NOTDEF || v10 == ncdf_NOTDEF ||
                        v01 == ncdf_NOTDEF || v11 == ncdf_NOTDEF) continue;
                    int c = 0;
                    if (v00 >= val) c |= 1; if (v10 >= val) c |= 2;
                    if (v11 >= val) c |= 4; if (v01 >= val) c |= 8;
                    if (c == 0 || c == 15) continue;
                    double lat0 = tlat + j * ilat, lon0 = tlon + i * ilon;
                    double lat1 = lat0 + ilat, lon1 = lon0 + ilon;
                    auto lerp = [](double a, double b, double va, double vb, double v) {
                        return a + (v - va) / (vb - va) * (b - a);
                    };
                    double et = lerp(lon0, lon1, v00, v10, val);
                    double eb = lerp(lon0, lon1, v01, v11, val);
                    double el = lerp(lat0, lat1, v00, v01, val);
                    double er = lerp(lat0, lat1, v10, v11, val);
                    auto emit = [&](double la, double lo, double lb, double lbo) {
                        wxPoint pA, pB;
                        GetCanvasPixLL(vp, &pA, la, lo);
                        GetCanvasPixLL(vp, &pB, lb, lbo);
                        glVertex2i(pA.x, pA.y); glVertex2i(pB.x, pB.y);
                    };
                    switch (c) {
                        case 1:  emit(lat0, et, el, lon0); break;
                        case 2:  emit(lat0, et, er, lon1); break;
                        case 3:  emit(el, lon0, er, lon1); break;
                        case 4:  emit(er, lon1, lat1, eb); break;
                        case 5:  emit(lat0, et, er, lon1); emit(el, lon0, lat1, eb); break;
                        case 6:  emit(lat0, et, lat1, eb); break;
                        case 7:  emit(el, lon0, lat1, eb); break;
                        case 8:  emit(el, lon0, lat1, eb); break;
                        case 9:  emit(lat0, et, lat1, eb); break;
                        case 10: emit(lat0, et, el, lon0); emit(er, lon1, lat1, eb); break;
                        case 11: emit(er, lon1, lat1, eb); break;
                        case 12: emit(el, lon0, er, lon1); break;
                        case 13: emit(lat0, et, er, lon1); break;
                        case 14: emit(lat0, et, el, lon0); break;
                    }
                }
            }
            glEnd();
            glDisable(GL_LINE_SMOOTH);

            // Draw label for this isoline (like GRIB's drawIsoLineLabelsGL)
            // Place label at first visible crossing
            wxString labelText;
            labelText.Printf(_T("%.1f"), val);
            wxColour backColor(rgba[0], rgba[1], rgba[2]);
            DrawIsoLabel(vp, labelText, backColor, val, sni, snj, tlat, tlon, ilat, ilon);

            glDisable(GL_BLEND);
        }
#endif
    }
}


//===================================================================
// DrawIsoLine helper - render a single label at first crossing point
//===================================================================
void ncdfOverlayFactory::DrawIsoLabel(PlugIn_ViewPort *vp, wxString &label,
    wxColour &color, double val, int sni, int snj,
    double tlat, double tlon, double ilat, double ilon)
{
    if (!m_magGrid) return;

    for (int j = 0; j < snj - 1; j++) {
        for (int i = 0; i < sni - 1; i++) {
            double v00 = m_magGrid[j][i], v10 = m_magGrid[j][i+1];
            double v01 = m_magGrid[j+1][i], v11 = m_magGrid[j+1][i+1];
            if (v00 == ncdf_NOTDEF || v10 == ncdf_NOTDEF ||
                v01 == ncdf_NOTDEF || v11 == ncdf_NOTDEF) continue;
            int c = 0;
            if (v00 >= val) c |= 1; if (v10 >= val) c |= 2;
            if (v11 >= val) c |= 4; if (v01 >= val) c |= 8;
            if (c == 0 || c == 15) continue;

            double lat0 = tlat + j * ilat, lon0 = tlon + i * ilon;
            double lat1 = lat0 + ilat, lon1 = lon0 + ilon;
            auto lerp2 = [](double a, double b, double va, double vb, double v) {
                if (fabs(vb - va) < 1e-10) return (a + b) / 2.0;
                return a + (v - va) / (vb - va) * (b - a);
            };
            double crossLat = lerp2(lat0, lat1, v00, v01, val);
            double crossLon = lerp2(lon0, lon1, v00, v10, val);

            wxPoint pLabel;
            GetCanvasPixLL(vp, &pLabel, crossLat, crossLon);

            // Render label with colored background (like GRIB's DrawNumbers)
            wxMemoryDC mdc(wxNullBitmap);
            wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
            mdc.SetFont(font);
            int tw, th;
            mdc.GetTextExtent(label, &tw, &th);
            int pad = 4;
            int bw = tw + pad * 2, bh = th + pad * 2;
            wxBitmap bm(bw, bh);
            mdc.SelectObject(bm);
            mdc.SetBackground(wxBrush(color));
            mdc.Clear();
            mdc.SetTextForeground(*wxWHITE);
            mdc.DrawText(label, pad, pad);
            mdc.SelectObject(wxNullBitmap);

            wxImage img = bm.ConvertToImage();
            unsigned char *d = img.GetData();
            unsigned char *e = new unsigned char[4 * bw * bh];
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++) {
                    int off = y * bw + x;
                    e[off * 4 + 0] = d[off * 3 + 0];
                    e[off * 4 + 1] = d[off * 3 + 1];
                    e[off * 4 + 2] = d[off * 3 + 2];
                    e[off * 4 + 3] = 220;
                }
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glRasterPos2i(pLabel.x - bw / 2, pLabel.y + bh / 2);
            glPixelZoom(1, -1);
            glDrawPixels(bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, e);
            glPixelZoom(1, 1);
            glDisable(GL_BLEND);
            delete[] e;
            return;  // One label per isoline value
        }
    }
}


//===================================================================
// RenderNumbers - Display numeric current values
// Following GRIB Mode B: iterate data grid with minimum spacing
//===================================================================
void ncdfOverlayFactory::RenderNumbers(PlugIn_ViewPort *vp)
{
    if (!gui || !gui->gridu || !gui->gridv) return;

    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni <= 0 || nj <= 0) return;

    double tlat = gui->myMessage.firstGridPointLat;
    double tlon = gui->myMessage.firstGridPointLong;
    double latStep = gui->myMessage.jDirectionIncr;
    double lonStep = gui->myMessage.iDirectionIncr;

    // Minimum spacing in pixels (from settings)
    int wstring = 35;
    double minspace = (double)m_iNumberSpacing;
    double minspace2 = minspace * minspace;

    wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    // Iterate data grid with minimum spacing filter (GRIB Mode B)
    wxPoint firstpx(-1000, -1000);
    wxPoint oldpx(-1000, -1000);
    wxPoint oldpy(-1000, -1000);

    for (int i = 0; i < ni; i++) {
        double lon = tlon + i * lonStep;

        // Quick column check
        wxPoint pl;
        GetCanvasPixLL(vp, &pl, tlat + (nj/2) * latStep, lon);

        if (pl.x <= firstpx.x &&
            square(pl.x - firstpx.x) + square(pl.y - firstpx.y) < minspace2 / 1.44)
            continue;

        if (square(pl.x - oldpx.x) + square(pl.y - oldpx.y) < minspace2)
            continue;

        oldpx = pl;
        if (i == 0) firstpx = pl;

        for (int j = 0; j < nj; j++) {
            double lat = tlat + j * latStep;

            double u = gui->gridu[j][i];
            double v = gui->gridv[j][i];
            if (u == ncdf_NOTDEF || v == ncdf_NOTDEF) continue;

            double mag = sqrt(u * u + v * v);
            if (mag < 0.05) continue;

            wxPoint p;
            GetCanvasPixLL(vp, &p, lat, lon);

            // Minimum spacing check
            if (square(p.x - oldpy.x) + square(p.y - oldpy.y) < minspace2)
                continue;
            oldpy = p;

            wxString label;
            label.Printf(_T("%.1f"), mag);

            unsigned char rgba[4];
            GetSeaCurrentGraphicColorRaw(mag, rgba);
            wxColour backColor(rgba[0], rgba[1], rgba[2]);

            // DrawNumbers (like GRIB) - colored background, black border, black text
            if (m_pdc) {
                wxMemoryDC mdc(wxNullBitmap);
                mdc.SetFont(font);
                int tw, th;
                mdc.GetTextExtent(label, &tw, &th);
                int pad = 2;
                int bw = tw + pad * 2, bh = th + pad * 2;
                wxBitmap bm(bw, bh);
                mdc.SelectObject(bm);
                mdc.SetBackground(wxBrush(backColor));
                mdc.Clear();
                mdc.SetTextForeground(*wxBLACK);
                mdc.DrawText(label, pad, pad);
                // Draw black border
                mdc.SetPen(wxPen(*wxBLACK, 1));
                mdc.SetBrush(*wxTRANSPARENT_BRUSH);
                mdc.DrawRectangle(0, 0, bw, bh);
                mdc.SelectObject(wxNullBitmap);
                m_pdc->DrawBitmap(bm, p.x - bw / 2, p.y - bh / 2, true);
            }
#ifdef ocpnUSE_GL
            else {
                wxMemoryDC mdc(wxNullBitmap);
                mdc.SetFont(font);
                int tw, th;
                mdc.GetTextExtent(label, &tw, &th);
                int pad = 2;
                int bw = tw + pad * 2, bh = th + pad * 2;
                wxBitmap bm(bw, bh);
                mdc.SelectObject(bm);
                mdc.SetBackground(wxBrush(backColor));
                mdc.Clear();
                mdc.SetTextForeground(*wxBLACK);
                mdc.DrawText(label, pad, pad);
                // Draw black border
                mdc.SetPen(wxPen(*wxBLACK, 1));
                mdc.SetBrush(*wxTRANSPARENT_BRUSH);
                mdc.DrawRectangle(0, 0, bw, bh);
                mdc.SelectObject(wxNullBitmap);

                wxImage img = bm.ConvertToImage();
                unsigned char *d = img.GetData();
                unsigned char *e = new unsigned char[4 * bw * bh];
                for (int y = 0; y < bh; y++)
                    for (int x = 0; x < bw; x++) {
                        int off = y * bw + x;
                        e[off * 4 + 0] = d[off * 3 + 0];
                        e[off * 4 + 1] = d[off * 3 + 1];
                        e[off * 4 + 2] = d[off * 3 + 2];
                        e[off * 4 + 3] = 255;
                    }
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glRasterPos2i(p.x - bw / 2, p.y + bh / 2);
                glPixelZoom(1, -1);
                glDrawPixels(bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, e);
                glPixelZoom(1, 1);
                glDisable(GL_BLEND);
                delete[] e;
            }
#endif
        }
    }
}



//===================================================================
// RenderParticles - Flow-field particle animation (GRIB-style)
//===================================================================

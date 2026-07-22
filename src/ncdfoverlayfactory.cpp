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
      m_glColorTexture = 0;
      m_bHasColorTexture = false;
      m_bNeedsColorTexRebuild = false;
      m_texDataDim[0] = m_texDataDim[1] = 0;
      m_texGLDim[0] = m_texGLDim[1] = 0;
      m_lvaSize = 0;
      m_lva = nullptr;
      m_glSeaTempTexture = 0;
      m_bHasSeaTempTexture = false;
      m_bNeedsSeaTempTexRebuild = false;
      m_lastIso_vp_scale = -1;
      m_lastIso_vp_latMax = -99999;
      m_lastIso_vp_latMin = -99999;
      m_lastIso_vp_lonMin = -99999;
      m_lastIso_vp_lonMax = -99999;
      m_sstTexDataDim[0] = m_sstTexDataDim[1] = 0;
      m_sstTexGLDim[0] = m_sstTexGLDim[1] = 0;

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
    DeleteColorTexture();
    DeleteSeaTempTexture();
    delete[] m_lva; m_lva = nullptr;
    ClearParticles();
}

void ncdfOverlayFactory::setData(MainDialog *gui, ncdf_pi *plugin, ncdfDataMessage g2data, int numberOfPoints, wxDouble tlat, wxDouble tlon, wxDouble blat, wxDouble blon)
{
	// Mark textures for rebuild (GL deletion deferred to render)
	m_bNeedsColorTexRebuild = true;
	m_bNeedsSeaTempTexRebuild = true;
	m_lastIso_vp_scale = -1;  // Force isoline redraw on data change
	ClearParticles();
	m_last_vp_scale = -1;
	m_last_vp_latMax = -99999.0;
	m_bUpdateParticles = true;
	m_particleBurstCount = 30;

	this->g2data = g2data;
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
    bool hasCurrentGrid = (gui->gridu && gui->gridv);
    bool hasSSTGrid = (gui->gridSST && gui->hasSeaTemp);
    if (!hasCurrentGrid && !hasSSTGrid) return false;
    if (gui->myMessage.lonLength < 2 || gui->myMessage.latLength < 2) return false;
    if (!m_bReadyToRender) return false;

    // Must have at least one rendering feature enabled
    if (!plugin->m_bShowCurrentForce && !plugin->m_bShowCurrentDir &&
        !plugin->m_bShowParticles && !plugin->m_bShowSeaTemp &&
        !plugin->m_bShowSeaTempIso) return false;

    static int s_frameDbg = 0;
    if (s_frameDbg < 200) {
        wxLogMessage(_T("[render] frame %d vp=%p"), s_frameDbg, (void*)vp);
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
		wxLogMessage(_T("[render] ready=%d gui=%p gridu=%p ni=%d showF=%d showD=%d showP=%d"),
			(int)m_bReadyToRender, gui,
			gui ? (void*)gui->gridu : nullptr,
			gui ? (int)gui->myMessage.lonLength : 0,
			plugin ? (int)plugin->m_bShowCurrentForce : -1,
			plugin ? (int)plugin->m_bShowCurrentDir : -1,
			plugin ? (int)plugin->m_bShowParticles : -1);
		s_renderDbg++;
	}

	// Color map: GRIB-style tiled texture with caching
	if(plugin->m_bShowCurrentForce && gui->gridu && gui->gridv && !m_pdc) {
#ifdef ocpnUSE_GL
      // Reset GL state to avoid inheriting GRIB's texture bindings
      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      // Check if color texture needs rebuild (flagged by setData)
      if (m_bNeedsColorTexRebuild) {
          if (m_glColorTexture) { glDeleteTextures(1, &m_glColorTexture); m_glColorTexture = 0; }
          m_bHasColorTexture = false;
          m_bNeedsColorTexRebuild = false;
      }
      // Build texture only if not already created
      if (!m_bHasColorTexture) {
          // Delete old texture if exists (safe here — GL context is active)
          if (m_glColorTexture) {
              glDeleteTextures(1, &m_glColorTexture);
              m_glColorTexture = 0;
          }
          // Create and cache the texture (once per data change)
          int ni = gui->myMessage.lonLength;
          int nj = gui->myMessage.latLength;
          if (ni > 1 && nj > 1) {
              int tw = ni + 2, th = nj + 2;
              unsigned char *texData = new unsigned char[tw * th * 4];
              memset(texData, 0, tw * th * 4);

              unsigned char globalAlpha = 255;  // Fully opaque

              for (int j = 0; j < nj; j++) {
                  if (!gui->gridu[j] || !gui->gridv[j]) break;
                  int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                  for (int i = 0; i < ni; i++) {
                      int x = i + 1, y = texRow + 1;
                      if (x >= tw - 1 || y >= th - 1) continue;
                      double vx = gui->gridu[j][i];
                      double vy = gui->gridv[j][i];
                      int off = 4 * (y * tw + x);
                      if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
                          double mag = sqrt(vx * vx + vy * vy);
                          // Render all valid data including zero/near-zero flow
                          wxColour c = GetSeaCurrentGraphicColor(mag);
                          texData[off] = c.Red();
                          texData[off+1] = c.Green();
                          texData[off+2] = c.Blue();
                          texData[off+3] = globalAlpha;
                      }
                  }
              }

              // GRIB-style border: copy adjacent row/col, then set border alpha=0
              memcpy(texData, texData + 4 * tw, 4 * tw);
              memcpy(texData + 4 * tw * (th - 1), texData + 4 * tw * (th - 2), 4 * tw);
              for (int y = 0; y < th; y++) {
                  memcpy(texData + 4 * y * tw, texData + 4 * (y * tw + 1), 4);
                  memcpy(texData + 4 * (y * tw + tw - 1), texData + 4 * (y * tw + tw - 2), 4);
              }
              for (int x = 0; x < tw; x++) {
                  texData[4 * x + 3] = 0;
                  texData[4 * ((th - 1) * tw + x) + 3] = 0;
              }
              for (int y = 0; y < th; y++) {
                  texData[4 * y * tw + 3] = 0;
                  texData[4 * (y * tw + tw - 1) + 3] = 0;
              }

              glGenTextures(1, &m_glColorTexture);
              glBindTexture(GL_TEXTURE_2D, m_glColorTexture);
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
              glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
              m_bHasColorTexture = true;
              m_texDataDim[0] = ni;
              m_texDataDim[1] = nj;
              m_texGLDim[0] = tw;
              m_texGLDim[1] = th;
              delete[] texData;
          }
      }

      // Color map: GRIB-style tiled texture rendering
      if (m_bHasColorTexture && m_glColorTexture != 0) {
          int ni = m_texDataDim[0], nj = m_texDataDim[1];
          int tw = m_texGLDim[0], th = m_texGLDim[1];
          double north = this->tlat, south = this->blat;
          double west = this->tlon, east = this->blon;

          double gridSpacingLon = (east - west) / (ni - 1);
          double gridSpacingLat = (north - south) / (nj - 1);
          double clon = (west + east) / 2.0;

          double pw = vp->view_scale_ppm * 1e6 / (pow(2, fabs(vp->clat) / 25));
          if (pw < 20) pw = 20;
          int xsquares = wxMax(2, (int)ceil(vp->pix_width / pw));
          int ysquares = wxMax(2, (int)ceil(vp->pix_height / pw));
          if (vp->rotation == 0 && vp->m_projection_type == PI_PROJECTION_MERCATOR)
              xsquares = 1;
          xsquares = wxMax(xsquares, 2);

          double xs = vp->pix_width / (double)xsquares;
          double ys = vp->pix_height / (double)ysquares;

          int neededLva = xsquares + 1;
          if (m_lvaSize < neededLva) {
              delete[] m_lva;
              m_lvaSize = neededLva;
              m_lva = new double[m_lvaSize][2][2];
          }

          int j = 0;
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, m_glColorTexture);
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

          for (double y = 0; y < vp->pix_height + ys / 2; y += ys) {
              int i = 0;
              for (double x = 0; x < vp->pix_width + xs / 2; x += xs) {
                  double lat, lon;
                  wxPoint p(x, y);
                  GetCanvasLLPix(vp, p, &lat, &lon);

                  if (clon - lon > 180) lon += 360;
                  else if (lon - clon > 180) lon -= 360;

                  double potNormX = (double)ni / tw;
                  double potNormY = (double)nj / th;
                  m_lva[i][j][0] = ((lon - west) / gridSpacingLon + 1.5) / tw * potNormX;
                  m_lva[i][j][1] = ((lat - south) / gridSpacingLat + 1.5) / th * potNormY;

                  if (i > 0 && y > 0) {
                      double u0 = m_lva[i-1][!j][0], v0 = m_lva[i-1][!j][1];
                      double u1 = m_lva[i  ][!j][0], v1 = m_lva[i  ][!j][1];
                      double u2 = m_lva[i  ][ j][0], v2 = m_lva[i  ][ j][1];
                      double u3 = m_lva[i-1][ j][0], v3 = m_lva[i-1][ j][1];

                      if ((u0 >= 0 || u1 >= 0 || u2 >= 0 || u3 >= 0) &&
                          (u0 <= 1 || u1 <= 1 || u2 <= 1 || u3 <= 1) &&
                          (v0 >= 0 || v1 >= 0 || v2 >= 0 || v3 >= 0) &&
                          (v0 <= 1 || v1 <= 1 || v2 <= 1 || v3 <= 1)) {
                          float sx = (float)(x - xs), sy = (float)(y - ys);
                          float sw = (float)xs, sh = (float)ys;
                          glBegin(GL_QUADS);
                          glTexCoord2f(u0, v0); glVertex2f(sx, sy);
                          glTexCoord2f(u1, v1); glVertex2f(sx + sw, sy);
                          glTexCoord2f(u2, v2); glVertex2f(sx + sw, sy + sh);
                          glTexCoord2f(u3, v3); glVertex2f(sx, sy + sh);
                          glEnd();
                      }
                  }
                  i++;
              }
              j = !j;
          }

          glDisable(GL_BLEND);
          glDisable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, 0);
      }
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

	// Sea temperature overlay
	if (plugin->m_bShowSeaTemp && gui && gui->gridSST && gui->hasSeaTemp) {
		RenderSeaTempOverlay(vp);
	}

	// Sea temperature isolines
	if (plugin->m_bShowSeaTempIso && gui && gui->gridSST && gui->hasSeaTemp) {
		RenderSeaTempIsoLines(vp);
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



void ncdfOverlayFactory::RenderncdfCurrent()
{
    if (!gui || !gui->gridu || !gui->gridv) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // GRIB-style geographic grid with 80px minimum spacing
    int arrowSpacing = 80;
    wxPoint p1, p2;
    GetCanvasPixLL(vp, &p1, vp->clat, vp->clon);
    GetCanvasPixLL(vp, &p2, vp->clat + 1.0, vp->clon + 1.0);
    double dy = fabs((double)(p2.y - p1.y));
    double dx = fabs((double)(p2.x - p1.x));
    double lat_sp = (dy > 1) ? arrowSpacing / dy : 1.0;
    double lon_sp = (dx > 1) ? arrowSpacing / dx : 1.0;
    if (lat_sp < 0.5) lat_sp = 0.5;
    if (lon_sp < 0.5) lon_sp = 0.5;

    double start_lat = floor(vp->lat_min / lat_sp) * lat_sp;
    double start_lon = floor(vp->lon_min / lon_sp) * lon_sp;

    wxColour colour;
    GetGlobalColor(_T("UBLCK"), &colour);

    for (double lat = start_lat; lat <= vp->lat_max; lat += lat_sp) {
        for (double lon = start_lon; lon <= vp->lon_max; lon += lon_sp) {
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, lon, lat, true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, lon, lat, true);
            if (vx == ncdf_NOTDEF || vy == ncdf_NOTDEF || !isfinite(vx) || !isfinite(vy)) continue;
            double mag = sqrt(vx * vx + vy * vy);
            if (mag < 0.01) continue;
            wxPoint p;
            GetCanvasPixLL(vp, &p, lat, lon);
            if (!PointInLLBox(vp, lon, lat)) continue;
            double dir = 90.0 + (atan2(vy, -vx) * 180.0 / PI);
            if (dir < 0) dir += 360.0;
            drawWaveArrow(p.x, p.y, dir - 90, colour);
        }
    }
}
bool ncdfOverlayFactory::RenderncdfCurrentBmp()
{

	static wxPoint porg;
      static int width, height;    
      // If needed, create the bitmap
            if(m_pbm_current == NULL)
            {
                 wxPoint pmin;
                  GetCanvasPixLL(vp,  &pmin, tlat, tlon);
                  wxPoint pmax;
                  GetCanvasPixLL(vp,  &pmax, blat, blon);

                  width = abs(pmax.x - pmin.x);
                  height = abs(pmax.y - pmin.y);	

				  

	          if(vp->pix_width < width) width = vp->pix_width;
		  if(vp->pix_height < height || vp->lat_max < blat) 
		  { 
		    GetCanvasPixLL(vp,  &porg, vp->lat_max, tlon);
		    height = vp->pix_height; 		    
		  }
		  if(vp->pix_width == width && vp->pix_height == height) 
		  { 
		    GetCanvasPixLL(vp,  &porg, vp->lat_max, vp->lon_min);
		  }
		  else	if(vp->pix_width != width && vp->pix_height != height)  
		    GetCanvasPixLL(vp,  &porg, blat, tlon);
		 		  
                  {
					  //    Dont try to create enormous GRIB bitmaps ( no more than the screen size )
					  if (width > m_ParentSize.GetWidth() || height > m_ParentSize.GetHeight()){
						  return false;
					  }

					  //    This could take a while....
			      if (g2data.ucurr == NULL || g2data.vcurr == NULL){
					  return false;
				  }
                              wxImage gr_image(width, height);
                              gr_image.InitAlpha();

                              int ncdf_pixel_size = 4;
			      
			      ::wxBeginBusyCursor();
				  int myCounter = 0;
                  
				  unsigned char a;
				  wxPoint p;

							  for (int ipix = 0; ipix < (width + 1); ipix += ncdf_pixel_size)
                              {
								  for (int jpix = 0; jpix < (height + 1); jpix += ncdf_pixel_size)
                                    {
                                          double lat, lon;
                                          p.x = ipix + porg.x;
                                          p.y = jpix + porg.y;
                                          GetCanvasLLPix( vp, p, &lat, &lon);

					                if(!PointInLLBox(vp, lon, lat) && !PointInLLBox(vp, lon-360.0, lat)) continue;
									double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, lon, lat, true);
									double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, lon, lat, true);

                                          if ((vx != ncdf_NOTDEF) && (vy != ncdf_NOTDEF))
                                          {
											    double  vkn = sqrt(vx*vx+vy*vy)*3.6/1.852;
												if (vkn == 0){
													a = 0;													
												}else{
													a = 128;													
												}

                                                wxColour c = GetSeaCurrentGraphicColor(vkn);
                                                unsigned char r = c.Red();
                                                unsigned char g = c.Green();
                                                unsigned char b = c.Blue();

                                                for(int xp=0 ; xp < ncdf_pixel_size ; xp++)
                                                      for(int yp=0 ; yp < ncdf_pixel_size ; yp++)
                                                {
                                                      gr_image.SetRGB(ipix + xp, jpix + yp, r,g,b);
													  gr_image.SetAlpha(ipix + xp, jpix + yp, a);

                                                }
                                          }
                                          else
                                          {
                                                for(int xp=0 ; xp < ncdf_pixel_size ; xp++)
                                                      for(int yp=0 ; yp < ncdf_pixel_size ; yp++)
                                                {
                                                      gr_image.SetAlpha(ipix + xp, jpix + yp, 0);
                                                }
                                          }
									 myCounter++;
									}
									
                              }
                              wxImage bl_image = (gr_image.Blur(4));

                        //    Create a Bitmap
                              m_pbm_current = new wxBitmap(bl_image);
                              wxMask *gr_mask = new wxMask(*m_pbm_current, wxColour(0,0,0));
                              m_pbm_current->SetMask(gr_mask);

                              ::wxEndBusyCursor();
                  }
            }

            if(m_pbm_current)
            {      
                  DrawOLBitmap(*m_pbm_current, porg.x, porg.y, true);
            }
      return true;
	  
 
}


void ncdfOverlayFactory::drawWaveArrow(int i, int j, double ang, wxColour arrowColor)
{
    // Simple solid single arrow (GRIB-style direction arrow)
    double si = sin(ang * PI / 180.);
    double co = cos(ang * PI / 180.);
    int arrowSize = 20;
    int dec = -arrowSize / 2;

    wxPen pen(arrowColor, 2);
    if (m_pdc) {
        m_pdc->SetPen(pen);
        m_pdc->SetBrush(*wxTRANSPARENT_BRUSH);
    }
#ifdef ocpnUSE_GL
    else
        glColor3ub(arrowColor.Red(), arrowColor.Green(), arrowColor.Blue());
#endif

    // Arrow shaft
    drawTransformedLine(pen, si, co, i, j, dec, 0, dec + arrowSize, 0);
    // Arrow head (V shape)
    drawTransformedLine(pen, si, co, i, j, dec, 0, dec + 7, 5);
    drawTransformedLine(pen, si, co, i, j, dec, 0, dec + 7, -5);
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
	{
		wxColour isoLineColor = myColour;
		glColor4ub(isoLineColor.Red(), isoLineColor.Green(), isoLineColor.Blue(),
			255/*isoLineColor.Alpha()*/);

		glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
			GL_POLYGON_BIT | GL_HINT_BIT); //Save state
		{

			//      Enable anti-aliased lines, at best quality
			glEnable(GL_LINE_SMOOTH);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
			glLineWidth(width);

			glBegin(GL_LINES);
			glVertex2d(x1, y1);
			glVertex2d(x2, y2);
			glEnd();
		}

		glPopAttrib();
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

// Is the given point in the vp ??
bool PointInLLBox(PlugIn_ViewPort *vp, double x, double y)
{


    if (  x >= (vp->lon_min) && x <= (vp->lon_max) &&
            y >= (vp->lat_min) && y <= (vp->lat_max) )
            return TRUE;
    return FALSE;
}



wxColour ncdfOverlayFactory::GetSeaCurrentGraphicColor(double val_in)
{
    // Custom color map with 5 flow speed ranges
    // <0.2: micro (deep blue), 0.2-0.5: low (blue-cyan), 0.5-1.0: medium (cyan-green),
    // 1.0-1.5: high (green-yellow), >1.5: very high (orange-red)
    double val = wxMax(val_in, 0.0);

    // Color stops: {speed, R, G, B}
    static const double stops[][4] = {
        {0.0,  20,  20, 180},  // deep blue (micro)
        {0.2,  30,  80, 220},  // blue (micro/low boundary)
        {0.5,  0,  180, 220},  // cyan (low/medium boundary)
        {1.0,  0,  200,  80},  // green (medium/high boundary)
        {1.5, 220, 220,  20},  // yellow (high/very high boundary)
        {2.0, 240, 100,  20},  // orange
        {3.0, 220,  20,  20},  // red (very high)
    };
    const int nStops = sizeof(stops) / sizeof(stops[0]);

    // Clamp to range
    if (val <= stops[0][0]) {
        return wxColour((unsigned char)stops[0][1], (unsigned char)stops[0][2], (unsigned char)stops[0][3]);
    }
    if (val >= stops[nStops - 1][0]) {
        return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
    }

    // Find the two stops to interpolate between
    for (int i = 1; i < nStops; i++) {
        if (val <= stops[i][0]) {
            double range = stops[i][0] - stops[i-1][0];
            double t = (range > 0) ? (val - stops[i-1][0]) / range : 0;

            // Smooth interpolation (ease-in-out)
            t = t * t * (3.0 - 2.0 * t);

            unsigned char r = (unsigned char)(stops[i-1][1] + t * (stops[i][1] - stops[i-1][1]));
            unsigned char g = (unsigned char)(stops[i-1][2] + t * (stops[i][2] - stops[i-1][2]));
            unsigned char b = (unsigned char)(stops[i-1][3] + t * (stops[i][3] - stops[i-1][3]));
            return wxColour(r, g, b);
        }
    }

    return wxColour(220, 20, 20);  // fallback red
}


//===================================================================
// Color texture (GRIB-style GL texture for color map)
//===================================================================
void ncdfOverlayFactory::DeleteColorTexture()
{
    if (m_bHasColorTexture && m_glColorTexture) {
        glDeleteTextures(1, &m_glColorTexture);
        m_glColorTexture = 0;
        m_bHasColorTexture = false;
    }
}

void ncdfOverlayFactory::CreateColorTexture(PlugIn_ViewPort *vp)
{
    if (!gui || !gui->gridu || !gui->gridv) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    int tw = ni + 2, th = nj + 2;
    if (tw > 1024 || th > 1024) {
        double scale = 1024.0 / wxMax(ni, nj);
        tw = (int)(ni * scale) + 2;
        th = (int)(nj * scale) + 2;
    }
    m_texDataDim[0] = ni; m_texDataDim[1] = nj;
    m_texGLDim[0] = tw; m_texGLDim[1] = th;

    unsigned char *data = new unsigned char[tw * th * 4];
    memset(data, 0, tw * th * 4);

    int transparency = 50;
    if (plugin) transparency = plugin->m_iOverlayTransparency;
    unsigned char alpha = (unsigned char)(255 * (100 - transparency) / 100);

    // Fill texture with velocity magnitude data
    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            int x = i + 1, y = j + 1;
            if (x >= tw - 1 || y >= th - 1) continue;
            double vx = gui->gridu[j][i];
            double vy = gui->gridv[j][i];
            int off = 4 * (y * tw + x);
            if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
                double mag = sqrt(vx * vx + vy * vy);
                wxColour c = GetSeaCurrentGraphicColor(mag);
                data[off] = c.Red(); data[off+1] = c.Green(); data[off+2] = c.Blue();
                data[off+3] = (mag < 0.01) ? 0 : alpha;
            } else { data[off+3] = 0; }
        }
    }

    for (int x = 0; x < tw; x++) { data[4*x+3] = 0; data[4*((th-1)*tw+x)+3] = 0; }
    for (int y = 0; y < th; y++) { data[4*y*tw+3] = 0; data[4*(y*tw+tw-1)+3] = 0; }

    DeleteColorTexture();
    glGenTextures(1, &m_glColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_glColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    m_bHasColorTexture = true;
    delete[] data;
}

void ncdfOverlayFactory::DrawColorTexture(PlugIn_ViewPort *vp)
{
    if (!m_bHasColorTexture || !m_glColorTexture || !gui) return;
    double lat1 = gui->myMessage.firstGridPointLat;
    double lon1 = gui->myMessage.firstGridPointLong;
    double lat2 = gui->myMessage.lastGridPointLat;
    double lon2 = gui->myMessage.lastGridPointLong;
    // Ensure correct ordering: north > south, east > west
    double tlat = wxMax(lat1, lat2);
    double blat = wxMin(lat1, lat2);
    double tlon = wxMin(lon1, lon2);
    double blon = wxMax(lon1, lon2);
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    int tw = m_texGLDim[0], th = m_texGLDim[1];
    double potNormX = (double)m_texDataDim[0] / tw;
    double potNormY = (double)m_texDataDim[1] / th;

    wxPoint pTL, pBR;
    GetCanvasPixLL(vp, &pTL, tlat, tlon);
    GetCanvasPixLL(vp, &pBR, blat, blon);

    float u0 = (float)(1.0 / tw) * potNormX;
    float u1 = (float)((ni + 1.0) / tw) * potNormX;
    float v0 = (float)(1.0 / th) * potNormY;
    float v1 = (float)((nj + 1.0) / th) * potNormY;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_glColorTexture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(pTL.x, pTL.y);
    glTexCoord2f(u1, v0); glVertex2f(pBR.x, pTL.y);
    glTexCoord2f(u1, v1); glVertex2f(pBR.x, pBR.y);
    glTexCoord2f(u0, v1); glVertex2f(pTL.x, pBR.y);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

//===================================================================
// Particle system (GRIB port)
//===================================================================
#define MAX_PARTICLE_HISTORY 8

struct Particle {
    int m_Duration;
    int m_HistoryPos, m_HistorySize, m_Run;
    double m_Velocity;  // Track velocity for trail length control
    struct ParticleNode {
        float m_Pos[2];
        float m_Screen[2];
        unsigned char m_Color[3];
    } m_History[MAX_PARTICLE_HISTORY];
};

struct ParticleMap {
    ParticleMap() : history_size(0) { last_viewport.bValid = false; }
    ~ParticleMap() {}
    std::vector<Particle> m_Particles;
    int history_size;
    PlugIn_ViewPort last_viewport;
};

void ncdfOverlayFactory::ClearParticles() {
    delete static_cast<ParticleMap*>(m_ParticleMap);
    m_ParticleMap = nullptr;
}

void ncdfOverlayFactory::RenderParticles(PlugIn_ViewPort *vp)
{
    if (!gui || !gui->gridu || !gui->gridv) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni <= 1 || nj <= 1) return;
    double tlat = gui->myMessage.firstGridPointLat;
    double tlon = gui->myMessage.firstGridPointLong;
    double blat = gui->myMessage.lastGridPointLat;
    double blon = gui->myMessage.lastGridPointLong;
    if (tlat >= blat || tlon >= blon) return;

    if (!m_ParticleMap) m_ParticleMap = new ParticleMap();
    ParticleMap *pm = static_cast<ParticleMap*>(m_ParticleMap);
    std::vector<Particle> &particles = pm->m_Particles;

    const int max_duration = 50;
    const int run_count = 6;
    int sliderVal = 5;
    if (gui && gui->pPlugIn) sliderVal = gui->pPlugIn->m_iParticleDensity;
    // GRIB formula: history_size = 27 / sqrt(density)
    // density = 0.2 * slider → slider=5 → density=1.0 → history_size=27 (capped at 8)
    double density = 0.2 * sliderVal;
    int history_size = 27 / sqrt(wxMax(density, 0.1));
    history_size = wxMax(4, wxMin(history_size, MAX_PARTICLE_HISTORY));

    // Viewport caching
    PlugIn_ViewPort &lvp = pm->last_viewport;
    if (lvp.bValid == false || vp->view_scale_ppm != lvp.view_scale_ppm ||
        vp->skew != lvp.skew || vp->rotation != lvp.rotation ||
        vp->clat != lvp.clat || vp->clon != lvp.clon) {
        for (auto it = particles.begin(); it != particles.end(); it++)
            for (int i = 0; i < it->m_HistorySize; i++) {
                Particle::ParticleNode &n = it->m_History[i];
                if (n.m_Pos[0] == -10000) continue;
                wxPoint ps;
                GetCanvasPixLL(vp, &ps, n.m_Pos[1], n.m_Pos[0]);
                n.m_Screen[0] = ps.x; n.m_Screen[1] = ps.y;
            }
        lvp = *vp;
    }

    // Update particle positions (timer-driven or burst after data change)
    bool shouldUpdate = m_bUpdateParticles || (m_particleBurstCount > 0);
    if (m_particleBurstCount > 0) m_particleBurstCount--;
    m_bUpdateParticles = false;
    if (shouldUpdate) {
    for (unsigned int i = 0; i < particles.size(); i++) {
        Particle &it = particles[i];
        if (++it.m_Run < run_count) continue;
        it.m_Run = 0;
        if (it.m_Duration > max_duration) {
            it = particles[particles.size() - 1];
            particles.pop_back();
            i--;
            continue;
        }
        it.m_Duration++;
        float *pp = it.m_History[it.m_HistoryPos].m_Pos;
        if (++it.m_HistorySize > history_size) it.m_HistorySize = history_size;
        if (++it.m_HistoryPos >= history_size) it.m_HistoryPos = 0;
        Particle::ParticleNode &n = it.m_History[it.m_HistoryPos];
        float(&p)[2] = n.m_Pos;
        double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, pp[0], pp[1], true);
        double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, pp[0], pp[1], true);
        double vkn = 0, ang;
        if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
            double mag = sqrt(vx * vx + vy * vy);
            ang = atan2(vx, vy) * 180.0 / PI;  // atan2(east, north) = bearing
            vkn = mag;
        } else { vkn = 0; ang = 0; }
        if (it.m_Duration < max_duration - history_size && vkn > 0.3 && vkn < 100) {
    // GRIB optimization: convert m/s to knots (1 m/s = 1.94 knots)
    // This makes particles move at the same speed as GRIB
    double d = vkn * run_count * 1.94;  // Match GRIB's knot-based speed
            float angr = (float)(ang / 180.0 * PI);
            float latr = pp[1] * (float)PI / 180.0f;
            float D = (float)(d / 3443.0);
            float sD = sinf(D), cD = cosf(D);
            float sy = sinf(latr), cy = cosf(latr);
            float sa = sinf(angr), ca = cosf(angr);
            p[0] = pp[0] + asinf(sa * sD / cy) * 180.0f / (float)PI;
            p[1] = asinf(sy * cD + cy * sD * ca) * 180.0f / (float)PI;
            wxPoint ps;
            GetCanvasPixLL(vp, &ps, p[1], p[0]);
            n.m_Screen[0] = ps.x; n.m_Screen[1] = ps.y;
            wxColour c = GetSeaCurrentGraphicColor(vkn);
            n.m_Color[0] = c.Red(); n.m_Color[1] = c.Green(); n.m_Color[2] = c.Blue();
            it.m_Velocity = vkn;  // Store velocity for trail length control
        } else { p[0] = -10000; }
    }

    // GRIB formula: total_particles = density * Ni * Nj
    int total_particles = (int)(density * ni * nj);
    int max_grid = ni * nj;
    if (total_particles > max_grid) total_particles = max_grid;
    if (total_particles > 50000) total_particles = 50000;
    int remove = ((int)particles.size() - total_particles) / 16;
    for (int i = 0; i < remove; i++) particles.pop_back();
    int run = 0;
    int new_count = (total_particles - (int)particles.size()) / 64;  // GRIB spawn rate
    for (int npi = 0; npi < new_count; npi++) {
        float p[2];
        double vkn = 0, ang = 0;
        // Velocity-weighted spawning: high-flow = dense, low-flow = sparse
        for (int attempt = 0; attempt < 30; attempt++) {
            p[0] = tlon + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (blon - tlon);
            p[1] = tlat + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (blat - tlat);
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, p[0], p[1], true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, p[0], p[1], true);
            if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
                double mag = sqrt(vx * vx + vy * vy);
                if (mag < 0.3) continue;  // Skip below 0.3 m/s
                // Linear: prob = (mag - 0.3) / 1.7 — increases every 0.1 m/s
                double prob = wxMin(1.0, (mag - 0.3) / 1.7);
                if ((double)rand() / RAND_MAX < prob) {
                    vkn = mag; ang = atan2(vx, vy) * 180.0 / PI;
                    break;
                }
            }
        }
        if (vkn < 0.3) continue;  // No valid position found
        Particle np;
        np.m_Duration = rand() % (max_duration / 2);
        np.m_HistoryPos = 0; np.m_HistorySize = 1;
        np.m_Run = run++;
        if (run == run_count) run = 0;
        memcpy(np.m_History[0].m_Pos, p, sizeof p);
        wxPoint ps;
        GetCanvasPixLL(vp, &ps, p[1], p[0]);
        np.m_History[0].m_Screen[0] = ps.x; np.m_History[0].m_Screen[1] = ps.y;
        wxColour c = GetSeaCurrentGraphicColor(vkn);
        np.m_History[0].m_Color[0] = c.Red(); np.m_History[0].m_Color[1] = c.Green(); np.m_History[0].m_Color[2] = c.Blue();

        // Pre-fill history with fake updates so trail is visible immediately
        float fakePos[2] = {p[0], p[1]};
        for (int h = 1; h < history_size && h < MAX_PARTICLE_HISTORY; h++) {
            double fakeVx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, fakePos[0], fakePos[1], true);
            double fakeVy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, fakePos[0], fakePos[1], true);
            if (fakeVx != ncdf_NOTDEF && fakeVy != ncdf_NOTDEF && isfinite(fakeVx) && isfinite(fakeVy)) {
                double fakeMag = sqrt(fakeVx*fakeVx + fakeVy*fakeVy);
                double fakeAng = atan2(fakeVx, fakeVy) * 180.0 / PI;
                double fakeD = fakeMag * run_count * 1.94;  // Match GRIB
                float fakeAngr = (float)(fakeAng / 180.0 * PI);
                float fakeLatr = fakePos[1] * (float)PI / 180.0f;
                float fakeDD = (float)(fakeD / 3443.0);
                float sD = sinf(fakeDD), cD = cosf(fakeDD);
                float sy = sinf(fakeLatr), cy = cosf(fakeLatr);
                float sa = sinf(fakeAngr), ca = cosf(fakeAngr);
                fakePos[0] = fakePos[0] + asinf(sa * sD / cy) * 180.0f / (float)PI;
                fakePos[1] = asinf(sy * cD + cy * sD * ca) * 180.0f / (float)PI;
            }
            np.m_History[h].m_Pos[0] = fakePos[0];
            np.m_History[h].m_Pos[1] = fakePos[1];
            wxPoint fps;
            GetCanvasPixLL(vp, &fps, fakePos[1], fakePos[0]);
            np.m_History[h].m_Screen[0] = fps.x; np.m_History[h].m_Screen[1] = fps.y;
            wxColour fc = GetSeaCurrentGraphicColor(vkn);
            np.m_History[h].m_Color[0] = fc.Red(); np.m_History[h].m_Color[1] = fc.Green(); np.m_History[h].m_Color[2] = fc.Blue();
            np.m_HistorySize = h + 1;
            np.m_HistoryPos = h;
        }

        particles.push_back(np);
    }
    } // end shouldUpdate

    // Debug: log particle state
    static int s_pDbg = 0;
    if (s_pDbg < 5) {
        wxLogMessage(_T("[particle] count=%zu, m_pdc=%p, history_size=%d"),
            particles.size(), m_pdc, history_size);
        s_pDbg++;
    }

    // Render with GRIB-style interpolation (immediate mode GL, stable)
    if (!m_pdc && !particles.empty()) {
#ifdef ocpnUSE_GL
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(2.3f);

        for (auto it = particles.begin(); it != particles.end(); it++) {
            wxUint8 alpha = 250;
            int i = it->m_HistoryPos;

            bool lip_valid = false;
            float *lp = nullptr, lip[2];
            wxUint8 lc[4];

            // Velocity-dependent trail length: slow=short, fast=long
            // Scale alpha decay: lower velocity = faster decay = shorter trail
            double velFactor = wxMax(0.3, wxMin(3.0, it->m_Velocity * 2.0));
            int trailSteps = (int)(history_size * velFactor);
            trailSteps = wxMax(2, wxMin(trailSteps, MAX_PARTICLE_HISTORY));
            int alphaDecay = 250 / trailSteps;

            for (;;) {
                if (it->m_History[i].m_Pos[0] != -10000) {
                    float *sp = it->m_History[i].m_Screen;
                    wxUint8 *ci = it->m_History[i].m_Color;
                    wxUint8 c[4] = {ci[0], ci[1], ci[2], alpha};

                    if (lp && fabsf(lp[0] - sp[0]) < vp->pix_width) {
                        float sip[2];
                        float d = (float)it->m_Run / run_count;
                        sip[0] = d * lp[0] + (1 - d) * sp[0];
                        sip[1] = d * lp[1] + (1 - d) * sp[1];

                        if (lip_valid && fabsf(lip[0] - sip[0]) < vp->pix_width) {
                            glColor4ub(lc[0], lc[1], lc[2], lc[3]);
                            glBegin(GL_LINES);
                            glVertex2f(lip[0], lip[1]);
                            glVertex2f(sip[0], sip[1]);
                            glEnd();
                        }
                        lip[0] = sip[0]; lip[1] = sip[1];
                        lip_valid = true;
                    }
                    memcpy(lc, c, sizeof lc);
                    lp = sp;
                }

                if (--i < 0) {
                    i = history_size - 1;
                    if (i >= it->m_HistorySize) break;
                }
                if (i == it->m_HistoryPos) break;
                alpha -= alphaDecay;
            }
        }

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#ifdef __WXMSW__
        glFlush();
#endif
#endif
    }

    // Adaptive timer (GRIB formula)
    if (!m_tParticleTimer.IsRunning()) {
        m_tParticleTimer.Start(50, wxTIMER_ONE_SHOT);
    }
}

// Calculates if two boxes intersect. If so, the function returns _ON.
// If they do not intersect, two scenario's are possible:
// other is outside this -> return _OUT
// other is inside this -> return _IN
OVERLAP Intersect(PlugIn_ViewPort *vp,
       double lat_min, double lat_max, double lon_min, double lon_max, double Marge)
{

    if (((vp->lon_min - Marge) > (lon_max + Marge)) ||
         ((vp->lon_max + Marge) < (lon_min - Marge)) ||
         ((vp->lat_max + Marge) < (lat_min - Marge)) ||
         ((vp->lat_min - Marge) > (lat_max + Marge)))
        return _OUT;

    // Check if other.bbox is inside this bbox
    if ((vp->lon_min <= lon_min) &&
         (vp->lon_max >= lon_max) &&
         (vp->lat_max >= lat_max) &&
         (vp->lat_min <= lat_min))
        return _IN;

    // Boundingboxes intersect
    return _ON;
}


//===================================================================
// Sea Temperature rendering
//===================================================================

// Sea temperature color map (-2°C to 32°C, purple→red, smoothstep)
wxColour ncdfOverlayFactory::GetSeaTempGraphicColor(double temp_c)
{
    static const double stops[][4] = {
        {-2, 0x80, 0x00, 0xc0},  // purple
        { 2, 0x40, 0x30, 0xff},  // blue
        { 7, 0x00, 0x90, 0xfa},  // sky blue
        {12, 0x00, 0xd8, 0xb0},  // cyan
        {17, 0x10, 0xbb, 0x20},  // green
        {22, 0x90, 0xd0, 0x00},  // yellow-green
        {26, 0xf0, 0xd0, 0x00},  // yellow
        {30, 0xf0, 0x70, 0x00},  // orange
        {32, 0xff, 0x00, 0x00},  // red
    };
    const int nStops = sizeof(stops) / sizeof(stops[0]);

    if (temp_c >= stops[nStops - 1][0])
        return wxColour(0xff, 0x00, 0x00);
    if (temp_c <= stops[0][0])
        return wxColour((unsigned char)stops[0][1], (unsigned char)stops[0][2], (unsigned char)stops[0][3]);

    for (int i = 1; i < nStops; i++) {
        if (temp_c <= stops[i][0]) {
            double range = stops[i][0] - stops[i-1][0];
            double t = (range > 0) ? (temp_c - stops[i-1][0]) / range : 0;
            t = t * t * (3.0 - 2.0 * t);
            unsigned char r = (unsigned char)(stops[i-1][1] + t * (stops[i][1] - stops[i-1][1]));
            unsigned char g = (unsigned char)(stops[i-1][2] + t * (stops[i][2] - stops[i-1][2]));
            unsigned char b = (unsigned char)(stops[i-1][3] + t * (stops[i][3] - stops[i-1][3]));
            return wxColour(r, g, b);
        }
    }
    return wxColour(0xff, 0x00, 0x00);
}

void ncdfOverlayFactory::DeleteSeaTempTexture()
{
    if (m_bHasSeaTempTexture && m_glSeaTempTexture) {
        glDeleteTextures(1, &m_glSeaTempTexture);
        m_glSeaTempTexture = 0;
        m_bHasSeaTempTexture = false;
    }
}

void ncdfOverlayFactory::RenderSeaTempOverlay(PlugIn_ViewPort *vp)
{
    if (!gui || !vp) return;
    // Snapshot grid pointer — if it becomes invalid mid-render, bail safely
    double **sstGrid = gui->gridSST;
    if (!sstGrid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        // Lazy texture rebuild (flagged by setData, safe in GL context)
        if (m_bNeedsSeaTempTexRebuild) {
            if (m_glSeaTempTexture) { glDeleteTextures(1, &m_glSeaTempTexture); m_glSeaTempTexture = 0; }
            m_bHasSeaTempTexture = false;
            m_bNeedsSeaTempTexRebuild = false;
        }

        // Create texture once per data change (GRIB pattern)
        if (!m_bHasSeaTempTexture) {
            int tw = ni + 2, th = nj + 2;  // 1-pixel transparent border
            unsigned char *texData = new(std::nothrow) unsigned char[tw * th * 4];
            if (!texData) return;
            memset(texData, 0, tw * th * 4);

            unsigned char alpha = 255;  // Fully opaque

            // Fill texture: write RGBA directly (GRIB pattern, no wxColour overhead)
            for (int j = 0; j < nj; j++) {
                if (!sstGrid[j]) break;
                int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                for (int i = 0; i < ni; i++) {
                    double val = sstGrid[j][i];
                    if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) continue;
                    int x = i + 1, y = texRow + 1;
                    if (x >= tw - 1 || y >= th - 1) continue;
                    // Write RGBA directly (avoid wxColour intermediate)
                    wxColour c = GetSeaTempGraphicColor(val);
                    int off = 4 * (y * tw + x);
                    texData[off]     = c.Red();
                    texData[off + 1] = c.Green();
                    texData[off + 2] = c.Blue();
                    texData[off + 3] = alpha;
                }
            }

            // GRIB-style border: copy adjacent row/col, then set border alpha=0
            memcpy(texData, texData + 4 * tw, 4 * tw);
            memcpy(texData + 4 * tw * (th - 1), texData + 4 * tw * (th - 2), 4 * tw);
            for (int y = 0; y < th; y++) {
                memcpy(texData + 4 * y * tw, texData + 4 * (y * tw + 1), 4);
                memcpy(texData + 4 * (y * tw + tw - 1), texData + 4 * (y * tw + tw - 2), 4);
            }
            for (int x = 0; x < tw; x++) {
                texData[4 * x + 3] = 0;
                texData[4 * ((th - 1) * tw + x) + 3] = 0;
            }
            for (int y = 0; y < th; y++) {
                texData[4 * y * tw + 3] = 0;
                texData[4 * (y * tw + tw - 1) + 3] = 0;
            }

            glGenTextures(1, &m_glSeaTempTexture);
            glBindTexture(GL_TEXTURE_2D, m_glSeaTempTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
            delete[] texData;

            m_bHasSeaTempTexture = true;
            m_sstTexDataDim[0] = tw;
            m_sstTexDataDim[1] = th;
            m_sstTexGLDim[0] = tw;
            m_sstTexGLDim[1] = th;
        }

        // Draw tiled texture (GRIB pattern)
        if (m_bHasSeaTempTexture && m_glSeaTempTexture) {
            int tw = m_sstTexGLDim[0], th = m_sstTexGLDim[1];
            double potNormX = (double)m_sstTexDataDim[0] / tw;
            double potNormY = (double)m_sstTexDataDim[1] / th;
            double lat_min = blat, lon_min = tlon;
            double lat_max = tlat, lon_max = blon;
            double latstep = fabs(lat_max - lat_min) / (nj - 1);
            double lonstep = (lon_max - lon_min) / (ni - 1);
            if (latstep < 1e-10 || lonstep < 1e-10) return;
            double clon = (lon_min + lon_max) / 2;

            // Tile grid for non-linear projection handling
            double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
            if (pw < 20) pw = 20;
            int xs = (int)ceil(vp->pix_width / pw);
            int ys = (int)ceil(vp->pix_height / pw);
            if (vp->rotation == 0) xs = 1;
            if (xs < 2) xs = 2; if (ys < 2) ys = 2;
            if (xs > 16) xs = 16; if (ys > 16) ys = 16;
            int gridW = xs + 1, gridH = ys + 1;

            double *lva = new(std::nothrow) double[gridW * gridH * 2];
            if (!lva) return;

            for (int i = 0; i < gridW; i++) {
                double px = vp->pix_width / (double)xs * i;
                for (int j = 0; j < gridH; j++) {
                    double py = vp->pix_height / (double)ys * j;
                    double lat, lon;
                    wxPoint pt((int)px, (int)py);
                    GetCanvasLLPix(vp, pt, &lat, &lon);
                    if (clon - lon > 180) lon += 360;
                    else if (lon - clon > 180) lon -= 360;
                    int idx = (i * gridH + j) * 2;
                    lva[idx]     = ((lon - lon_min) / lonstep + 1.5) / tw * potNormX;
                    lva[idx + 1] = ((lat - lat_min) / latstep + 1.5) / th * potNormY;
                }
            }

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m_glSeaTempTexture);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glColor4f(1, 1, 1, 1);

            double xS = vp->pix_width / (double)xs;
            double yS = vp->pix_height / (double)ys;
            for (int i = 0; i < xs; i++) {
                for (int j = 0; j < ys; j++) {
                    int i00 = (i * gridH + j) * 2;
                    int i10 = ((i+1) * gridH + j) * 2;
                    int i11 = ((i+1) * gridH + j+1) * 2;
                    int i01 = (i * gridH + j+1) * 2;
                    double u0=lva[i00], u1=lva[i10], u2=lva[i11], u3=lva[i01];
                    double v0=lva[i00+1], v1=lva[i10+1], v2=lva[i11+1], v3=lva[i01+1];
                    if (!((u0>=0||u1>=0||u2>=0||u3>=0)&&(u0<=1||u1<=1||u2<=1||u3<=1))) continue;
                    if (!((v0>=0||v1>=0||v2>=0||v3>=0)&&(v0<=1||v1<=1||v2<=1||v3<=1))) continue;
                    if (u1 <= u0) continue;
                    double x = xS * i, y = yS * j;
                    glBegin(GL_QUADS);
                    glTexCoord2d(u0,v0); glVertex2f((float)x,(float)y);
                    glTexCoord2d(u1,v1); glVertex2f((float)(x+xS),(float)y);
                    glTexCoord2d(u2,v2); glVertex2f((float)(x+xS),(float)(y+yS));
                    glTexCoord2d(u3,v3); glVertex2f((float)x,(float)(y+yS));
                    glEnd();
                }
            }
            delete[] lva;

            glDisable(GL_BLEND);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
#endif
    }
}

void ncdfOverlayFactory::RenderSeaTempIsoLines(PlugIn_ViewPort *vp)
{
    if (!gui || !vp) return;
    double **sstGrid = gui->gridSST;
    if (!sstGrid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // Auto-detect temperature range from data
    double minT = 1e10, maxT = -1e10;
    for (int j = 0; j < nj; j++) {
        if (!sstGrid[j]) break;
        for (int i = 0; i < ni; i++) {
            double v = sstGrid[j][i];
            if (v == ncdf_NOTDEF || isnan(v) || !isfinite(v)) continue;
            if (v < minT) minT = v;
            if (v > maxT) maxT = v;
        }
    }
    if (minT >= maxT) return;

    double spacing = 2.0;
    minT = floor(minT / spacing) * spacing;
    maxT = ceil(maxT / spacing) * spacing;

    double lat_min = blat, lon_min = tlon;
    double lat_max = tlat, lon_max = blon;
    double incrLat = (lat_max - lat_min) / (nj - 1);
    if (gui->myMessage.jDirectionIncr < 0) incrLat = -incrLat;
    double incrLon = (lon_max - lon_min) / (ni - 1);

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(1.0f);

        for (double temp = minT; temp <= maxT; temp += spacing) {
            IsoLine isoLine(temp, sstGrid, nj, ni, lat_max, lon_min, incrLat, incrLon);
            if (isoLine.getNbSegments() < 1) continue;

            // GRIB pattern: draw segments directly with GL
            glColor4ub(80, 80, 80, 200);
            std::list<Segment*>& trace = isoLine.getTrace();
            glBegin(GL_LINES);
            for (std::list<Segment*>::iterator it = trace.begin(); it != trace.end(); ++it) {
                Segment *seg = *it;
                wxPoint ab, cd;
                GetCanvasPixLL(vp, &ab, seg->py1, seg->px1);
                GetCanvasPixLL(vp, &cd, seg->py2, seg->px2);
                glVertex2i(ab.x, ab.y);
                glVertex2i(cd.x, cd.y);
            }
            glEnd();
        }

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#endif
    } else {
        for (double temp = minT; temp <= maxT; temp += spacing) {
            IsoLine isoLine(temp, sstGrid, nj, ni, lat_max, lon_min, incrLat, incrLon);
            if (isoLine.getNbSegments() < 1) continue;
            isoLine.drawIsoLine(*m_pdc, vp, false, false);
            isoLine.drawIsoLineLabels(m_pdc, wxColour(80, 80, 80), vp, 40, 0, temp);
        }
    }
}


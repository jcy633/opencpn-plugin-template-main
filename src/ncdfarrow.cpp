//===================================================================
// Auto-split from ncdfoverlayfactory.cpp
//===================================================================

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"

#ifndef PI
#define PI 3.14159265
#endif

// Is the given point in the vp ?? (GRIB pattern: handle date line crossing)
bool PointInLLBox(PlugIn_ViewPort *vp, double x, double y)
{
    if (y < vp->lat_min || y > vp->lat_max) return FALSE;
    double m_minx = vp->lon_min;
    double m_maxx = vp->lon_max;
    if (x < m_maxx - 360.0) x += 360.0;
    else if (x > m_minx + 360.0) x -= 360.0;
    if (x >= m_minx && x <= m_maxx) return TRUE;
    return FALSE;
}

void ncdfOverlayFactory::RenderncdfCurrent()
{
    if (!gui || !gui->gridu || !gui->gridv) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // GRIB pattern: pixel-based spacing → geographic spacing
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
    double end_lat = vp->lat_max + lat_sp;
    double end_lon = vp->lon_max + lon_sp;

    wxColour colour;
    GetGlobalColor(_T("UBLCK"), &colour);

    // Set GL state ONCE before the loop (GRIB pattern)
#ifdef ocpnUSE_GL
    if (!m_pdc) {
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(2.0f);
    }
#endif

    int maxIter = 5000;
    int iterCount = 0;

    for (double lat = start_lat; lat <= end_lat && iterCount < maxIter; lat += lat_sp) {
        for (double lon = start_lon; lon <= end_lon && iterCount < maxIter; lon += lon_sp) {
            iterCount++;
            double nlon = lon;
            while (nlon > 180.0) nlon -= 360.0;
            while (nlon < -180.0) nlon += 360.0;
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, nlon, lat, true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, nlon, lat, true);
            if (vx == ncdf_NOTDEF || vy == ncdf_NOTDEF || !isfinite(vx) || !isfinite(vy)) continue;
            double mag = sqrt(vx * vx + vy * vy);
            if (mag < 0.01) continue;
            wxPoint p;
            GetCanvasPixLL(vp, &p, lat, lon);
            // Direction: atan2(east, north) + rotation
            double dir = atan2(vy, -vx) + vp->rotation;
            double scale = wxMax(1.0, mag);
            drawLineBuffer(m_ArrowBuffer.lines, m_ArrowBuffer.count,
                           p.x, p.y, dir, scale, colour);
        }
    }

#ifdef ocpnUSE_GL
    if (!m_pdc) {
        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
    }
#endif
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
			      if (gui->myMessage.ucurr == NULL || gui->myMessage.vcurr == NULL){
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


void ncdfOverlayFactory::ArrowBuffer::pushLine(float x0, float y0, float x1, float y1) {
    buffer.push_back(x0); buffer.push_back(y0);
    buffer.push_back(x1); buffer.push_back(y1);
}

void ncdfOverlayFactory::ArrowBuffer::Finalize() {
    count = (int)(buffer.size() / 4);
    lines = new float[buffer.size()];
    for (size_t i = 0; i < buffer.size(); i++) lines[i] = buffer[i];
    buffer.clear();
}

void ncdfOverlayFactory::drawLineBuffer(float *lines, int count, int x, int y,
                                         double ang, double scale, wxColour colour)
{
    float six = sinf((float)ang), cox = cosf((float)ang);
    float vertexes[40];
    int n = wxMin(count, 10);  // max 10 line segments = 20 floats
    for (int i = 0; i < 2 * n; i++) {
        float *k = lines + 2 * i;
        vertexes[2 * i + 0] = k[0] * cox * scale + k[1] * six * scale + x;
        vertexes[2 * i + 1] = k[0] * six * scale - k[1] * cox * scale + y;
    }

    if (m_pdc) {
        wxPen pen(colour, 2);
        m_pdc->SetPen(pen);
        for (int i = 0; i < n; i++) {
            float *l = vertexes + 4 * i;
            m_pdc->DrawLine(l[0], l[1], l[2], l[3]);
        }
    }
#ifdef ocpnUSE_GL
    else {
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
        glBegin(GL_LINES);
        for (int i = 0; i < n; i++) {
            float *l = vertexes + 4 * i;
            glVertex2f(l[0], l[1]);
            glVertex2f(l[2], l[3]);
        }
        glEnd();
    }
#endif
}

// Legacy drawWaveArrow kept for DC mode compatibility
void ncdfOverlayFactory::drawWaveArrow(int i, int j, double ang, wxColour arrowColor)
{
    drawLineBuffer(m_ArrowBuffer.lines, m_ArrowBuffer.count, i, j,
                   ang * PI / 180.0, 1.0, arrowColor);
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

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

#ifndef ncdfOVERLAYFACTORY_H
#define ncdfOVERLAYFACTORY_H

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#include <wx/dynarray.h>
#include "ocpn_plugin.h"
#include "ncdfdata.h"
#include "wx/graphics.h"
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <vector>
#include "ncdf.h"
#include <map>

using namespace std;

enum OVERLAP {_IN,_ON,_OUT};

OVERLAP Intersect(PlugIn_ViewPort *vp,
       double lat_min, double lat_max, double lon_min, double lon_max, double Marge);
bool PointInLLBox(PlugIn_ViewPort *vp, double x, double y);

class ncdfOverlayFactory
{
public:
            ncdfOverlayFactory();
            ~ncdfOverlayFactory();

			void setData(MainDialog *gui, ncdf_pi *plugin, ncdfDataMessage g2data, int numberOfPoints, wxDouble tlat, wxDouble tlon, wxDouble blat, wxDouble blon);
     void reset();
     void setSelectionRectangle(Selection *rect);
     bool isReadyToRender(){ return m_bReadyToRender; }
     void clearBmp();
	 bool RenderGLncdfOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp);
	 bool RenderncdfOverlay(wxDC &dc, PlugIn_ViewPort *vp);
	 bool DoRenderncdfOverlay(PlugIn_ViewPort *vp);
     void RenderSelectionRectangle();
	 void RenderMyArrows(PlugIn_ViewPort *vp);  
     void RenderncdfCurrent();     
     bool RenderncdfCurrentBmp();

     void drawWaveArrow(int i, int j, double ang, wxColour arrowColor);
     
     void drawTransformedLine( wxPen pen, double si, double co,int di, int dj, int i,int j, int k,int l);
	 void DrawGLLine(double x1, double y1, double x2, double y2, double width, wxColour myColour);
	 void DrawOLBitmap(const wxBitmap &bitmap, wxCoord x, wxCoord y, bool usemask);
	 void DrawAllCurrentsInViewPort(double dlat, double dlon, double ddir, double dfor, wxDC &myDC, PlugIn_ViewPort *myVP);
	 void DrawAllGLCurrentsInViewPort(double dlat, double dlon, double ddir, double dfor, wxGLContext *pcontext, PlugIn_ViewPort *myVP);
	 void drawCurrentArrow(int x, int y, double rot_angle, double scale, double rate, wxDC &dc, PlugIn_ViewPort *vp);
	 wxImage &DrawGLPolygon();
	 void drawGLPolygons(ncdfOverlayFactory *pof, wxDC *dc,	PlugIn_ViewPort *vp, wxImage &imageLabel, double myLat, double myLon, int offset);
	 void DrawGLLabels(ncdfOverlayFactory *pof, wxDC *dc, PlugIn_ViewPort *vp, wxImage &imageLabel, double myLat, double myLon, int offset);
	 wxImage &DrawGLText(double value, int precision);
	 wxImage &DrawGLTextDir(double value, int precision);
	 wxImage &DrawGLTextString(wxString myText);
     void drawPetiteBarbule(wxDC *pmdc, wxPen pen, bool south,
                               double si, double co, int di, int dj, int b);
     void drawGrandeBarbule(wxDC *pmdc, wxPen pen, bool south,
                               double si, double co, int di, int dj, int b);
     void drawTriangle(wxDC *pmdc, wxPen pen, bool south,
			       double si, double co, int di, int dj, int b); 
     
     wxColour GetSeaCurrentGraphicColor(double val_in); 
   
     PlugIn_ViewPort 	*vp;
	 bool 		m_bReadyToRender;
	 bool		renderSelectionRectangle;

	 void SetParentSize(int w, int h) { m_ParentSize.SetWidth(w); m_ParentSize.SetHeight(h); }
	 wxSize  m_ParentSize;
     
private:
	 ncdfDataMessage g2data;
	 int numberOfPoints;
     MainDialog		*gui;
     ncdf_pi        *plugin;
     double 		m_last_vp_scale;
     wxDouble		tlon,tlat,blon,blat;
     wxDouble		incrLon, incrLat;
     wxUint32		sectors, latSectors, lonSectors;
     wxHashTable 	*hash;
     wxFloat32 		*data;
     wxDC		 	*pmdc;
     PlugIn_ViewPort 	*m_last_vp;
     double	 	m_last_vp_latMax;     
     int			space[7];
     wxUint16 		m_space;
     
     wxBitmap		*m_pbm_current;
     Selection		*rect;
	 
	 
	 //  for GL
	 wxColour c_GLcolour;
	 wxPoint p_basic[9];

	 wxDC *m_pdc;
	 wxDC *myDC;

	 std::map < double, wxImage > m_labelCache;
	 std::map < wxString, wxImage > m_labelCacheText;

#if wxUSE_GRAPHICS_CONTEXT
	 wxGraphicsContext *m_gdc;
#endif
	 bool m_hiDefGraphics;

	 // Particle system
	 void *m_ParticleMap;
	 wxTimer m_tParticleTimer;
	 bool m_bUpdateParticles;
	 int m_particleBurstCount;  // Frames of forced particle update after data change
	 void ClearParticles();
	 void RenderParticles(PlugIn_ViewPort *vp);

	 // GL texture cache for color map
	 GLuint m_glColorTexture;
	 bool m_bHasColorTexture;
	 int m_texDataDim[2];
	 int m_texGLDim[2];
	 int m_lvaSize;
	 double (*m_lva)[2][2];
	 void CreateColorTexture(PlugIn_ViewPort *vp);
	 void DrawColorTexture(PlugIn_ViewPort *vp);
	 void DeleteColorTexture();

	 // Sea temperature rendering
	 wxColour GetSeaTempGraphicColor(double temp_c);
	 void RenderSeaTempOverlay(PlugIn_ViewPort *vp);
	 void RenderSeaTempIsoLines(PlugIn_ViewPort *vp);
	 GLuint m_glSeaTempTexture;
	 bool m_bHasSeaTempTexture;
	 int m_sstTexDataDim[2];
	 int m_sstTexGLDim[2];
	 void DeleteSeaTempTexture();
};



#endif // ncdfOVERLAYFACTORY_H

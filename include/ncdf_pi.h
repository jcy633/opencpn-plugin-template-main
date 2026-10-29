/******************************************************************************
 * $Id: ncdf_pi.h,v 1.8 2010/06/21 01:54:37 bdbcat Exp $
 *
 * Project:  OpenCPN
 * Purpose:  ncdf Plugin
 * Author:   David Register, Mike Rossiter
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register   *
 *   bdbcat@yahoo.com   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */

#ifndef _ncdfPI_H_
#define _ncdfPI_H_

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#define     PLUGIN_VERSION_MAJOR    0
#define     PLUGIN_VERSION_MINOR    5

#define     MY_API_VERSION_MAJOR    1
#define     MY_API_VERSION_MINOR    19

#include "ocpn_plugin.h"

#include "ncdfoverlayfactory.h"
#include "ncdf_landmask.h"

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

#define ncdf_TOOL_POSITION    -1          // Request default positioning of toolbar tool

class ncdf_pi : public opencpn_plugin_119
{
public:
      ncdf_pi(void *ppimgr);

//    The required PlugIn Methods
      int Init(void);
      bool DeInit(void);

      int GetAPIVersionMajor();
      int GetAPIVersionMinor();
      int GetPlugInVersionMajor();
      int GetPlugInVersionMinor();
      wxBitmap *GetPlugInBitmap();
      wxString GetCommonName();
      wxString GetShortDescription();
      wxString GetLongDescription();

//    The required override PlugIn Methods
      bool RenderOverlay(wxDC &pmdc, PlugIn_ViewPort *vp);	  
	  bool RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp);
	 
	  bool RenderOverlayArrow(wxDC *dc, PlugIn_ViewPort *vp);
	  bool RenderGLOverlayArrow(wxGLContext *pcontext, PlugIn_ViewPort *vp);
      
	  void SetCursorLatLon(double lat, double lon);
	  void OnToolbarToolCallback(int id);
	  void OnContextMenuItemCallback(int id);
      void SetDefaults(void);
      int GetToolbarToolCount(void);     
      void OnncdfDialogClose();      
      ncdfOverlayFactory *GetncdfOverlayFactory(){ return m_pncdfOverlayFactory; }

      wxString         m_ncdf_dir;
      int			   m_choice;
	  
	  double GetCursorLat(void) { return m_pncdfDialog->m_cursor_lat; }
	  double GetCursorLon(void) { return m_pncdfDialog->m_cursor_lon; }	

	  double dlat, dlon;

	  bool b_showODAS;

	  // Mouse events for feature icon clicks + region pick (P2)
	  bool MouseEventHook(wxMouseEvent &event) override;

      bool              m_bShowCurrentDir;
      bool              m_bShowCurrentForce;
      bool              m_bShowIsoLines;
      bool              m_bShowNumbers;
      bool              m_bShowParticles;
      bool              m_bShowSeaTemp;
      bool              m_bShowSeaTempIso;
      bool              m_bShowSalinity;
      bool              m_bShowZos;
      bool              m_bShowZosIso;
      // Per-data-type rendering settings
      struct InterpSettings {
          bool showIsoLines;    // isolines (SST/salinity)
          bool animate;         // flow-field animation
          InterpSettings() : showIsoLines(false), animate(false) {}
      };
      InterpSettings m_settingsCurrent;
      InterpSettings m_settingsSeaTemp;
      InterpSettings m_settingsSalinity;
      InterpSettings m_settingsZos;
      // Feature detection (plan docs/ocean-feature-detection-plan.md)
      bool              m_bFeatClipMode;    // data layers only inside features
      bool              m_bFeatFront;
      bool              m_bFeatEddy;
      bool              m_bFeatJet;
      bool              m_bFeatConverg;
      bool              m_bFeatShear;
      // 黑潮主轴（流线约束最大流速法）
      bool              m_bShowKuroshio;    // 显示主轴
      bool              m_bShowKuroshioBdy; // 显示左右边界
      // SEAMT 急流核心提取（Udalov2026，黑潮下游）
      bool              m_bShowSeamt;
      bool              m_bShowSeamtSubjets;
      int               m_iSeamtPreset;     // 0=日本以南 1=论文原域 2=自定义
      double            m_seamtLonIn, m_seamtLonOut;
      int               m_seamtNpts;
      double            m_seamtHalfWidth;
      wxString          m_etopoPath;        // ETOPO 2022 高分辨率陆海掩膜 (NetCDF)
      // 陆海掩膜：主层（精细，通常 30"/15"）+ 回退层（粗，通常全球 1'）。
      // 一律位打包，全球 1' 约 29 MB；首次从 NetCDF 建好后写 .bin 缓存，
      // 之后启动直接读缓存，切换文件零开销。
      NcdfLandMask      m_landMask;
      NcdfLandMask      m_landMaskCoarse;
      bool              loadLandMaskFiles(bool force = false);
      int               m_iFeatSensitivity; // 0..100 slider (higher = more detections)
      int               m_iFeatROIType;     // 0=中国周边 1=亚洲海域 2=全球 3=自定义
      double            m_featROILatMin, m_featROILatMax;
      double            m_featROILonMin, m_featROILonMax;
      int               m_iOverlayTransparency;
      int               m_iArrowSpacing;
      int               m_iNumberSpacing;
      int               m_iColorMapType;
      int               m_iPlaySpeed;
      bool              m_bLoopMode;
      int               m_iLoopStartPoint;
      int               m_iParticleDensity;

private:
      bool LoadConfig(void);
      bool SaveConfig(void);

	  int              m_position_menu_id;

      wxFileConfig     *m_pconfig;
      wxWindow         *m_parent_window;

      MainDialog		*m_pncdfDialog;
      ncdfOverlayFactory *m_pncdfOverlayFactory;

      int              m_display_width, m_display_height;
      int              m_leftclick_tool_id;

      int              m_ncdf_dialog_x, m_ncdf_dialog_y;
      int              m_ncdf_dialog_sx, m_ncdf_dialog_sy;
	  

      bool              m_bncdfUseHiDef;
      bool              m_bncdfShowIcon;


      //    Controls added to Preferences panel
      wxCheckBox              *m_pncdfShowIcon;
      wxCheckBox              *m_pncdfUseHiDef;

};

#endif




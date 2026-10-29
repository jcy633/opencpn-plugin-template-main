/******************************************************************************
 * $Id: ncdf_pi.cpp,v 1.8 2010/06/21 01:54:37 bdbcat Exp $
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


#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers


#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <chrono>

#include "ncdf_pi.h"
#include "ncdf.h"

extern void ncdfLog(const char* format, ...);

// Crash dump handler — generates minidump on access violation
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static LONG WINAPI NcdfCrashHandler(EXCEPTION_POINTERS* pExInfo) {
    HANDLE hFile = CreateFileA(
        "C:\\ProgramData\\opencpn\\ncdf_crash.dmp",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pExInfo;
        mdei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
            hFile, MiniDumpWithDataSegs, &mdei, NULL, NULL);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}



// the class factories, used to create and destroy instances of the PlugIn

extern "C" DECL_EXP opencpn_plugin* create_pi(void *ppimgr)
{
    return new ncdf_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}


//---------------------------------------------------------------------------------------------------------
//
//    ncdf PlugIn Implementation
//
//---------------------------------------------------------------------------------------------------------

#include "icons2.h"

//---------------------------------------------------------------------------------------------------------
//
//          PlugIn initialization and de-init
//
//---------------------------------------------------------------------------------------------------------

ncdf_pi::ncdf_pi(void *ppimgr)
      :opencpn_plugin_119(ppimgr)
{
      // Create the PlugIn icons
	initialize_images();
}

int ncdf_pi::Init(void)
{
      SetUnhandledExceptionFilter(NcdfCrashHandler);

      AddLocaleCatalog(_T("opencpn-ncdf_pi"));
      // Set some default private member parameters
      m_ncdf_dialog_x = 0;
      m_ncdf_dialog_y = 0;
      m_ncdf_dialog_sx = 270;
      m_ncdf_dialog_sy = 460;
      m_pncdfDialog = NULL;
      m_pncdfOverlayFactory = NULL;
	  ::wxDisplaySize(&m_display_width, &m_display_height);
	  m_choice = 0;
	  b_showODAS = false;
	  m_bShowCurrentDir = true;
	  m_bShowCurrentForce = true;
	  m_bShowParticles = false;
	  m_bShowSeaTemp = false;
	  m_bShowSeaTempIso = false;
	  m_bShowSalinity = false;
	  m_bShowZos = false;
	  m_bShowZosIso = false;
	  m_bFeatClipMode = false;
	  m_bFeatFront = false;
	  m_bFeatEddy = false;
	  m_bFeatJet = false;
	  m_bFeatConverg = false;
	  m_bFeatShear = false;
	  m_bShowKuroshio = true;
	  m_bShowKuroshioBdy = true;
	  m_iFeatSensitivity = 60;
	  m_iFeatROIType = 0;
	  m_featROILatMin = 15; m_featROILatMax = 45;
	  m_featROILonMin = 100; m_featROILonMax = 135;
	  // Per-type settings default to all off
	  m_iParticleDensity = 5;

      //    Get a pointer to the opencpn configuration object
      m_pconfig = GetOCPNConfigObject();

      //    And load the configuration items
      LoadConfig();
      // Land mask is not loaded here: it follows the panel being shown.

      // Get a pointer to the opencpn display canvas, to use as a parent for the ncdf dialog
      m_parent_window = GetOCPNCanvasWindow();

      //    This PlugIn needs a toolbar icon, so request its insertion if enabled locally
      if(m_bncdfShowIcon)
            m_leftclick_tool_id  = InsertPlugInTool(_T(""), _img_ncdf, _img_ncdf, wxITEM_NORMAL,
                  _("ncdf"), _T(""), NULL,
                   ncdf_TOOL_POSITION, 0, this);

      // Create the drawing factory

      m_pncdfOverlayFactory = new ncdfOverlayFactory;
	  m_pncdfOverlayFactory->m_bReadyToRender=false;
	  

	  wxMenu dummy_menu;
	  m_position_menu_id = AddCanvasContextMenuItem
		  (new wxMenuItem(&dummy_menu, -1, _("Drop Tidal Arrow")), this);
	  SetCanvasContextMenuItemViz(m_position_menu_id, true);

      // 自动化测试：存在请求文件时自动打开面板。面板构造完成后会跑 SEAMT 自测
      // （见 MainDialog::maybeRunSeamtAutoTest）。这样外部脚本只需
      // 「写请求文件 -> 启动 OpenCPN -> 等 -> 读结果文件」，不必手动点界面。
      if (wxFileExists(_T("C:\\ProgramData\\opencpn\\ncdf_seamt_auto.txt"))) {
          ncdfLog("[seamtauto] request file found at plugin init -> opening panel\n");
          OnToolbarToolCallback(0);
      }

      return (WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_CURSOR_LATLON       |
           WANTS_TOOLBAR_CALLBACK    |
           INSTALLS_TOOLBAR_TOOL     |
           WANTS_MOUSE_EVENTS        |
           WANTS_CONFIG              
           //WANTS_PREFERENCES
            );
}

bool ncdf_pi::MouseEventHook(wxMouseEvent &event)
{
    if (event.LeftUp() || event.LeftDown())
        ncdfLog("[mouse] MouseEventHook: %s at (%d,%d) dialog=%p\n",
            event.LeftDown() ? "down" : "up", event.GetX(), event.GetY(), (void*)m_pncdfDialog);
    if (m_pncdfDialog) return m_pncdfDialog->onCanvasMouse(event);
    return false;
}

bool ncdf_pi::DeInit(void)
{
      if(m_pncdfDialog)
      {
		 m_pncdfOverlayFactory->m_bReadyToRender = false;
		 m_pncdfDialog->Close();
		 delete m_pncdfDialog;
		 m_pncdfDialog = NULL;
      }
     
	delete m_pncdfOverlayFactory;
	m_pncdfOverlayFactory = NULL;
    return true;
}

int ncdf_pi::GetAPIVersionMajor()
{
      return MY_API_VERSION_MAJOR;
}

int ncdf_pi::GetAPIVersionMinor()
{
      return MY_API_VERSION_MINOR;
}

int ncdf_pi::GetPlugInVersionMajor()
{
      return PLUGIN_VERSION_MAJOR;
}

int ncdf_pi::GetPlugInVersionMinor()
{
      return PLUGIN_VERSION_MINOR;
}

wxBitmap *ncdf_pi::GetPlugInBitmap()
{
      return _img_ncdf_pi;
}

wxString ncdf_pi::GetCommonName()
{
      return _("ncdf");
}


wxString ncdf_pi::GetShortDescription()
{
      return _("ncdf PlugIn for OpenCPN");
}


wxString ncdf_pi::GetLongDescription()
{
      return _("ncdf PlugIn for OpenCPN\n\
For surface current direction and speed.");

}


void ncdf_pi::SetDefaults(void)
{
      // If the config somehow says NOT to show the icon, override it so the user gets good feedback
      if(!m_bncdfShowIcon)
      {
            m_bncdfShowIcon = true;

            m_leftclick_tool_id  = InsertPlugInTool(_T(""), _img_ncdf, _img_ncdf, wxITEM_NORMAL,
                  _("ncdf"), _T(""), NULL,
                   ncdf_TOOL_POSITION, 0, this);
      }
}

void ncdf_pi::OnContextMenuItemCallback(int id)
{
	if (!m_pncdfDialog)
		return;

	if (id == m_position_menu_id) {
		m_pncdfDialog->m_cursor_lat = GetCursorLat();
		m_pncdfDialog->m_cursor_lon = GetCursorLon();

		wxString myLat = wxString::Format(wxT("%5.2f"), (double)m_pncdfDialog->m_cursor_lat);

		if (m_pncdfDialog) {
			m_pncdfDialog->OnContextMenu(m_pncdfDialog->m_cursor_lat, m_pncdfDialog->m_cursor_lon);
		}
	}
}
int ncdf_pi::GetToolbarToolCount(void)
{
      return 1;
}

void ncdf_pi::OnToolbarToolCallback(int id)
{
      if(NULL == m_pncdfDialog)
      {
        m_pncdfDialog = new MainDialog(m_parent_window);
	    m_pncdfDialog->setPlugIn(this);
      }
      // Land mask follows panel visibility: opened -> load (idempotent).
      {
          auto t0 = std::chrono::steady_clock::now();
          bool ok = m_pncdfDialog->ensureLandMaskVisible();
          long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
          ncdfLog("[landmask] panel show -> %s in %lld ms\n", ok ? "loaded" : "not available", ms);
      }
      m_pncdfDialog->Show();                        // Show modeless, so it stays on the screen
	  
}

void ncdf_pi::OnncdfDialogClose()
{
	  m_pncdfDialog->Hide();
      if(m_pncdfOverlayFactory)
      {
        m_pncdfOverlayFactory->reset();
	    RequestRefresh(m_parent_window);
      }
      SaveConfig();     
}

bool ncdf_pi::RenderOverlay(wxDC &pmdc, PlugIn_ViewPort *vp)
{    
	
	if (b_showODAS == true){
		RenderOverlayArrow(&pmdc, vp);
		return true;	
	}
		
    if(m_pncdfDialog && m_pncdfOverlayFactory)
    {
            if(m_pncdfOverlayFactory->isReadyToRender())
            {
				m_pncdfOverlayFactory->SetParentSize(m_display_width, m_display_height);
				m_pncdfOverlayFactory->RenderncdfOverlay ( pmdc, vp );
                return true;
            }
            else
                return false;
    }
    else
            return false;
	
}

bool ncdf_pi::RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
	if (b_showODAS == true){
		RenderGLOverlayArrow(pcontext, vp);
		return true;
	}

	static int s_glDbg = 0;
	if (s_glDbg < 10) {
		ncdfLog("[render] RenderGLOverlay: dialog=%p factory=%p ready=%d\n",
			(void*)m_pncdfDialog, (void*)m_pncdfOverlayFactory,
			m_pncdfOverlayFactory ? (int)m_pncdfOverlayFactory->isReadyToRender() : -1);
		s_glDbg++;
	}

	if (m_pncdfDialog && m_pncdfOverlayFactory)
	{
		if (m_pncdfOverlayFactory->isReadyToRender())
		{
			m_pncdfOverlayFactory->SetParentSize(m_display_width, m_display_height);
			m_pncdfOverlayFactory->RenderGLncdfOverlay(pcontext, vp);
			return true;
		}
		else
			return false;
	}
	else
		return false;
}

bool ncdf_pi::RenderOverlayArrow(wxDC *dc, PlugIn_ViewPort *vp)
{
	if (!m_pncdfDialog || !m_pncdfDialog->IsShown())
		return false;

	if (m_pncdfDialog && m_pncdfOverlayFactory)
	{
		for (std::list<Arrow*>::iterator it = m_pncdfDialog->m_ArrowList.begin();
			it != m_pncdfDialog->m_ArrowList.end(); it++){

			double myLat = (*it)->m_lat;
			double myLon = (*it)->m_lon;
			CurrentData data = m_pncdfDialog->getCurrentData(myLat, myLon);
			m_pncdfOverlayFactory->DrawAllCurrentsInViewPort(myLat, myLon, data.dir, data.force, *dc, vp);
		}
	}

	return true;

}

bool ncdf_pi::RenderGLOverlayArrow(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
	if (!m_pncdfDialog || !m_pncdfDialog->IsShown())
		return false;
	if (m_pncdfDialog && m_pncdfOverlayFactory)
	{
		for (std::list<Arrow*>::iterator it = m_pncdfDialog->m_ArrowList.begin();
			it != m_pncdfDialog->m_ArrowList.end(); it++){

			double myLat = (*it)->m_lat;
			double myLon = (*it)->m_lon;
			CurrentData data = m_pncdfDialog->getCurrentData(myLat, myLon);
			m_pncdfOverlayFactory->DrawAllGLCurrentsInViewPort(myLat, myLon, data.dir, data.force, pcontext, vp);
		}
	}

	return true;

}
void ncdf_pi::SetCursorLatLon(double lat, double lon)
{
      if(m_pncdfDialog)
      {
            m_pncdfDialog->SetCursorLatLon(lat, lon);
      }
      
}


// ---------------------------------------------------------------------------
// ETOPO 2022 高分辨率陆海掩膜（论文 Methods："Land masking was applied using
// the Earth Topography 2022 (ETOPO 2022) global DEM dataset at a 30-arc-second
// resolution."）
//
// 掩膜文件放在 <userDataDir>/etopo/ 下（文件名含 etopo），最多两个：
//   精细层：30"/15"（黑潮算法用；流区显示也用它）
//   粗层  ：全球 1'（流区之外的显示回退）
// 都是一次性读入 + 位打包 + .bin 缓存，之后切文件零开销。
// ---------------------------------------------------------------------------
static bool loadLayerWithCache(NcdfLandMask &m, const wxString &ncPath, bool force)
{
    // 直接给到 .bin（预建缓存）时跳过 NetCDF
    if (ncPath.Lower().EndsWith(_T(".bin")))
        return m.loadCache(ncPath);
    wxString cache = ncPath + _T(".landmask.bin");
    if (!force && m.loadCache(cache)) return true;
    if (!m.load(ncPath, -90.0, 90.0, -180.0, 180.0)) return false;
    if (m.saveCache(cache))
        ncdfLog("[landmask] cache written: %s\n", (const char*)cache.mb_str());
    return true;
}

bool ncdf_pi::loadLandMaskFiles(bool force)
{
    if (!force && m_landMask.valid()) return true;

    wxArrayString cands;
    wxString sep = wxFileName::GetPathSeparator();
    // OpenCPN 的数据分散在几个地方（ProgramData / Local / Roaming），都搜一遍
    wxArrayString roots;
    {
        wxString e;
        if (wxGetEnv(_T("LOCALAPPDATA"), &e) && !e.IsEmpty()) roots.Add(e + sep + _T("opencpn"));
        if (wxGetEnv(_T("APPDATA"), &e) && !e.IsEmpty())       roots.Add(e + sep + _T("opencpn"));
        if (wxGetEnv(_T("PROGRAMDATA"), &e) && !e.IsEmpty())   roots.Add(e + sep + _T("opencpn"));
        roots.Add(wxStandardPathsBase::Get().GetUserDataDir());
        roots.Add(wxStandardPathsBase::Get().GetConfigDir());
    }
    for (size_t r = 0; r < roots.GetCount(); r++) {
        // 优先插件自己的数据目录（与其他插件资源放在一起），其次 etopo 子目录
        wxString dirs[4] = {
            roots[r] + sep + _T("plugins") + sep + _T("ncdf_pi") + sep + _T("data"),
            roots[r] + sep + _T("etopo"),
            roots[r] + sep + _T("plugins") + sep + _T("ncdf_pi"),
            roots[r]
        };
        for (int d = 0; d < 4; d++) {
            if (dirs[d].IsEmpty() || !wxDirExists(dirs[d])) continue;
            wxDir::GetAllFiles(dirs[d], &cands, _T("*etopo*.nc"), wxDIR_FILES);
            wxDir::GetAllFiles(dirs[d], &cands, _T("*etopo*.nc4"), wxDIR_FILES);
            wxDir::GetAllFiles(dirs[d], &cands, _T("*etopo*.bin"), wxDIR_FILES);
        }
    }
    if (cands.GetCount() == 0) {
        ncdfLog("[landmask] no ETOPO file found; using data validity as land mask\n");
        return false;
    }

    // 名字含 30s/15s 的排前面当精细层；含 60s/1min 的当粗层
    auto rank = [](const wxString &p) {
        wxString n = p.Lower();
        if (n.Find(_T("15s")) != wxNOT_FOUND || n.Find(_T("30s")) != wxNOT_FOUND ||
            n.Find(_T("15sec")) != wxNOT_FOUND) return 0;
        if (n.Find(_T("60s")) != wxNOT_FOUND || n.Find(_T("1min")) != wxNOT_FOUND) return 2;
        return 1;
    };
    for (size_t a = 0; a + 1 < cands.GetCount(); a++)
        for (size_t b = a + 1; b < cands.GetCount(); b++)
            if (rank(cands[b]) < rank(cands[a])) { wxString t = cands[a]; cands[a] = cands[b]; cands[b] = t; }

    // 去重（不同搜索目录可能命中同一个文件，或同一份数据被复制到多处）
    for (size_t a = 0; a < cands.GetCount(); a++)
        for (size_t b = a + 1; b < cands.GetCount(); ) {
            wxString fa = cands[a], fb = cands[b];
            bool same = (fa == fb);
            if (!same && wxFileName::FileExists(fa) && wxFileName::FileExists(fb))
                same = (wxFileName::GetSize(fa) == wxFileName::GetSize(fb)) &&
                       (fa.AfterLast(sep[0]) == fb.AfterLast(sep[0]));
            if (same) cands.RemoveAt(b); else b++;
        }

    m_landMask.clear();
    m_landMaskCoarse.clear();

    bool ok = loadLayerWithCache(m_landMask, cands[0], force);
    if (cands.GetCount() > 1)
        loadLayerWithCache(m_landMaskCoarse, cands[1], force);

    m_landMask.setFallback(m_landMaskCoarse.valid() ? &m_landMaskCoarse : NULL);
    m_etopoPath = cands[0];

    if (ok) {
        ncdfLog("[landmask] primary : %dx%d  %.1f'  lat %.2f..%.2f lon %.2f..%.2f\n",
                m_landMask.nlat(), m_landMask.nlon(), m_landMask.dLat() * 60.0,
                m_landMask.latMin(), m_landMask.latMax(), m_landMask.lonMin(), m_landMask.lonMax());
        if (m_landMaskCoarse.valid())
            ncdfLog("[landmask] fallback: %dx%d  %.1f'  lat %.2f..%.2f lon %.2f..%.2f\n",
                    m_landMaskCoarse.nlat(), m_landMaskCoarse.nlon(), m_landMaskCoarse.dLat() * 60.0,
                    m_landMaskCoarse.latMin(), m_landMaskCoarse.latMax(),
                    m_landMaskCoarse.lonMin(), m_landMaskCoarse.lonMax());
        // 掩膜刚载入（或换了一份），已建好的陆地图层作废，下次渲染时重建
        InvalidateLandLayers();
    }
    return ok;
}

bool ncdf_pi::LoadConfig(void)
{
      wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

      if(pConf)
      {
            pConf->SetPath ( _T( "/Settings" ) );
            pConf->Read ( _T( "ncdfUseHiDef" ),  &m_bncdfUseHiDef, 0 );
            pConf->Read ( _T( "ShowncdfIcon" ),  &m_bncdfShowIcon, 1 );
            pConf->Read ( _T( "ShowCurrentDir" ), &m_bShowCurrentDir, 1 );
            pConf->Read ( _T( "ShowCurrentForce" ), &m_bShowCurrentForce, 1 );
            pConf->Read ( _T( "ShowParticles" ), &m_bShowParticles, 0 );
            pConf->Read ( _T( "ShowSeaTemp" ), &m_bShowSeaTemp, 0 );
            pConf->Read ( _T( "ShowSeaTempIso" ), &m_bShowSeaTempIso, 0 );
            pConf->Read ( _T( "ShowSalinity" ), &m_bShowSalinity, 0 );
            pConf->Read ( _T( "ShowZos" ), &m_bShowZos, 0 );
            pConf->Read ( _T( "ZosIso" ), &m_bShowZosIso, 0 );
            pConf->Read ( _T( "Animate" ), &m_settingsCurrent.animate, 0 );
            pConf->Read ( _T( "SeaTempIso" ), &m_settingsSeaTemp.showIsoLines, 0 );
            pConf->Read ( _T( "SalinityIso" ), &m_settingsSalinity.showIsoLines, 0 );
            pConf->Read ( _T( "ZosAnimate" ), &m_settingsZos.animate, 0 );
            pConf->Read ( _T( "FeatClipMode" ), &m_bFeatClipMode, 0 );
            pConf->Read ( _T( "FeatFront" ), &m_bFeatFront, 0 );
            pConf->Read ( _T( "FeatEddy" ), &m_bFeatEddy, 0 );
            pConf->Read ( _T( "FeatJet" ), &m_bFeatJet, 0 );
            pConf->Read ( _T( "FeatConverg" ), &m_bFeatConverg, 0 );
            pConf->Read ( _T( "FeatShear" ), &m_bFeatShear, 0 );
            pConf->Read ( _T( "ShowKuroshio" ), &m_bShowKuroshio, 1 );
            pConf->Read ( _T( "ShowKuroshioBdy" ), &m_bShowKuroshioBdy, 1 );
            pConf->Read ( _T( "ShowSeamt" ), &m_bShowSeamt, 1 );
            pConf->Read ( _T( "ShowSeamtSubjets" ), &m_bShowSeamtSubjets, 0 );
            pConf->Read ( _T( "SeamtPreset" ), &m_iSeamtPreset, 0 );
            pConf->Read ( _T( "SeamtLonIn" ), &m_seamtLonIn, 131.0 );
            pConf->Read ( _T( "SeamtLonOut" ), &m_seamtLonOut, 142.0 );
            pConf->Read ( _T( "SeamtNpts" ), &m_seamtNpts, 281 );
            pConf->Read ( _T( "SeamtHalfWidth" ), &m_seamtHalfWidth, 0.70 );
            pConf->Read ( _T( "FeatSensitivity" ), &m_iFeatSensitivity, 60 );
            pConf->Read ( _T( "FeatROIType" ), &m_iFeatROIType, 0 );
            pConf->Read ( _T( "FeatROILatMin" ), &m_featROILatMin, 15.0 );
            pConf->Read ( _T( "FeatROILatMax" ), &m_featROILatMax, 45.0 );
            pConf->Read ( _T( "FeatROILonMin" ), &m_featROILonMin, 100.0 );
            pConf->Read ( _T( "FeatROILonMax" ), &m_featROILonMax, 135.0 );
            pConf->Read ( _T( "ParticleDensity" ), &m_iParticleDensity, 5 );


            m_ncdf_dialog_sx = pConf->Read ( _T ( "ncdfDialogSizeX" ), 270L );
            m_ncdf_dialog_sy = pConf->Read ( _T ( "ncdfDialogSizeY" ), 460L );
            m_ncdf_dialog_x =  pConf->Read ( _T ( "GRI2BDialogPosX" ), 20L );
            m_ncdf_dialog_y =  pConf->Read ( _T ( "ncdfDialogPosY" ), 20L );

            if((m_ncdf_dialog_x < 0) || (m_ncdf_dialog_x > m_display_width))
                  m_ncdf_dialog_x = 5;
            if((m_ncdf_dialog_y < 0) || (m_ncdf_dialog_y > m_display_height))
                  m_ncdf_dialog_y = 5;

			pConf->Read(_T("AreaChoice"), &m_choice, 0);

            pConf->SetPath ( _T ( "/Directories" ) );
			wxStandardPathsBase &std = wxStandardPathsBase::Get(); 
			pConf->Read ( _T ( "ncdfDirectory" ), &m_ncdf_dir, std.GetDocumentsDir() );

            return true;
      }
      else
            return false;
}

bool ncdf_pi::SaveConfig(void)
{
      wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

      if(pConf)
      {
            pConf->SetPath ( _T ( "/Settings" ) );

            pConf->Write ( _T ( "ncdfUseHiDef" ), m_bncdfUseHiDef );
            pConf->Write ( _T ( "ShowncdfIcon" ), m_bncdfShowIcon );
            pConf->Write ( _T ( "ShowCurrentDir" ), m_bShowCurrentDir );
            pConf->Write ( _T ( "ShowCurrentForce" ), m_bShowCurrentForce );
            pConf->Write ( _T ( "ShowParticles" ), m_bShowParticles );
            pConf->Write ( _T ( "ShowSeaTemp" ), m_bShowSeaTemp );
            pConf->Write ( _T ( "ShowSeaTempIso" ), m_bShowSeaTempIso );
            pConf->Write ( _T ( "ShowSalinity" ), m_bShowSalinity );
            pConf->Write ( _T ( "ShowZos" ), m_bShowZos );
            pConf->Write ( _T ( "ZosIso" ), m_bShowZosIso );
            pConf->Write ( _T ( "Animate" ), m_settingsCurrent.animate );
            pConf->Write ( _T ( "SeaTempIso" ), m_settingsSeaTemp.showIsoLines );
            pConf->Write ( _T ( "SalinityIso" ), m_settingsSalinity.showIsoLines );
            pConf->Write ( _T ( "ZosAnimate" ), m_settingsZos.animate );
            pConf->Write ( _T ( "FeatClipMode" ), m_bFeatClipMode );
            pConf->Write ( _T ( "FeatFront" ), m_bFeatFront );
            pConf->Write ( _T ( "FeatEddy" ), m_bFeatEddy );
            pConf->Write ( _T ( "FeatJet" ), m_bFeatJet );
            pConf->Write ( _T ( "FeatConverg" ), m_bFeatConverg );
            pConf->Write ( _T ( "FeatShear" ), m_bFeatShear );
            pConf->Write ( _T ( "ShowKuroshio" ), m_bShowKuroshio );
            pConf->Write ( _T ( "ShowKuroshioBdy" ), m_bShowKuroshioBdy );
            pConf->Write ( _T ( "ShowSeamt" ), m_bShowSeamt );
            pConf->Write ( _T ( "ShowSeamtSubjets" ), m_bShowSeamtSubjets );
            pConf->Write ( _T ( "SeamtPreset" ), m_iSeamtPreset );
            pConf->Write ( _T ( "SeamtLonIn" ), m_seamtLonIn );
            pConf->Write ( _T ( "SeamtLonOut" ), m_seamtLonOut );
            pConf->Write ( _T ( "SeamtNpts" ), m_seamtNpts );
            pConf->Write ( _T ( "SeamtHalfWidth" ), m_seamtHalfWidth );
            pConf->Write ( _T ( "FeatSensitivity" ), m_iFeatSensitivity );
            pConf->Write ( _T ( "FeatROIType" ), m_iFeatROIType );
            pConf->Write ( _T ( "FeatROILatMin" ), m_featROILatMin );
            pConf->Write ( _T ( "FeatROILatMax" ), m_featROILatMax );
            pConf->Write ( _T ( "FeatROILonMin" ), m_featROILonMin );
            pConf->Write ( _T ( "FeatROILonMax" ), m_featROILonMax );
            pConf->Write ( _T ( "ParticleDensity" ), m_iParticleDensity );

            pConf->Write ( _T ( "ncdfDialogSizeX" ),  m_ncdf_dialog_sx );
            pConf->Write ( _T ( "ncdfDialogSizeY" ),  m_ncdf_dialog_sy );
            pConf->Write ( _T ( "ncdfDialogPosX" ),   m_ncdf_dialog_x );
            pConf->Write ( _T ( "ncdfDialogPosY" ),   m_ncdf_dialog_y );

			pConf->Write(_T("AreaChoice"), m_choice);

            pConf->SetPath ( _T ( "/Directories" ) );
            pConf->Write ( _T ( "ncdfDirectory" ), m_ncdf_dir );

            return true;
      }
      else
            return false;
}

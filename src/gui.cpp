///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "gui.h"
#include "folder.xpm"

///////////////////////////////////////////////////////////////////////////

ncdfDialog::ncdfDialog( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, wxSize(420, 650), style )
{
	this->SetSizeHints( wxSize(640,800), wxDefaultSize );

	wxFlexGridSizer* fgSizer3;
	fgSizer3 = new wxFlexGridSizer( 1, 1, 0, 0 );
	fgSizer3->AddGrowableRow( 0 );
	fgSizer3->AddGrowableCol( 0 );
	fgSizer3->SetFlexibleDirection( wxBOTH );
	fgSizer3->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_notebook1 = new wxNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	m_panel1 = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel1->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_3DLIGHT ) );

	wxFlexGridSizer* fgSizer1;
	fgSizer1 = new wxFlexGridSizer( 4, 1, 0, 0 );
	fgSizer1->AddGrowableRow( 1 );
	fgSizer1->AddGrowableCol( 0 );
	fgSizer1->SetFlexibleDirection( wxBOTH );
	fgSizer1->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	wxBoxSizer* bSizer2;
	bSizer2 = new wxBoxSizer( wxHORIZONTAL );

	m_textCtrlDir = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY );
	m_textCtrlDir->SetMinSize( wxSize( 180,-1 ) );

	bSizer2->Add( m_textCtrlDir, 0, wxALL, 5 );

	m_fileButton = new wxBitmapButton( m_panel1, wxID_ANY, wxBitmap(openfile), wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );

	bSizer2->Add( m_fileButton, 0, wxALL, 5 );


	fgSizer1->Add( bSizer2, 0, wxEXPAND, 5 );

	m_treeCtrl = new wxTreeCtrl( m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE|wxHSCROLL|wxBORDER_SUNKEN );
	m_treeCtrl->SetFont( wxFont( 12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Sans") ) );
	m_treeCtrl->SetMinSize( wxSize( -1,250 ) );

	fgSizer1->Add( m_treeCtrl, 1, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer10;
	bSizer10 = new wxBoxSizer( wxVERTICAL );

	m_staticTextDateTime = new wxStaticText( m_panel1, wxID_ANY, _("日期/时间"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextDateTime->Wrap( -1 );
	bSizer10->Add( m_staticTextDateTime, 1, wxALIGN_CENTER_HORIZONTAL|wxALL, 5 );


	fgSizer1->Add( bSizer10, 0, wxEXPAND, 5 );

	wxFlexGridSizer* fgSizer7;
	fgSizer7 = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer7->SetFlexibleDirection( wxBOTH );
	fgSizer7->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	wxBoxSizer* bSizer5;
	bSizer5 = new wxBoxSizer( wxHORIZONTAL );


	bSizer5->Add( 24, 0, 1, wxEXPAND, 5 );

	m_bpPlay = new wxBitmapButton( m_panel1, wxID_ANY, wxBitmap(play), wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer5->Add( m_bpPlay, 0, wxALL|wxALIGN_CENTER_VERTICAL, 5 );


	fgSizer7->Add( bSizer5, 1, wxEXPAND, 5 );


	fgSizer1->Add( fgSizer7, 0, wxEXPAND, 5 );

	m_staticline1 = new wxStaticLine( m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	fgSizer1->Add( m_staticline1, 0, wxEXPAND | wxALL, 5 );

	wxFlexGridSizer* fgSizer2;
	fgSizer2 = new wxFlexGridSizer( 5, 1, 0, 0 );
	fgSizer2->SetFlexibleDirection( wxBOTH );
	fgSizer2->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	// --- Data overlay rows (aligned grid layout) ---
	// All three rows: [indent 24px] [checkbox] [label 80px] [value 75px] [unit 30px]

	// Row 1: Current color map
	wxFlexGridSizer* bSizerCurrent;
	bSizerCurrent = new wxFlexGridSizer( 1, 5, 0, 0 );
	bSizerCurrent->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxBmpCurrentForce = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString );
	bSizerCurrent->Add( m_checkBoxBmpCurrentForce, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticText40 = new wxStaticText( m_panel1, wxID_ANY, _("海流") );
	m_staticText40->SetMinSize( wxSize( 80,-1 ) );
	bSizerCurrent->Add( m_staticText40, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlCurrentForce = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_CENTRE );
	bSizerCurrent->Add( m_textCtrlCurrentForce, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticText41 = new wxStaticText( m_panel1, wxID_ANY, _("m/s") );
	m_staticText41->SetMinSize( wxSize( 30,-1 ) );
	bSizerCurrent->Add( m_staticText41, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );
	fgSizer2->Add( bSizerCurrent, 0, 0, 5 );

	// Row 2: Sea Temperature
	wxFlexGridSizer* bSizerSeaTemp;
	bSizerSeaTemp = new wxFlexGridSizer( 1, 5, 0, 0 );
	bSizerSeaTemp->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxSeaTemp = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString );
	bSizerSeaTemp->Add( m_checkBoxSeaTemp, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSeaTemp = new wxStaticText( m_panel1, wxID_ANY, _("海温") );
	m_staticTextSeaTemp->SetMinSize( wxSize( 80,-1 ) );
	bSizerSeaTemp->Add( m_staticTextSeaTemp, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlSeaTemp = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_CENTRE );
	bSizerSeaTemp->Add( m_textCtrlSeaTemp, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSeaTempUnit = new wxStaticText( m_panel1, wxID_ANY, wxString::FromUTF8("\xc2\xb0""C") );
	m_staticTextSeaTempUnit->SetMinSize( wxSize( 30,-1 ) );
	bSizerSeaTemp->Add( m_staticTextSeaTempUnit, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );
	fgSizer2->Add( bSizerSeaTemp, 0, 0, 5 );

	// Row 3: Salinity
	wxFlexGridSizer* bSizerSalinity;
	bSizerSalinity = new wxFlexGridSizer( 1, 4, 0, 0 );
	bSizerSalinity->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxSalinity = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString );
	bSizerSalinity->Add( m_checkBoxSalinity, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSalinity = new wxStaticText( m_panel1, wxID_ANY, _("盐度") );
	m_staticTextSalinity->SetMinSize( wxSize( 80,-1 ) );
	bSizerSalinity->Add( m_staticTextSalinity, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlSalinity = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_CENTRE );
	bSizerSalinity->Add( m_textCtrlSalinity, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	fgSizer2->Add( bSizerSalinity, 0, 0, 5 );

	// Row 4: Sea Surface Height (Zos)
	wxFlexGridSizer* bSizerZos;
	bSizerZos = new wxFlexGridSizer( 1, 5, 0, 0 );
	bSizerZos->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxZos = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString );
	bSizerZos->Add( m_checkBoxZos, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextZos = new wxStaticText( m_panel1, wxID_ANY, _("海面高度") );
	m_staticTextZos->SetMinSize( wxSize( 80,-1 ) );
	bSizerZos->Add( m_staticTextZos, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlZos = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_CENTRE );
	bSizerZos->Add( m_textCtrlZos, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextZosUnit = new wxStaticText( m_panel1, wxID_ANY, _("m") );
	m_staticTextZosUnit->SetMinSize( wxSize( 30,-1 ) );
	bSizerZos->Add( m_staticTextZosUnit, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );
	fgSizer2->Add( bSizerZos, 0, 0, 5 );

	// Numbers checkbox (hidden - feature removed)
	m_checkBoxNumbers = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString );
	m_staticTextNumbers = new wxStaticText( m_panel1, wxID_ANY, _("数值") );
	m_checkBoxNumbers->Hide();
	m_staticTextNumbers->Hide();


	fgSizer1->Add( fgSizer2, 0, wxEXPAND, 0 );


	m_panel1->SetSizer( fgSizer1 );
	m_panel1->Layout();
	fgSizer1->Fit( m_panel1 );
	m_notebook1->AddPage( m_panel1, _("数据"), true );

	// Settings page with 3 sub-tabs (replaces Download)
	m_panelSettings = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panelSettings->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_3DLIGHT ) );
	wxBoxSizer* sizerSettings = new wxBoxSizer( wxVERTICAL );
	m_notebookSettings = new wxNotebook( m_panelSettings, wxID_ANY );

	// Helper lambda to create a settings tab

	// --- Current settings tab ---
	m_panelCurrSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerCurr = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerCurr->AddGrowableCol( 1 );
	sizerCurr->AddSpacer(0);
	m_checkBoxAnimate = new wxCheckBox( m_panelCurrSettings, wxID_ANY, _("流动动画") );
	sizerCurr->Add( m_checkBoxAnimate, 0, wxALL, 3 );
	sizerCurr->AddSpacer(0);
	// Arrow and particle checkboxes (moved from Data panel)
	m_checkBoxDCurrent = new wxCheckBox( m_panelCurrSettings, wxID_ANY, _("显示方向箭头") );
	sizerCurr->Add( m_checkBoxDCurrent, 0, wxALL, 3 );
	sizerCurr->AddSpacer(0);
	m_checkBoxParticles = new wxCheckBox( m_panelCurrSettings, wxID_ANY, _("显示粒子") );
	sizerCurr->Add( m_checkBoxParticles, 0, wxALL, 3 );
	m_panelCurrSettings->SetSizer( sizerCurr );
	m_notebookSettings->AddPage( m_panelCurrSettings, _("海流") );

	// --- Sea Temp settings tab ---
	m_panelSSTSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerSST = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerSST->AddGrowableCol( 1 );
	sizerSST->AddSpacer(0);
	m_checkBoxIsoSST = new wxCheckBox( m_panelSSTSettings, wxID_ANY, _("显示等值线") );
	sizerSST->Add( m_checkBoxIsoSST, 0, wxALL, 3 );
	sizerSST->AddSpacer(0);
	m_checkBoxAnimateSST = new wxCheckBox( m_panelSSTSettings, wxID_ANY, _("流动动画") );
	sizerSST->Add( m_checkBoxAnimateSST, 0, wxALL, 3 );
	m_panelSSTSettings->SetSizer( sizerSST );
	m_notebookSettings->AddPage( m_panelSSTSettings, _("海温") );

	// --- Salinity settings tab ---
	m_panelSalSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerSal = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerSal->AddGrowableCol( 1 );
	sizerSal->AddSpacer(0);
	m_checkBoxIsoSal = new wxCheckBox( m_panelSalSettings, wxID_ANY, _("显示等值线") );
	sizerSal->Add( m_checkBoxIsoSal, 0, wxALL, 3 );
	sizerSal->AddSpacer(0);
	m_checkBoxAnimateSal = new wxCheckBox( m_panelSalSettings, wxID_ANY, _("流动动画") );
	sizerSal->Add( m_checkBoxAnimateSal, 0, wxALL, 3 );
	m_panelSalSettings->SetSizer( sizerSal );
	m_notebookSettings->AddPage( m_panelSalSettings, _("盐度") );

	// --- Zos settings tab ---
	m_panelZosSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerZos = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerZos->AddGrowableCol( 1 );
	sizerZos->AddSpacer(0);
	m_checkBoxIsoZos = new wxCheckBox( m_panelZosSettings, wxID_ANY, _("显示等值线") );
	sizerZos->Add( m_checkBoxIsoZos, 0, wxALL, 3 );
	sizerZos->AddSpacer(0);
	m_checkBoxAnimateZos = new wxCheckBox( m_panelZosSettings, wxID_ANY, _("流动动画") );
	sizerZos->Add( m_checkBoxAnimateZos, 0, wxALL, 3 );
	m_panelZosSettings->SetSizer( sizerZos );
	m_notebookSettings->AddPage( m_panelZosSettings, _("海面高度") );

	// --- Feature detection settings tab (识别) ---
	m_panelFeatSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerFeat = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerFeat->AddGrowableCol( 1 );
	// top-level clip mode
	m_checkBoxFeatClip = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("数据图层仅在特征内显示") );
	sizerFeat->Add( m_checkBoxFeatClip, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	// processing region
	wxStaticText* lblRegion = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("处理范围") );
	sizerFeat->Add( lblRegion, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	wxBoxSizer* sizerRegion = new wxBoxSizer( wxHORIZONTAL );
	m_choiceFeatROI = new wxChoice( m_panelFeatSettings, wxID_ANY );
	m_choiceFeatROI->Append( _("中国周边 (15-45N/100-135E)") );
	m_choiceFeatROI->Append( _("亚洲海域 (0-55N/60-150E)") );
	m_choiceFeatROI->Append( _("全球") );
	m_choiceFeatROI->Append( _("自定义") );
	sizerRegion->Add( m_choiceFeatROI, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5 );
	m_textFeatLatMin = new wxTextCtrl( m_panelFeatSettings, wxID_ANY, _("15"), wxDefaultPosition, wxSize( 46,-1 ) );
	m_textFeatLatMax = new wxTextCtrl( m_panelFeatSettings, wxID_ANY, _("45"), wxDefaultPosition, wxSize( 46,-1 ) );
	m_textFeatLonMin = new wxTextCtrl( m_panelFeatSettings, wxID_ANY, _("100"), wxDefaultPosition, wxSize( 46,-1 ) );
	m_textFeatLonMax = new wxTextCtrl( m_panelFeatSettings, wxID_ANY, _("135"), wxDefaultPosition, wxSize( 46,-1 ) );
	sizerRegion->Add( m_textFeatLatMin, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 2 );
	sizerRegion->Add( new wxStaticText( m_panelFeatSettings, wxID_ANY, _("~") ), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 2 );
	sizerRegion->Add( m_textFeatLatMax, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5 );
	sizerRegion->Add( m_textFeatLonMin, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 2 );
	sizerRegion->Add( new wxStaticText( m_panelFeatSettings, wxID_ANY, _("~") ), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 2 );
	sizerRegion->Add( m_textFeatLonMax, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5 );
	m_btnFeatRegionApply = new wxButton( m_panelFeatSettings, wxID_ANY, _("应用范围"), wxDefaultPosition, wxSize( 60,-1 ) );
	sizerRegion->Add( m_btnFeatRegionApply, 0, wxALIGN_CENTER_VERTICAL, 0 );
	m_btnFeatRegionPick = new wxButton( m_panelFeatSettings, wxID_ANY, _("圈画"), wxDefaultPosition, wxSize( 46,-1 ) );
	sizerRegion->Add( m_btnFeatRegionPick, 0, wxALIGN_CENTER_VERTICAL, 0 );
	m_btnFeatRegionPick->Hide();   // quarantined: crashes on drag (2026-09-24), re-enable when fixed
	sizerFeat->Add( sizerRegion, 0, wxEXPAND, 0 );
	m_staticFeatFileExtent = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("文件覆盖: (未加载)") );
	sizerFeat->AddSpacer(0);
	sizerFeat->Add( m_staticFeatFileExtent, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	// feature type toggles (P1: fronts / convergence-divergence / shear)
	m_checkBoxFeatFront = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("锋面 (密度锋)") );
	sizerFeat->Add( m_checkBoxFeatFront, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	m_checkBoxFeatEddy = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("中尺度涡") );
	sizerFeat->Add( m_checkBoxFeatEddy, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	m_checkBoxFeatJet = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("强流轴") );
	sizerFeat->Add( m_checkBoxFeatJet, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	m_checkBoxFeatConverg = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("辐聚/辐散区") );
	sizerFeat->Add( m_checkBoxFeatConverg, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	m_checkBoxFeatShear = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("流场剪切区") );
	sizerFeat->Add( m_checkBoxFeatShear, 0, wxALL, 3 );
	sizerFeat->AddSpacer(0);
	// sensitivity
	wxStaticText* lblSens = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("灵敏度") );
	sizerFeat->Add( lblSens, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	m_sliderFeatSens = new wxSlider( m_panelFeatSettings, wxID_ANY, 60, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_MIN_MAX_LABELS|wxSL_VALUE_LABEL );
	sizerFeat->Add( m_sliderFeatSens, 0, wxEXPAND|wxALL, 3 );
	// --- 黑潮主轴提取（流线约束最大流速法，Wang2026） ---
	{
		wxStaticLine* lineKur = new wxStaticLine( m_panelFeatSettings );
		sizerFeat->Add( lineKur, 0, wxEXPAND|wxALL, 3 );
		sizerFeat->AddSpacer(0);
		wxStaticText* lblKur = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("黑潮主轴") );
		sizerFeat->Add( lblKur, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
		wxBoxSizer* sizerKur = new wxBoxSizer( wxHORIZONTAL );
		m_btnKuroshioExtract = new wxButton( m_panelFeatSettings, wxID_ANY, _("提取主轴"), wxDefaultPosition, wxSize( 64,-1 ) );
		sizerKur->Add( m_btnKuroshioExtract, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5 );
		m_btnKuroshioExport = new wxButton( m_panelFeatSettings, wxID_ANY, _("导出CSV"), wxDefaultPosition, wxSize( 64,-1 ) );
		sizerKur->Add( m_btnKuroshioExport, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5 );
		sizerFeat->Add( sizerKur, 0, wxALIGN_CENTER_VERTICAL, 0 );
		m_checkBoxKuroshioShow = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("显示主轴 (黄色粗线)") );
		sizerFeat->Add( m_checkBoxKuroshioShow, 0, wxALL, 3 );
		sizerFeat->AddSpacer(0);
		m_checkBoxKuroshioBdy = new wxCheckBox( m_panelFeatSettings, wxID_ANY, _("显示左右边界 (蓝/红细线)") );
		sizerFeat->Add( m_checkBoxKuroshioBdy, 0, wxALL, 3 );
		sizerFeat->AddSpacer(0);
		// 高分辨率陆海掩膜（自动加载，仅显示状态）
		wxStaticText* lblMask = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("陆海掩膜") );
		sizerFeat->Add( lblMask, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
		m_staticEtopoStatus = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("(自动加载中…)") );
		sizerFeat->Add( m_staticEtopoStatus, 0, wxALIGN_CENTER_VERTICAL, 0 );
	}
	// hover readout
	wxStaticText* lblRead = new wxStaticText( m_panelFeatSettings, wxID_ANY, _("识别读数") );
	sizerFeat->Add( lblRead, 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	m_textCtrlFeatReadout = new wxTextCtrl( m_panelFeatSettings, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( -1, 48 ), wxTE_READONLY|wxTE_MULTILINE );
	sizerFeat->Add( m_textCtrlFeatReadout, 0, wxEXPAND|wxALL, 3 );
	m_panelFeatSettings->SetSizer( sizerFeat );
	m_notebookSettings->AddPage( m_panelFeatSettings, _("识别") );

	sizerSettings->Add( m_notebookSettings, 1, wxEXPAND|wxALL, 5 );
	m_panelSettings->SetSizer( sizerSettings );
	m_panelSettings->Layout();
	m_notebook1->AddPage( m_panelSettings, _("设置"), false );

	fgSizer3->Add( m_notebook1, 1, wxEXPAND | wxALL, 5 );


	this->SetSizer( fgSizer3 );
	this->Layout();

	// Connect Events
	this->Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( ncdfDialog::onCloseDialog ) );
	m_notebook1->Connect( wxEVT_CHAR, wxKeyEventHandler( ncdfDialog::OnCharNoteBook1 ), NULL, this );
	m_notebook1->Connect( wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, wxNotebookEventHandler( ncdfDialog::onPageChanged ), NULL, this );
	m_textCtrlDir->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( ncdfDialog::onDirChanged ), NULL, this );
	m_fileButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFileButtonClick ), NULL, this );
	m_treeCtrl->Connect( wxEVT_COMMAND_TREE_ITEM_RIGHT_CLICK, wxTreeEventHandler( ncdfDialog::onTreeItemRightClick ), NULL, this );
	m_treeCtrl->Connect( wxEVT_COMMAND_TREE_SEL_CHANGED, wxTreeEventHandler( ncdfDialog::onTreeSelectionChanged ), NULL, this );
	m_checkBoxDCurrent->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onDCurrentClick ), NULL, this );
	m_checkBoxBmpCurrentForce->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onBmpCurrentForceClick ), NULL, this );
	m_checkBoxParticles->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onParticlesClick ), NULL, this );
	m_checkBoxSeaTemp->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSeaTempClick ), NULL, this );
	m_checkBoxIsoSST->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoSSTClick ), NULL, this );
	m_checkBoxAnimateSST->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateSSTClick ), NULL, this );
	m_checkBoxIsoSal->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoSalClick ), NULL, this );
	m_checkBoxAnimateSal->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateSalClick ), NULL, this );
	m_checkBoxSalinity->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSalinityClick ), NULL, this );
	m_checkBoxZos->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onZosClick ), NULL, this );
	m_checkBoxIsoZos->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoZosClick ), NULL, this );
	m_checkBoxAnimateZos->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateZosClick ), NULL, this );
	m_checkBoxFeatClip->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatClipClick ), NULL, this );
	m_checkBoxFeatFront->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatFrontClick ), NULL, this );
	m_checkBoxFeatEddy->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatEddyClick ), NULL, this );
	m_checkBoxFeatJet->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatJetClick ), NULL, this );
	m_btnFeatRegionPick->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatRegionPick ), NULL, this );
	m_checkBoxFeatConverg->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatConvergClick ), NULL, this );
	m_checkBoxFeatShear->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatShearClick ), NULL, this );
	m_choiceFeatROI->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onFeatROIChoice ), NULL, this );
	m_btnFeatRegionApply->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatRegionApply ), NULL, this );
	m_sliderFeatSens->Connect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( ncdfDialog::onFeatSensChanged ), NULL, this );
	m_btnKuroshioExtract->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioExtractClick ), NULL, this );
	m_btnKuroshioExport->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioExportClick ), NULL, this );
	m_checkBoxKuroshioShow->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioShowClick ), NULL, this );
	m_checkBoxKuroshioBdy->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioBdyClick ), NULL, this );
	m_checkBoxAnimate->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateClick ), NULL, this );

}

ncdfDialog::~ncdfDialog()
{
	// Disconnect Events
	this->Disconnect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( ncdfDialog::onCloseDialog ) );
	m_notebook1->Disconnect( wxEVT_CHAR, wxKeyEventHandler( ncdfDialog::OnCharNoteBook1 ), NULL, this );
	m_notebook1->Disconnect( wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, wxNotebookEventHandler( ncdfDialog::onPageChanged ), NULL, this );
	m_textCtrlDir->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( ncdfDialog::onDirChanged ), NULL, this );
	m_fileButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFileButtonClick ), NULL, this );
	m_treeCtrl->Disconnect( wxEVT_COMMAND_TREE_ITEM_RIGHT_CLICK, wxTreeEventHandler( ncdfDialog::onTreeItemRightClick ), NULL, this );
	m_treeCtrl->Disconnect( wxEVT_COMMAND_TREE_SEL_CHANGED, wxTreeEventHandler( ncdfDialog::onTreeSelectionChanged ), NULL, this );
	m_checkBoxDCurrent->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onDCurrentClick ), NULL, this );
	m_checkBoxBmpCurrentForce->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onBmpCurrentForceClick ), NULL, this );
	m_checkBoxParticles->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onParticlesClick ), NULL, this );
	m_checkBoxSeaTemp->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSeaTempClick ), NULL, this );
	m_checkBoxIsoSST->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoSSTClick ), NULL, this );
	m_checkBoxAnimateSST->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateSSTClick ), NULL, this );
	m_checkBoxIsoSal->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoSalClick ), NULL, this );
	m_checkBoxAnimateSal->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateSalClick ), NULL, this );
	m_checkBoxSalinity->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSalinityClick ), NULL, this );
	m_checkBoxZos->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onZosClick ), NULL, this );
	m_checkBoxIsoZos->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onIsoZosClick ), NULL, this );
	m_checkBoxAnimateZos->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateZosClick ), NULL, this );
	m_checkBoxFeatClip->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatClipClick ), NULL, this );
	m_checkBoxFeatFront->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatFrontClick ), NULL, this );
	m_checkBoxFeatEddy->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatEddyClick ), NULL, this );
	m_checkBoxFeatJet->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatJetClick ), NULL, this );
	m_btnFeatRegionPick->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatRegionPick ), NULL, this );
	m_checkBoxFeatConverg->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatConvergClick ), NULL, this );
	m_checkBoxFeatShear->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatShearClick ), NULL, this );
	m_choiceFeatROI->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onFeatROIChoice ), NULL, this );
	m_btnFeatRegionApply->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onFeatRegionApply ), NULL, this );
	m_sliderFeatSens->Disconnect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( ncdfDialog::onFeatSensChanged ), NULL, this );
	m_btnKuroshioExtract->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioExtractClick ), NULL, this );
	m_btnKuroshioExport->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioExportClick ), NULL, this );
	m_checkBoxKuroshioShow->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioShowClick ), NULL, this );
	m_checkBoxKuroshioBdy->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onKuroshioBdyClick ), NULL, this );
	m_checkBoxAnimate->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onAnimateClick ), NULL, this );


}

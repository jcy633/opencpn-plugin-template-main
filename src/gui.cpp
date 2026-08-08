///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "gui.h"

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

	m_fileButton = new wxBitmapButton( m_panel1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );

	bSizer2->Add( m_fileButton, 0, wxALL, 5 );

	m_bpSettings = new wxBitmapButton( m_panel1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer2->Add( m_bpSettings, 0, wxALL, 5 );


	fgSizer1->Add( bSizer2, 0, wxEXPAND, 5 );

	// Hidden time choice (used by playback logic)
	m_choiceTime = new wxChoice( m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxArrayString(), 0 );
	m_choiceTime->Hide();

	m_treeCtrl = new wxTreeCtrl( m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE|wxHSCROLL|wxBORDER_SUNKEN );
	m_treeCtrl->SetFont( wxFont( 12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Sans") ) );
	m_treeCtrl->SetMinSize( wxSize( -1,250 ) );

	fgSizer1->Add( m_treeCtrl, 1, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer10;
	bSizer10 = new wxBoxSizer( wxVERTICAL );

	m_staticTextDateTime = new wxStaticText( m_panel1, wxID_ANY, _("date/time"), wxDefaultPosition, wxDefaultSize, 0 );
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

	m_bpPrev = new wxBitmapButton( m_panel1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer5->Add( m_bpPrev, 0, wxALL, 5 );

	m_bpNext = new wxBitmapButton( m_panel1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer5->Add( m_bpNext, 0, wxALL, 5 );

	m_bpPlay = new wxBitmapButton( m_panel1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer5->Add( m_bpPlay, 0, wxALL|wxALIGN_CENTER_VERTICAL, 5 );

	m_sTimeline = new wxSlider( m_panel1, wxID_ANY, 0, 0, 10, wxDefaultPosition, wxSize(200, -1), wxSL_HORIZONTAL );
	bSizer5->Add( m_sTimeline, 1, wxEXPAND|wxALL|wxALIGN_CENTER_VERTICAL, 5 );


	fgSizer7->Add( bSizer5, 1, wxEXPAND, 5 );


	fgSizer1->Add( fgSizer7, 0, wxEXPAND, 5 );

	m_staticline1 = new wxStaticLine( m_panel1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	fgSizer1->Add( m_staticline1, 0, wxEXPAND | wxALL, 5 );

	wxFlexGridSizer* fgSizer2;
	fgSizer2 = new wxFlexGridSizer( 5, 1, 0, 0 );
	fgSizer2->SetFlexibleDirection( wxBOTH );
	fgSizer2->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	// Row 1: Current checkbox + direction display
	wxBoxSizer* bSizer11;
	bSizer11 = new wxBoxSizer( wxHORIZONTAL );

	bSizer11->Add( 24, 0, 0, wxEXPAND, 5 );

	m_checkBoxDCurrent = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer11->Add( m_checkBoxDCurrent, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );

	m_staticText333 = new wxStaticText( m_panel1, wxID_ANY, _("Current"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText333->Wrap( -1 );
	m_staticText333->SetMinSize( wxSize( 75,-1 ) );
	bSizer11->Add( m_staticText333, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_textCtrlCurrentDir = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_RIGHT );
	bSizer11->Add( m_textCtrlCurrentDir, 0, wxALL, 0 );

	m_staticText341 = new wxStaticText( m_panel1, wxID_ANY, _("Deg"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText341->Wrap( -1 );
	bSizer11->Add( m_staticText341, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );

	fgSizer2->Add( bSizer11, 0, 0, 5 );

	// Row 2: Force checkbox + force display
	wxBoxSizer* bSizer14;
	bSizer14 = new wxBoxSizer( wxHORIZONTAL );

	bSizer14->Add( 24, 0, 0, wxEXPAND, 0 );

	m_checkBoxBmpCurrentForce = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer14->Add( m_checkBoxBmpCurrentForce, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );

	m_staticText40 = new wxStaticText( m_panel1, wxID_ANY, _("Force"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText40->Wrap( -1 );
	m_staticText40->SetMinSize( wxSize( 75,-1 ) );
	bSizer14->Add( m_staticText40, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_textCtrlCurrentForce = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_RIGHT );
	bSizer14->Add( m_textCtrlCurrentForce, 0, wxALL, 0 );

	m_staticText41 = new wxStaticText( m_panel1, wxID_ANY, _("kts"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText41->Wrap( -1 );
	bSizer14->Add( m_staticText41, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );

	fgSizer2->Add( bSizer14, 0, 0, 5 );

	// Row 3: Numbers checkbox (hidden - feature removed)
	m_checkBoxNumbers = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextNumbers = new wxStaticText( m_panel1, wxID_ANY, _("Numbers"), wxDefaultPosition, wxDefaultSize, 0 );
	m_checkBoxNumbers->Hide();
	m_staticTextNumbers->Hide();

	// Row 4: Particles checkbox
	wxBoxSizer* bSizerPart;
	bSizerPart = new wxBoxSizer( wxHORIZONTAL );

	bSizerPart->Add( 24, 0, 0, wxEXPAND, 0 );

	m_checkBoxParticles = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizerPart->Add( m_checkBoxParticles, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );

	m_staticTextParticles = new wxStaticText( m_panel1, wxID_ANY, _("Particles"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextParticles->Wrap( -1 );
	bSizerPart->Add( m_staticTextParticles, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	fgSizer2->Add( bSizerPart, 0, 0, 5 );

	// Sea Temperature overlay (hidden by default)
	wxFlexGridSizer* bSizerSeaTemp;
	bSizerSeaTemp = new wxFlexGridSizer( 1, 4, 0, 0 );
	bSizerSeaTemp->SetFlexibleDirection( wxBOTH );
	bSizerSeaTemp->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	bSizerSeaTemp->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxSeaTemp = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizerSeaTemp->Add( m_checkBoxSeaTemp, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSeaTemp = new wxStaticText( m_panel1, wxID_ANY, _("Sea Temp"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextSeaTemp->Wrap( -1 );
	bSizerSeaTemp->Add( m_staticTextSeaTemp, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlSeaTemp = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_RIGHT );
	bSizerSeaTemp->Add( m_textCtrlSeaTemp, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSeaTempUnit = new wxStaticText( m_panel1, wxID_ANY, _("\xc2\xb0""C"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextSeaTempUnit->Wrap( -1 );
	bSizerSeaTemp->Add( m_staticTextSeaTempUnit, 0, wxALL|wxALIGN_CENTER_VERTICAL, 2 );
	fgSizer2->Add( bSizerSeaTemp, 0, 0, 5 );

	// Sea Temperature isolines (hidden by default)
	wxFlexGridSizer* bSizerSeaTempIso;
	bSizerSeaTempIso = new wxFlexGridSizer( 1, 4, 0, 0 );
	bSizerSeaTempIso->SetFlexibleDirection( wxBOTH );
	bSizerSeaTempIso->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	bSizerSeaTempIso->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxSeaTempIso = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizerSeaTempIso->Add( m_checkBoxSeaTempIso, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSeaTempIso = new wxStaticText( m_panel1, wxID_ANY, _("SST Iso"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextSeaTempIso->Wrap( -1 );
	bSizerSeaTempIso->Add( m_staticTextSeaTempIso, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	fgSizer2->Add( bSizerSeaTempIso, 0, 0, 5 );

	// Salinity overlay (hidden by default)
	wxFlexGridSizer* bSizerSalinity;
	bSizerSalinity = new wxFlexGridSizer( 1, 4, 0, 0 );
	bSizerSalinity->SetFlexibleDirection( wxBOTH );
	bSizerSalinity->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	bSizerSalinity->Add( 24, 0, 0, wxEXPAND, 0 );
	m_checkBoxSalinity = new wxCheckBox( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizerSalinity->Add( m_checkBoxSalinity, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	m_staticTextSalinity = new wxStaticText( m_panel1, wxID_ANY, _("Salinity"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextSalinity->Wrap( -1 );
	bSizerSalinity->Add( m_staticTextSalinity, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );
	m_textCtrlSalinity = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 75,-1 ), wxTE_READONLY|wxTE_RIGHT );
	bSizerSalinity->Add( m_textCtrlSalinity, 0, wxALL|wxALIGN_CENTER_VERTICAL, 0 );
	fgSizer2->Add( bSizerSalinity, 0, 0, 5 );


	fgSizer1->Add( fgSizer2, 0, wxEXPAND, 0 );


	m_panel1->SetSizer( fgSizer1 );
	m_panel1->Layout();
	fgSizer1->Fit( m_panel1 );
	m_notebook1->AddPage( m_panel1, _("Data"), true );

	// Settings page with 3 sub-tabs (replaces Download)
	m_panelSettings = new wxPanel( m_notebook1, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panelSettings->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_3DLIGHT ) );
	wxBoxSizer* sizerSettings = new wxBoxSizer( wxVERTICAL );
	m_notebookSettings = new wxNotebook( m_panelSettings, wxID_ANY );

	// Helper lambda to create a settings tab
	wxArrayString interpChoices;
	interpChoices.Add(_("Linear (color)"));
	interpChoices.Add(_("Linear (scalar)"));
	interpChoices.Add(_("Bicubic"));
	interpChoices.Add(_("Monotone bicubic"));

	// --- Current settings tab ---
	m_panelCurrSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerCurr = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerCurr->AddGrowableCol( 1 );
	sizerCurr->Add( new wxStaticText(m_panelCurrSettings, wxID_ANY, _("Interpolation:")), 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	m_choiceInterpCurr = new wxChoice( m_panelCurrSettings, wxID_ANY, wxDefaultPosition, wxDefaultSize, interpChoices );
	m_choiceInterpCurr->SetSelection(0);
	sizerCurr->Add( m_choiceInterpCurr, 0, wxALL|wxEXPAND, 3 );
	sizerCurr->AddSpacer(0);
	m_checkBoxSmoothCurr = new wxCheckBox( m_panelCurrSettings, wxID_ANY, _("Smooth color gradients") );
	sizerCurr->Add( m_checkBoxSmoothCurr, 0, wxALL, 3 );
	sizerCurr->AddSpacer(0);
	m_checkBoxSharpenCurr = new wxCheckBox( m_panelCurrSettings, wxID_ANY, _("Edge sharpening") );
	sizerCurr->Add( m_checkBoxSharpenCurr, 0, wxALL, 3 );
	m_panelCurrSettings->SetSizer( sizerCurr );
	m_notebookSettings->AddPage( m_panelCurrSettings, _("Current") );

	// --- Sea Temp settings tab ---
	m_panelSSTSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerSST = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerSST->AddGrowableCol( 1 );
	sizerSST->Add( new wxStaticText(m_panelSSTSettings, wxID_ANY, _("Interpolation:")), 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	m_choiceInterpSST = new wxChoice( m_panelSSTSettings, wxID_ANY, wxDefaultPosition, wxDefaultSize, interpChoices );
	m_choiceInterpSST->SetSelection(0);
	sizerSST->Add( m_choiceInterpSST, 0, wxALL|wxEXPAND, 3 );
	sizerSST->AddSpacer(0);
	m_checkBoxSmoothSST = new wxCheckBox( m_panelSSTSettings, wxID_ANY, _("Smooth color gradients") );
	sizerSST->Add( m_checkBoxSmoothSST, 0, wxALL, 3 );
	sizerSST->AddSpacer(0);
	m_checkBoxSharpenSST = new wxCheckBox( m_panelSSTSettings, wxID_ANY, _("Edge sharpening") );
	sizerSST->Add( m_checkBoxSharpenSST, 0, wxALL, 3 );
	m_panelSSTSettings->SetSizer( sizerSST );
	m_notebookSettings->AddPage( m_panelSSTSettings, _("Sea Temp") );

	// --- Salinity settings tab ---
	m_panelSalSettings = new wxPanel( m_notebookSettings );
	wxFlexGridSizer* sizerSal = new wxFlexGridSizer( 0, 2, 5, 5 );
	sizerSal->AddGrowableCol( 1 );
	sizerSal->Add( new wxStaticText(m_panelSalSettings, wxID_ANY, _("Interpolation:")), 0, wxALIGN_CENTER_VERTICAL|wxALL, 3 );
	m_choiceInterpSal = new wxChoice( m_panelSalSettings, wxID_ANY, wxDefaultPosition, wxDefaultSize, interpChoices );
	m_choiceInterpSal->SetSelection(0);
	sizerSal->Add( m_choiceInterpSal, 0, wxALL|wxEXPAND, 3 );
	sizerSal->AddSpacer(0);
	m_checkBoxSmoothSal = new wxCheckBox( m_panelSalSettings, wxID_ANY, _("Smooth color gradients") );
	sizerSal->Add( m_checkBoxSmoothSal, 0, wxALL, 3 );
	sizerSal->AddSpacer(0);
	m_checkBoxSharpenSal = new wxCheckBox( m_panelSalSettings, wxID_ANY, _("Edge sharpening") );
	sizerSal->Add( m_checkBoxSharpenSal, 0, wxALL, 3 );
	m_panelSalSettings->SetSizer( sizerSal );
	m_notebookSettings->AddPage( m_panelSalSettings, _("Salinity") );

	sizerSettings->Add( m_notebookSettings, 1, wxEXPAND|wxALL, 5 );
	m_panelSettings->SetSizer( sizerSettings );
	m_panelSettings->Layout();
	m_notebook1->AddPage( m_panelSettings, _("Settings"), false );

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
	m_checkBoxSeaTempIso->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSeaTempIsoClick ), NULL, this );
	m_checkBoxSalinity->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSalinityClick ), NULL, this );
	m_choiceInterpCurr->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpCurrChange ), NULL, this );
	m_checkBoxSmoothCurr->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothCurrClick ), NULL, this );
	m_checkBoxSharpenCurr->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenCurrClick ), NULL, this );
	m_choiceInterpSST->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpSSTChange ), NULL, this );
	m_checkBoxSmoothSST->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothSSTClick ), NULL, this );
	m_checkBoxSharpenSST->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenSSTClick ), NULL, this );
	m_choiceInterpSal->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpSalChange ), NULL, this );
	m_checkBoxSmoothSal->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothSalClick ), NULL, this );
	m_checkBoxSharpenSal->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenSalClick ), NULL, this );
	m_choiceTime->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onTimeChange ), NULL, this );
	m_sTimeline->Connect( wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );
	m_sTimeline->Connect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );
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
	m_checkBoxSeaTempIso->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSeaTempIsoClick ), NULL, this );
	m_checkBoxSalinity->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSalinityClick ), NULL, this );
	m_choiceInterpCurr->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpCurrChange ), NULL, this );
	m_checkBoxSmoothCurr->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothCurrClick ), NULL, this );
	m_checkBoxSharpenCurr->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenCurrClick ), NULL, this );
	m_choiceInterpSST->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpSSTChange ), NULL, this );
	m_checkBoxSmoothSST->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothSSTClick ), NULL, this );
	m_checkBoxSharpenSST->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenSSTClick ), NULL, this );
	m_choiceInterpSal->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onInterpSalChange ), NULL, this );
	m_checkBoxSmoothSal->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSmoothSalClick ), NULL, this );
	m_checkBoxSharpenSal->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( ncdfDialog::onSharpenSalClick ), NULL, this );
	m_choiceTime->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onTimeChange ), NULL, this );
	m_sTimeline->Disconnect( wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );
	m_sTimeline->Disconnect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );

}

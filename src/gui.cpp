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
	m_staticText333->SetMinSize( wxSize( 55,-1 ) );
	bSizer11->Add( m_staticText333, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_textCtrlCurrentDir = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 55,-1 ), wxTE_READONLY|wxTE_RIGHT );
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
	m_staticText40->SetMinSize( wxSize( 55,-1 ) );
	bSizer14->Add( m_staticText40, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_textCtrlCurrentForce = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 55,-1 ), wxTE_READONLY|wxTE_RIGHT );
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
	m_textCtrlSeaTemp = new wxTextCtrl( m_panel1, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 55,-1 ), wxTE_READONLY|wxTE_RIGHT );
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


	fgSizer1->Add( fgSizer2, 0, wxEXPAND, 0 );


	m_panel1->SetSizer( fgSizer1 );
	m_panel1->Layout();
	fgSizer1->Fit( m_panel1 );
	m_notebook1->AddPage( m_panel1, _("Data"), true );
	m_panel2 = new wxPanel( m_notebook1, wxID_ANY, wxPoint( -1,-1 ), wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel2->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_3DLIGHT ) );

	wxFlexGridSizer* fgSizer5;
	fgSizer5 = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer5->SetFlexibleDirection( wxBOTH );
	fgSizer5->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_staticText6 = new wxStaticText( m_panel2, wxID_ANY, _("This page is intended to help users download \nthe areas to use with the ncdf plugin.\n\nThe display Areas for ncdf Tidal Currents\n are as follows:\n\nFormat: \nLat(Min), Lat(Max), Lon(Min), Lon(Max)\n\nEnglish Channel:           48, 51, -7, -2\nIrish Sea:                         50, 56, -9, -2\nSouthern Brittany:         46.5, 48.5, -5, -0.5\nNorth Sea:                      51, 56, -2.5, 5\nBiscay South:                 43, 47, -9.5, -0.5\nWestern Ireland:            50, 56, -12, -8               "), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText6->Wrap( -1 );
	fgSizer5->Add( m_staticText6, 0, wxALL, 5 );


	m_panel2->SetSizer( fgSizer5 );
	m_panel2->Layout();
	fgSizer5->Fit( m_panel2 );
	m_notebook1->AddPage( m_panel2, _("Download"), false );

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
	m_choiceTime->Disconnect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler( ncdfDialog::onTimeChange ), NULL, this );
	m_sTimeline->Disconnect( wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );
	m_sTimeline->Disconnect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( ncdfDialog::OnTimeline ), NULL, this );

}

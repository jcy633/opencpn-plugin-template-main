///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/bmpbuttn.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/choice.h>
#include <wx/treectrl.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/dialog.h>

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// Class ncdfDialog
///////////////////////////////////////////////////////////////////////////////
class ncdfDialog : public wxDialog
{
	private:

	protected:
	wxNotebook* m_notebook1;
	wxPanel* m_panel1;
	wxChoice* m_choiceTime;
	wxBitmapButton* m_bpNext;
	wxBitmapButton* m_bpPrev;
	wxBitmapButton* m_bpPlay;
	wxSlider* m_sTimeline;
	wxStaticLine* m_staticline1;
	wxStaticText* m_staticText333;
	wxStaticText* m_staticText341;
	wxStaticText* m_staticText40;
	wxTextCtrl* m_textCtrlCurrentForce;
	wxStaticText* m_staticText41;
	wxPanel* m_panel2;
	wxStaticText* m_staticText6;

		// Virtual event handlers, override them in your derived class
	virtual void onCloseDialog( wxCloseEvent& event ) { event.Skip(); }
	virtual void OnCharNoteBook1( wxKeyEvent& event ) { event.Skip(); }
	virtual void onPageChanged( wxNotebookEvent& event ) { event.Skip(); }
	virtual void onDirChanged( wxCommandEvent& event ) { event.Skip(); }
	virtual void onFileButtonClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onTreeItemRightClick( wxTreeEvent& event ) { event.Skip(); }
	virtual void onTreeSelectionChanged( wxTreeEvent& event ) { event.Skip(); }
	virtual void onDCurrentClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onBmpCurrentForceClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onIsoLinesClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onNumbersClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onParticlesClick( wxCommandEvent& event ) { event.Skip(); }
	virtual void onTimeChange( wxCommandEvent& event ) { event.Skip(); }
	virtual void OnTimeline( wxScrollEvent& event ) { event.Skip(); }
	virtual void OnPlayStop( wxCommandEvent& event ) { event.Skip(); }
	virtual void OnSettings( wxCommandEvent& event ) { event.Skip(); }


	public:
		wxTextCtrl* m_textCtrlDir;
		wxBitmapButton* m_fileButton;
		wxBitmapButton* m_bpSettings;
		wxTreeCtrl* m_treeCtrl;
		wxStaticText* m_staticTextDateTime;
		wxCheckBox* m_checkBoxDCurrent;
		wxTextCtrl* m_textCtrlCurrentDir;
		wxCheckBox* m_checkBoxBmpCurrentForce;
		wxCheckBox* m_checkBoxNumbers;
		wxStaticText* m_staticTextNumbers;
		wxCheckBox* m_checkBoxParticles;
		wxStaticText* m_staticTextParticles;

		ncdfDialog( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("ncdf Dialog"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 280,460 ), long style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER );

		~ncdfDialog();

};


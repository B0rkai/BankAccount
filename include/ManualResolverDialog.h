#pragma once
#include "wx/wx.h"
#include "CommonTypes.h"
#include "Logger.h"

class INameResolve;

class ManualResolverDialog : public wxDialog {
	//static text
	Logger& m_logger;
	wxButton* m_ok_butt = nullptr;
	wxButton* m_def_butt = nullptr;
	wxButton* m_cancel_butt = nullptr;
	wxTextCtrl* m_search_txtctrl = nullptr;
	wxTextCtrl* m_create_new_txtctrl = nullptr;
	wxTextCtrl* m_add_keyword_txtctrl = nullptr;
	wxTextCtrl* m_add_desc_txtctrl = nullptr;
	wxListBox* m_selection_listctrl = nullptr;
	//Id m_resolved_id;
	QueryTopic m_topic;
	INameResolve* m_resolve_if = nullptr;
	std::vector<Id> m_id_choices;
	bool m_ok = true;
	void PopulateSelectionChoices(const IdSet& matches, const Id select = Id(INVALID_ID));
	void SearchTextChanged(wxCommandEvent& evt);
	void NewTextChanged(wxCommandEvent& evt);
	void Selected(wxCommandEvent& evt);
	void ButtonClicked(wxCommandEvent& evt); // ok, default or abort
	void CheckState();
public:
	ManualResolverDialog(wxWindow* parent, const String& title, const QueryTopic topic, INameResolve* resolve_if);
	void SetUp(const String& tr_details, const IdSet& matches, const Id& select, const String& create, const String& desc, bool optional);
	Id GetResolvedId() const;
	String GetNewName() const;
	String GetNewKeyword() const;
	String GetDescription() const;
	wxDECLARE_EVENT_TABLE();
};


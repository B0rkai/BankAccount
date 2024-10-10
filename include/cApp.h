#pragma once
#include <memory>

#include "wx/wx.h"

class cMain;

class cApp : public wxApp {
public:
	cApp();
	~cApp();
	virtual bool OnInit();
private:
	cMain* m_frame = nullptr;
};
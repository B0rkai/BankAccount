#pragma once
#include "wx/string.h"

class wxGrid;

// Exports the given grid's current contents (column headers + every cell, including any live
// edits the user made directly in the grid - e.g. the editable Category/Description columns) to
// a real .xlsx file via OpenXLSX. Every cell is written as text, faithful to what's currently
// displayed. Returns false (having logged why) on failure.
bool ExportGridToExcel(wxGrid* grid, const wxString& path);

#pragma once
#include "wx/string.h"

class wxGrid;

// Exports the given grid's current contents (column headers + every cell, including any live
// edits the user made directly in the grid - e.g. the editable Category/Description columns) to
// a real, styled .xlsx file via xlnt. Cells matching this app's own date/currency display
// formats (see Currency::PrettyPrint()/GetDateFormat()) are written as real typed values with a
// matching native Excel number format, not as text; the header row is bold with a fill color,
// every cell gets a thin grid border, and the header row is frozen. Returns false (having
// logged why) on failure.
bool ExportGridToExcel(wxGrid* grid, const wxString& path);

#include "GuiHelpers.h"
#include "wx/font.h"

const wxFont& GetMonoSpaceFont() {
    static wxFont font = wxFont(wxSize(7, 14), wxFontFamily::wxFONTFAMILY_TELETYPE, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_NORMAL);
    return font;
}

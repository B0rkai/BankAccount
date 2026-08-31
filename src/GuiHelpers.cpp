#include "GuiHelpers.h"
#include "wx/font.h"
#include "wx/bitmap.h"
#include "wx/dcmemory.h"
#include "wx/pen.h"
#include "wx/brush.h"
#include "wx/colour.h"

const wxFont& GetMonoSpaceFont() {
    static wxFont font = wxFont(wxSize(7, 14), wxFontFamily::wxFONTFAMILY_TELETYPE, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_NORMAL);
    return font;
}

namespace {
    // Bright magenta never appears in either pictogram's own black outline, so it's a safe
    // "transparent" sentinel for the classic wxMask technique (draw opaque, then mask one
    // colour out) - the simple, GraphicsContext-free way to get a transparent button bitmap.
    const wxColour ICON_MASK_COLOUR(255, 0, 255);

    wxBitmap FinishIcon(wxBitmap& bmp) {
        bmp.SetMask(new wxMask(bmp, ICON_MASK_COLOUR)); // bitmap takes ownership, same as wxGridCellAttr* elsewhere
        return bmp;
    }
}

wxBitmap MakeExportIconBitmap(int size) {
    wxBitmap bmp(size, size, 24);
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(ICON_MASK_COLOUR));
    dc.Clear();
    dc.SetPen(wxPen(*wxBLACK, 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // a small spreadsheet grid: outer square plus one internal horizontal/vertical divider pair
    const int margin = size / 6;
    const int inner = size - 2 * margin;
    dc.DrawRectangle(margin, margin, inner, inner);
    const int third = inner / 3;
    dc.DrawLine(margin, margin + third, margin + inner, margin + third);
    dc.DrawLine(margin, margin + 2 * third, margin + inner, margin + 2 * third);
    dc.DrawLine(margin + third, margin, margin + third, margin + inner);
    dc.DrawLine(margin + 2 * third, margin, margin + 2 * third, margin + inner);
    dc.SelectObject(wxNullBitmap); // release before SetMask() touches the bitmap
    return FinishIcon(bmp);
}

wxBitmap MakeChartIconBitmap(int size) {
    wxBitmap bmp(size, size, 24);
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(ICON_MASK_COLOUR));
    dc.Clear();
    dc.SetPen(wxPen(*wxBLACK, 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // a pie outline plus one filled wedge
    const int margin = size / 6;
    const int diameter = size - 2 * margin;
    dc.DrawEllipse(margin, margin, diameter, diameter);
    dc.SetBrush(*wxBLACK_BRUSH);
    dc.DrawEllipticArc(margin, margin, diameter, diameter, 0, 90); // wxDC fills a pie slice when the brush isn't transparent
    dc.SelectObject(wxNullBitmap);
    return FinishIcon(bmp);
}

#pragma once

// Small GUI-only helpers with no reason to live in CommonTypes.h (which is included by
// virtually every domain file) - GetMonoSpaceFont() pulls in wxFont (wxCore, not just wxBase)
// purely for the benefit of controls that display fixed-width text (cMain's grid/info text,
// and the resolve/keyword/new-account dialogs), so it belongs here instead.
class wxFont;
const wxFont& GetMonoSpaceFont();

// Small hand-drawn pictograms for the filter row's square icon buttons - this project has no
// icon asset pipeline (BankAccount.ico is the one bitmap resource, used only as the app icon),
// so these are drawn once via wxDC rather than adding new image files. `size` is the bitmap's
// width/height in pixels (square).
class wxBitmap;
wxBitmap MakeExportIconBitmap(int size);
wxBitmap MakeChartIconBitmap(int size);

#pragma once

// Small GUI-only helpers with no reason to live in CommonTypes.h (which is included by
// virtually every domain file) - GetMonoSpaceFont() pulls in wxFont (wxCore, not just wxBase)
// purely for the benefit of controls that display fixed-width text (cMain's grid/info text,
// and the resolve/keyword/new-account dialogs), so it belongs here instead.
class wxFont;
const wxFont& GetMonoSpaceFont();

#include "ExcelExport.h"
#include "Logger.h"

#include "wx/grid.h"

#include <xlnt/xlnt.hpp>

namespace {

enum StyleKind { STYLE_HEADER, STYLE_DATE, STYLE_EUR, STYLE_HUF, STYLE_USD, STYLE_GBP, STYLE_CHF, STYLE_PLAIN };

// Matches the exact "YYYY.MM.DD" text GetDateFormat() (CommonTypes.cpp) produces.
bool TryParseDate(const wxString& text, int& year, int& month, int& day) {
    if ((text.length() != 10) || (text[4] != '.') || (text[7] != '.')) {
        return false;
    }
    long y = 0, m = 0, d = 0;
    if (!text.Mid(0, 4).ToLong(&y) || !text.Mid(5, 2).ToLong(&m) || !text.Mid(8, 2).ToLong(&d)) {
        return false;
    }
    if ((m < 1) || (m > 12) || (d < 1) || (d > 31)) {
        return false;
    }
    year = (int)y; month = (int)m; day = (int)d;
    return true;
}

// Matches Currency::PrettyPrint()'s output (Currency.cpp): a leading space, optional '-', the
// currency sign (prefix for $/£/Fr., suffix for €/Ft), thousands-grouped digits (',' for the
// four cents-based currencies, '\'' for HUF), and (except HUF) a '.'-separated 2-digit fraction.
bool TryParseAmount(const wxString& text, double& value_out, StyleKind& style_out) {
    static const std::pair<const char*, StyleKind> SIGNS[] = {
        {"\xE2\x82\xAC", STYLE_EUR}, // €
        {"Ft", STYLE_HUF},
        {"$", STYLE_USD},
        {"\xC2\xA3", STYLE_GBP},     // £
        {"Fr.", STYLE_CHF},
    };
    for (const auto& entry : SIGNS) {
        wxString sign = wxString::FromUTF8(entry.first);
        if (text.Find(sign) == wxNOT_FOUND) {
            continue;
        }
        wxString stripped = text;
        stripped.Replace(sign, wxEmptyString);
        bool is_huf = (entry.second == STYLE_HUF);
        wxString cleaned;
        for (size_t i = 0; i < stripped.length(); ++i) {
            wxChar ch = stripped[i];
            if ((ch == ',') && !is_huf) {
                continue; // thousands separator for cents-based currencies
            }
            if ((ch == '\'') && is_huf) {
                continue; // thousands separator for HUF
            }
            if (ch == ' ') {
                continue;
            }
            cleaned += ch;
        }
        double val = 0.0;
        if (!cleaned.ToCDouble(&val)) { // ToCDouble: locale-independent, always '.' decimal - matches `cleaned`
            return false;
        }
        value_out = val;
        style_out = entry.second;
        return true;
    }
    return false;
}

const char* NumberFormatCode(StyleKind kind) {
    switch (kind) {
    case STYLE_DATE: return "yyyy\\.mm\\.dd";
    case STYLE_EUR:  return "#,##0.00\" \xE2\x82\xAC\"";
    case STYLE_HUF:  return "#,##0\" Ft\"";
    case STYLE_USD:  return "\"$\"#,##0.00";
    case STYLE_GBP:  return "\"\xC2\xA3\"#,##0.00";
    case STYLE_CHF:  return "\"Fr. \"#,##0.00";
    default: return nullptr;
    }
}

void ApplyStyle(xlnt::cell& cell, StyleKind kind) {
    static const xlnt::color HEADER_FILL_COLOR = xlnt::rgb_color(0x2F, 0x54, 0x8C);
    static const xlnt::color GRID_BORDER_COLOR = xlnt::rgb_color(0xD9, 0xD9, 0xD9);

    xlnt::border grid_border;
    xlnt::border::border_property thin_gray;
    thin_gray.style(xlnt::border_style::thin);
    thin_gray.color(GRID_BORDER_COLOR);
    for (auto side : { xlnt::border_side::start, xlnt::border_side::end, xlnt::border_side::top, xlnt::border_side::bottom }) {
        grid_border.side(side, thin_gray);
    }
    cell.border(grid_border);

    if (kind == STYLE_HEADER) {
        xlnt::font f;
        f.bold(true);
        f.color(xlnt::color::white());
        cell.font(f);
        cell.fill(xlnt::fill::solid(HEADER_FILL_COLOR));
        xlnt::alignment a;
        a.horizontal(xlnt::horizontal_alignment::center);
        cell.alignment(a);
        return;
    }
    const char* code = NumberFormatCode(kind);
    if (code) {
        cell.number_format(xlnt::number_format(code));
    }
}

} // namespace

bool ExportGridToExcel(wxGrid* grid, const wxString& path) {
    if (!grid) {
        return false;
    }
    try {
        xlnt::workbook wb;
        xlnt::worksheet ws = wb.active_sheet();

        int cols = grid->GetNumberCols();
        int rows = grid->GetNumberRows();

        for (int c = 0; c < cols; ++c) {
            xlnt::cell cell = ws.cell(xlnt::column_t(c + 1), 1);
            cell.value(std::string(grid->GetColLabelValue(c).utf8_str()));
            ApplyStyle(cell, STYLE_HEADER);

            xlnt::column_properties props;
            props.width = 16;
            props.custom_width = true;
            ws.add_column_properties(xlnt::column_t(c + 1), props);
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                wxString text = grid->GetCellValue(r, c);
                xlnt::cell cell = ws.cell(xlnt::column_t(c + 1), r + 2);
                int year, month, day;
                double amount;
                StyleKind style;
                if (text.empty()) {
                    ApplyStyle(cell, STYLE_PLAIN);
                } else if (TryParseDate(text, year, month, day)) {
                    cell.value(xlnt::date(year, month, day));
                    ApplyStyle(cell, STYLE_DATE);
                } else if (TryParseAmount(text, amount, style)) {
                    cell.value(amount);
                    ApplyStyle(cell, style);
                } else {
                    cell.value(std::string(text.utf8_str()));
                    ApplyStyle(cell, STYLE_PLAIN);
                }
            }
        }

        if (rows > 0) {
            ws.freeze_panes(ws.cell(xlnt::column_t(1), 2)); // keep the header row visible while scrolling
        }

        wb.save(std::wstring(path.wc_str()));
        LogInfo() << "Excel export: wrote " << rows << " row(s), " << cols << " column(s) to " << path.utf8_str();
        return true;
    } catch (const std::exception& e) {
        LogError() << "Excel export failed: " << e.what();
        return false;
    } catch (...) {
        LogError() << "Excel export failed: unknown error";
        return false;
    }
}

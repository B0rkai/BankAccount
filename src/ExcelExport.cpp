#include "ExcelExport.h"
#include "Logger.h"

#include "wx/grid.h"

#include "OpenXLSX.hpp"
#include "ZipFile.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

// OpenXLSX as installed here exposes no styles/number-format API at all (no XLStyles/
// XLNumberFormat headers) - cells can be given real numeric/date values, but not told how to
// *display* them. So after OpenXLSX writes the base file, this reopens it as a plain zip (via
// ZipLib, already a project dependency - see MnbExchangeRateClient.cpp for the same manual-XML
// approach applied to *reading* an xlsx) and hand-edits xl/styles.xml + the sheet XML to add the
// number formats and apply them to the relevant cells.
namespace {

enum StyleIndex {
    STYLE_DEFAULT = 0,
    STYLE_DATE = 1,
    STYLE_EUR = 2,
    STYLE_HUF = 3,
    STYLE_USD = 4,
    STYLE_GBP = 5,
    STYLE_CHF = 6,
};

// Matches the exact "YYYY.MM.DD" text GetDateFormat() (CommonTypes.cpp) produces.
bool TryParseDate(const wxString& text, double& serial_out) {
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
    std::tm tm{};
    tm.tm_year = (int)y - 1900;
    tm.tm_mon = (int)m - 1;
    tm.tm_mday = (int)d;
    // XLDateTime's std::tm constructor handles Excel's own serial-date conversion (including its
    // 1900-leap-year quirk) correctly - safer than reusing this app's own DMYToExcelSerialDate,
    // which was never meant to match Excel's exact convention, only its own round-trip.
    serial_out = OpenXLSX::XLDateTime(tm).serial();
    return true;
}

// Matches Currency::PrettyPrint()'s output (Currency.cpp): a leading space, optional '-', the
// currency sign (prefix for $/£/Fr., suffix for €/Ft), thousands-grouped digits (',' for the
// four cents-based currencies, '\'' for HUF), and (except HUF) a '.'-separated 2-digit fraction.
bool TryParseAmount(const wxString& text, double& value_out, StyleIndex& style_out) {
    static const std::pair<const char*, StyleIndex> SIGNS[] = {
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

const char* NumFmtCode(StyleIndex style) {
    switch (style) {
    case STYLE_DATE: return "yyyy\\.mm\\.dd";
    case STYLE_EUR:  return "#,##0.00&quot; \xE2\x82\xAC&quot;";
    case STYLE_HUF:  return "#,##0&quot; Ft&quot;";
    case STYLE_USD:  return "&quot;$&quot;#,##0.00";
    case STYLE_GBP:  return "&quot;\xC2\xA3&quot;#,##0.00";
    case STYLE_CHF:  return "&quot;Fr. &quot;#,##0.00";
    default: return nullptr;
    }
}

std::string ReadZipEntry(ZipArchive::Ptr archive, const std::string& name) {
    ZipArchiveEntry::Ptr entry = archive->GetEntry(name);
    if (!entry) {
        return "";
    }
    std::istream* stream = entry->GetDecompressionStream();
    if (!stream) {
        return "";
    }
    std::ostringstream ss;
    ss << stream->rdbuf();
    // Must close before this same entry's SetCompressionStream() is used to write it back out -
    // leaving the decompression stream open while switching the entry to write mode left the
    // zip's internal CRC/size bookkeeping stale, producing a file that looked fine as raw text
    // (unzip could still extract it) but that Excel's stricter zip reader rejected as corrupt.
    entry->CloseDecompressionStream();
    return ss.str();
}

// Rewrites xl/styles.xml (adds one <numFmt>/<cellXfs> pair per style actually used) and the
// sheet XML (stamps s="N" onto every cell that needs one of those styles), then re-saves the
// zip. cells_by_style maps a StyleIndex to the "B2"-style cell references that need it.
bool ApplyNumberFormats(const std::string& path, const std::map<StyleIndex, std::vector<std::string>>& cells_by_style) {
    try {
        std::ifstream in_file(path, std::ios::binary);
        if (!in_file) {
            return false;
        }
        ZipArchive::Ptr archive = ZipArchive::Create(in_file);
        if (!archive) {
            return false;
        }

        std::string styles_xml = ReadZipEntry(archive, "xl/styles.xml");
        std::string sheet_xml = ReadZipEntry(archive, "xl/worksheets/sheet1.xml");
        if (styles_xml.empty() || sheet_xml.empty()) {
            return false;
        }

        std::ostringstream numfmts, cellxfs;
        int numfmt_id = 164; // first free custom id; 0-163 are reserved built-ins
        int xf_index = 1;    // cellXfs index 0 is the pre-existing default ("General") entry
        std::map<StyleIndex, int> style_to_xf;
        for (const auto& kv : cells_by_style) {
            const char* code = NumFmtCode(kv.first);
            if (!code) {
                continue;
            }
            numfmts << "<numFmt numFmtId=\"" << numfmt_id << "\" formatCode=\"" << code << "\"/>";
            cellxfs << "<xf numFmtId=\"" << numfmt_id << "\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>";
            style_to_xf[kv.first] = xf_index;
            ++numfmt_id;
            ++xf_index;
        }
        if (style_to_xf.empty()) {
            return true; // nothing recognized needed formatting - not an error
        }

        // numFmts must be the first child of <styleSheet>, ahead of <fonts> etc. (schema order).
        size_t styleSheet_tag_end = styles_xml.find('>', styles_xml.find("<styleSheet")) + 1;
        std::string numfmts_block = "<numFmts count=\"" + std::to_string(style_to_xf.size()) + "\">" + numfmts.str() + "</numFmts>";
        styles_xml.insert(styleSheet_tag_end, numfmts_block);

        size_t cellxfs_start = styles_xml.find("<cellXfs");
        size_t cellxfs_close = styles_xml.find("</cellXfs>");
        if ((cellxfs_start == std::string::npos) || (cellxfs_close == std::string::npos)) {
            return false;
        }
        size_t cellxfs_end = cellxfs_close + strlen("</cellXfs>");
        std::string new_cellxfs = "<cellXfs count=\"" + std::to_string(xf_index) + "\">"
            "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
            + cellxfs.str() + "</cellXfs>";
        styles_xml.replace(cellxfs_start, cellxfs_end - cellxfs_start, new_cellxfs);

        for (const auto& kv : cells_by_style) {
            auto it = style_to_xf.find(kv.first);
            if (it == style_to_xf.end()) {
                continue;
            }
            std::string s_attr = " s=\"" + std::to_string(it->second) + "\"";
            for (const std::string& ref : kv.second) {
                std::string needle = "<c r=\"" + ref + "\"";
                size_t pos = sheet_xml.find(needle);
                if (pos != std::string::npos) {
                    sheet_xml.insert(pos + needle.size(), s_attr);
                }
            }
        }

        // Patching xl/styles.xml/sheet1.xml's SetCompressionStream() in place on entries that
        // were already in the archive read from disk left stale CRC32s in the saved file (Excel
        // rejected it as corrupt; confirmed with `unzip -t`) even in Immediate compression mode -
        // apparently ZipLib doesn't fully refresh an existing entry's zip-level bookkeeping this
        // way. Building a brand-new archive instead, with every entry freshly created (including
        // the unchanged ones, just copied byte-for-byte), avoids that "patch in place" path
        // entirely and only exercises ZipLib's ordinary, well-trodden write path.
        ZipArchive::Ptr out = ZipArchive::Create();
        size_t entry_count = archive->GetEntriesCount();
        for (size_t i = 0; i < entry_count; ++i) {
            ZipArchiveEntry::Ptr src_entry = archive->GetEntry((int)i);
            if (!src_entry) {
                continue;
            }
            std::string name = src_entry->GetFullName();
            ZipArchiveEntry::Ptr new_entry = out->CreateEntry(name);
            if (!new_entry) {
                return false;
            }
            bool ok;
            if (name == "xl/styles.xml") {
                std::istringstream in(styles_xml);
                ok = new_entry->SetCompressionStream(in, DeflateMethod::Create(), ZipArchiveEntry::CompressionMode::Immediate);
            } else if (name == "xl/worksheets/sheet1.xml") {
                std::istringstream in(sheet_xml);
                ok = new_entry->SetCompressionStream(in, DeflateMethod::Create(), ZipArchiveEntry::CompressionMode::Immediate);
            } else {
                std::istream* content = src_entry->GetDecompressionStream();
                if (!content) {
                    return false;
                }
                std::istringstream in(std::string((std::istreambuf_iterator<char>(*content)), std::istreambuf_iterator<char>()));
                src_entry->CloseDecompressionStream();
                ok = new_entry->SetCompressionStream(in, DeflateMethod::Create(), ZipArchiveEntry::CompressionMode::Immediate);
            }
            if (!ok) {
                return false;
            }
        }

        std::string tmp_path = path + ".tmp";
        ZipFile::SaveAndClose(out, tmp_path);
        in_file.close();
        if (std::remove(path.c_str()) != 0) {
            LogError() << "Excel export: could not remove original file before replacing it with the formatted version";
            return false;
        }
        if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
            LogError() << "Excel export: could not move the formatted file into place";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        LogError() << "Excel export: applying number formats failed: " << e.what();
        return false;
    } catch (...) {
        LogError() << "Excel export: applying number formats failed: unknown error";
        return false;
    }
}

} // namespace

bool ExportGridToExcel(wxGrid* grid, const wxString& path) {
    if (!grid) {
        return false;
    }
    try {
        std::string path_utf8(path.utf8_str());
        OpenXLSX::XLDocument doc;
        doc.create(path_utf8);
        OpenXLSX::XLWorksheet wks = doc.workbook().worksheet("Sheet1");

        int cols = grid->GetNumberCols();
        int rows = grid->GetNumberRows();
        for (int c = 0; c < cols; ++c) {
            wks.cell(1, c + 1).value() = std::string(grid->GetColLabelValue(c).utf8_str());
        }

        std::map<StyleIndex, std::vector<std::string>> cells_by_style;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                wxString text = grid->GetCellValue(r, c);
                if (text.empty()) {
                    continue;
                }
                OpenXLSX::XLCell cell = wks.cell(r + 2, c + 1);
                double serial = 0.0, amount = 0.0;
                StyleIndex style = STYLE_DEFAULT;
                if (TryParseDate(text, serial)) {
                    cell.value() = serial;
                    cells_by_style[STYLE_DATE].push_back(cell.cellReference().address());
                } else if (TryParseAmount(text, amount, style)) {
                    cell.value() = amount;
                    cells_by_style[style].push_back(cell.cellReference().address());
                } else {
                    cell.value() = std::string(text.utf8_str());
                }
            }
        }
        doc.save();
        doc.close();

        if (!ApplyNumberFormats(path_utf8, cells_by_style)) {
            LogWarn() << "Excel export: wrote data but could not apply date/currency number formats - values are still correct, just shown as plain numbers";
        }

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

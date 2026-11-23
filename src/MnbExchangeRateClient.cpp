#include <algorithm>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <utility>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <chrono>
// Downloads with cpp-httplib over OpenSSL; the Makefile defines CPPHTTPLIB_OPENSSL_SUPPORT for
// every source, so httplib is the same everywhere.
#include "httplib.h"

#include "MnbExchangeRateClient.h"
#include "ExchangeRateHistory.h"
#include "Currency.h"
#include "Logger.h"
#include "ZipFile.h"

namespace {

const char* MNB_HOST = "www.mnb.hu";
const char* MNB_PATH = "/Root/ExchangeRate/arfolyam.xlsx";

// Downloads MNB_HOST/MNB_PATH over HTTPS: the raw bytes, or "" on failure or cancellation (logged).
std::string HttpGet(const std::function<void(const std::string&)>& report_phase, FetchCancelToken* cancel_token) {
	if (report_phase) {
		report_phase("Connecting to MNB...");
	}
	const auto t_start = std::chrono::steady_clock::now();
	httplib::SSLClient client(MNB_HOST);
	client.set_connection_timeout(10, 0);
	client.set_read_timeout(20, 0);
	client.set_follow_location(true);
	std::string response;
	auto result = client.Get(MNB_PATH, [&](const char* data, size_t length) {
		response.append(data, length);
		return !(cancel_token && cancel_token->IsCancelled());
	});
	if (cancel_token && cancel_token->IsCancelled()) {
		LogWarn() << "MNB fetch: cancelled";
		return "";
	}
	if (!result) {
		LogError() << "MNB fetch: download failed (" << httplib::to_string(result.error()) << ")";
		return "";
	}
	if (result->status != 200) {
		LogError() << "MNB fetch: HTTP status " << result->status;
		return "";
	}
	LogInfo() << "MNB fetch: downloaded " << response.size() << " bytes in "
		<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count() << " ms";
	return response;
}

std::string XmlUnescape(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ) {
		if (in.compare(i, 4, "&lt;") == 0) { out += '<'; i += 4; }
		else if (in.compare(i, 4, "&gt;") == 0) { out += '>'; i += 4; }
		else if (in.compare(i, 6, "&quot;") == 0) { out += '"'; i += 6; }
		else if (in.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; }
		else if (in.compare(i, 5, "&amp;") == 0) { out += '&'; i += 5; }
		else { out += in[i]; ++i; }
	}
	return out;
}

// Reads a whole zip entry's decompressed content into a string.
std::string ReadEntry(ZipArchive::Ptr archive, const char* entry_name) {
	ZipArchiveEntry::Ptr entry = archive->GetEntry(entry_name);
	if (!entry) {
		return "";
	}
	std::istream* stream = entry->GetDecompressionStream();
	if (!stream) {
		return "";
	}
	std::ostringstream ss;
	ss << stream->rdbuf();
	return ss.str();
}

// sharedStrings.xml holds every distinct string used in the sheet, in document order; sheet
// cells reference them by 0-based index instead of embedding text directly.
std::vector<std::string> ParseSharedStrings(const std::string& xml) {
	std::vector<std::string> strings;
	size_t pos = 0;
	while ((pos = xml.find("<si", pos)) != std::string::npos) {
		size_t si_end = xml.find("</si>", pos);
		if (si_end == std::string::npos) {
			break;
		}
		std::string text;
		size_t t_pos = pos;
		while (((t_pos = xml.find("<t", t_pos)) != std::string::npos) && (t_pos < si_end)) {
			size_t tag_close = xml.find('>', t_pos);
			if ((tag_close == std::string::npos) || (tag_close > si_end)) {
				break;
			}
			if (xml[tag_close - 1] == '/') { // self-closing <t/>, i.e. empty string
				t_pos = tag_close + 1;
				continue;
			}
			size_t t_end = xml.find("</t>", tag_close);
			if ((t_end == std::string::npos) || (t_end > si_end)) {
				break;
			}
			text += xml.substr(tag_close + 1, t_end - tag_close - 1);
			t_pos = t_end + 4;
		}
		strings.push_back(XmlUnescape(text));
		pos = si_end + 5;
	}
	return strings;
}

struct Cell {
	std::string col; // column letters, e.g. "A", "BX"
	bool is_shared_string;
	std::string value; // raw <v> content: a shared-string index if is_shared_string, else a plain number
};

std::string ExtractColumnLetters(const std::string& cell_ref) {
	std::string col;
	for (char c : cell_ref) {
		if (isalpha((unsigned char)c)) {
			col += c;
		} else {
			break;
		}
	}
	return col;
}

// keep_column, when set, lets a cell be skipped entirely (no value extraction/allocation, just
// advancing past it) once its column letters are known but before its value is parsed - the
// per-row worksheet scan uses this to ignore the ~76 of ~80 published currency columns this app
// doesn't track, which matters since this runs once per row for tens of thousands of rows.
std::vector<Cell> ParseCells(const std::string& xml, size_t start, size_t end, const std::function<bool(const std::string&)>& keep_column = nullptr) {
	std::vector<Cell> cells;
	size_t pos = start;
	while (((pos = xml.find("<c ", pos)) != std::string::npos) && (pos < end)) {
		size_t tag_end = xml.find('>', pos);
		if ((tag_end == std::string::npos) || (tag_end > end)) {
			break;
		}
		std::string tag = xml.substr(pos, tag_end - pos);
		std::string col;
		size_t r_pos = tag.find("r=\"");
		if (r_pos != std::string::npos) {
			size_t r_start = r_pos + 3;
			size_t r_end = tag.find('"', r_start);
			col = ExtractColumnLetters(tag.substr(r_start, r_end - r_start));
		}
		bool self_closing = (tag[tag.size() - 1] == '/'); // self-closing <c .../>, i.e. blank cell
		size_t cell_close = std::string::npos;
		if (!self_closing) {
			cell_close = xml.find("</c>", tag_end);
			if ((cell_close == std::string::npos) || (cell_close > end)) {
				break;
			}
		}
		if (keep_column && !keep_column(col)) {
			pos = self_closing ? (tag_end + 1) : (cell_close + 4);
			continue;
		}
		Cell cell;
		cell.col = col;
		cell.is_shared_string = (tag.find("t=\"s\"") != std::string::npos);
		if (self_closing) {
			cells.push_back(cell);
			pos = tag_end + 1;
			continue;
		}
		size_t v_start = xml.find("<v>", tag_end);
		if ((v_start != std::string::npos) && (v_start < cell_close)) {
			size_t v_end = xml.find("</v>", v_start);
			cell.value = xml.substr(v_start + 3, v_end - v_start - 3);
		}
		cells.push_back(cell);
		pos = cell_close + 4;
	}
	return cells;
}

bool FindRow(const std::string& xml, int row_number, size_t& content_start, size_t& content_end) {
	std::string needle = "<row r=\"" + std::to_string(row_number) + "\"";
	size_t pos = xml.find(needle);
	if (pos == std::string::npos) {
		return false;
	}
	content_start = xml.find('>', pos) + 1;
	content_end = xml.find("</row>", content_start);
	return content_end != std::string::npos;
}

// Currencies this app tracks history for; everything else in the archive (HUF itself, and ~70
// other current/obsolete currencies MNB also publishes) is ignored.
const std::pair<const char*, CurrencyType> TRACKED_CURRENCIES[] = {
	{"EUR", EUR}, {"USD", USD}, {"GBP", GBP}, {"CHF", CHF}
};

void ParseWorksheet(ExchangeRateHistory& history, const std::vector<std::string>& shared_strings, const std::string& sheet_xml, const std::set<uint16_t>* wanted_dates) {
	size_t header_start, header_end;
	if (!FindRow(sheet_xml, 1, header_start, header_end)) {
		LogError() << "MNB fetch: could not find the header row in the MNB archive";
		return;
	}
	std::map<std::string, CurrencyType> tracked_columns; // column letters -> currency we track
	for (const Cell& cell : ParseCells(sheet_xml, header_start, header_end)) {
		if (!cell.is_shared_string || cell.value.empty()) {
			continue;
		}
		size_t idx = (size_t)atoi(cell.value.c_str());
		if (idx >= shared_strings.size()) {
			continue;
		}
		for (const auto& tracked : TRACKED_CURRENCIES) {
			if (shared_strings[idx] == tracked.first) {
				tracked_columns[cell.col] = tracked.second;
			}
		}
	}
	if (tracked_columns.empty()) {
		LogError() << "MNB fetch: none of the tracked currencies (EUR/USD/GBP/CHF) were found in the MNB archive's header row";
		return;
	}

	std::map<std::string, int> units; // column letters -> the rate's unit divisor (1, 100, or 1000)
	size_t unit_start = header_end, unit_end = header_end;
	if (FindRow(sheet_xml, 2, unit_start, unit_end)) {
		for (const Cell& cell : ParseCells(sheet_xml, unit_start, unit_end)) {
			if (!tracked_columns.count(cell.col)) {
				continue;
			}
			int unit = 1;
			if (cell.is_shared_string) {
				size_t idx = (size_t)atoi(cell.value.c_str());
				if (idx < shared_strings.size()) {
					unit = atoi(shared_strings[idx].c_str());
				}
			} else if (!cell.value.empty()) {
				unit = atoi(cell.value.c_str());
			}
			units[cell.col] = (unit > 0) ? unit : 1;
		}
	}

	auto keep_column = [&](const std::string& col) { return (col == "A") || tracked_columns.count(col) != 0; };

	size_t added = 0;
	size_t pos = unit_end;
	while ((pos = sheet_xml.find("<row ", pos)) != std::string::npos) {
		size_t row_start = sheet_xml.find('>', pos) + 1;
		size_t row_end = sheet_xml.find("</row>", row_start);
		if (row_end == std::string::npos) {
			break;
		}
		std::vector<Cell> cells = ParseCells(sheet_xml, row_start, row_end, keep_column);
		uint16_t date = 0;
		for (const Cell& cell : cells) {
			if ((cell.col == "A") && !cell.is_shared_string && !cell.value.empty()) {
				date = (uint16_t)atoi(cell.value.c_str());
				break;
			}
		}
		if ((date != 0) && (!wanted_dates || wanted_dates->count(date))) {
			for (const Cell& cell : cells) {
				auto it = tracked_columns.find(cell.col);
				if ((it == tracked_columns.end()) || cell.is_shared_string || cell.value.empty()) {
					continue; // untracked currency column, or "no quote for this date" placeholder
				}
				double rate = atof(cell.value.c_str());
				if (rate > 0.) {
					int unit = units.count(cell.col) ? units[cell.col] : 1;
					history.AddRate(it->second, date, rate / unit);
					++added;
				}
			}
		}
		pos = row_end + strlen("</row>");
	}
	LogInfo() << "MNB fetch: added/updated " << added << " rate(s) from the MNB archive";
}

} // namespace

bool DownloadAllRates(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase,
                      const std::set<uint16_t>* wanted_dates, FetchCancelToken* cancel_token) {
	auto report = [&](const std::string& phase) { if (report_phase) report_phase(phase); };

	using Clock = std::chrono::steady_clock;
	auto ms_since = [](Clock::time_point since) {
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
	};
	try {
		std::string xlsx = HttpGet(report_phase, cancel_token);
		if (xlsx.empty()) {
			return false; // already logged by HttpGet (failure or cancellation)
		}
		report("Unpacking archive...");
		auto t_unzip = Clock::now();
		std::istringstream zip_stream(xlsx);
		ZipArchive::Ptr archive = ZipArchive::Create(zip_stream);
		if (!archive) {
			LogError() << "MNB fetch: downloaded file is not a valid zip/xlsx archive";
			return false;
		}
		std::string shared_strings_xml = ReadEntry(archive, "xl/sharedStrings.xml");
		std::string sheet_xml = ReadEntry(archive, "xl/worksheets/sheet1.xml");
		LogInfo() << "MNB fetch: unzip + read entries took " << ms_since(t_unzip) << " ms";
		if (sheet_xml.empty()) {
			LogError() << "MNB fetch: could not read the worksheet from the downloaded archive";
			return false;
		}
		report("Parsing exchange rates...");
		auto t_parse = Clock::now();
		std::vector<std::string> shared_strings = ParseSharedStrings(shared_strings_xml);
		ParseWorksheet(history, shared_strings, sheet_xml, wanted_dates);
		LogInfo() << "MNB fetch: parsing (shared strings + worksheet) took " << ms_since(t_parse) << " ms";
		return true;
	} catch (const std::exception& e) {
		LogError() << "MNB fetch: unexpected error while processing the archive: " << e.what();
	} catch (...) {
		LogError() << "MNB fetch: unexpected error while processing the archive";
	}
	return false;
}

MnbExchangeRateFetcher& MnbExchangeRateFetcher::Instance() {
	static MnbExchangeRateFetcher instance;
	return instance;
}

bool MnbExchangeRateFetcher::Fetch(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase,
                                    const std::set<uint16_t>* wanted_dates, FetchCancelToken* cancel_token) {
	return DownloadAllRates(history, report_phase, wanted_dates, cancel_token);
}

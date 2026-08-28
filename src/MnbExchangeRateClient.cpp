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
#define NOMINMAX // windows.h's min/max macros break ZipLib's template code (e.g. clamp<T>) once both are included here
#include <windows.h>
#include <winhttp.h>

#include "MnbExchangeRateClient.h"
#include "ExchangeRateHistory.h"
#include "Currency.h"
#include "Logger.h"
#include "ZipFile.h"

#pragma comment(lib, "winhttp.lib")

namespace {

const wchar_t* MNB_HOST = L"www.mnb.hu";
const wchar_t* MNB_PATH = L"/Root/ExchangeRate/arfolyam.xlsx";

// Downloads the resource at MNB_HOST/MNB_PATH over HTTPS GET and returns its raw bytes, or an
// empty string on failure (already logged).
std::string HttpGet(const std::function<void(const std::string&)>& report_phase) {
	auto report = [&](const std::string& phase) { if (report_phase) report_phase(phase); };

	using Clock = std::chrono::steady_clock;
	auto ms_since = [](Clock::time_point since) {
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
	};
	auto t_start = Clock::now();

	report("Connecting to MNB...");
	std::string response;
	// WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY respects WPAD/PAC-based system proxy settings instead of
	// the legacy machine-wide "default" WinHTTP proxy (which is usually unconfigured).
	HINTERNET hSession = WinHttpOpen(L"BankAccount/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		LogError() << "MNB fetch: WinHttpOpen failed (error " << (unsigned long)GetLastError() << ")";
		return response;
	}
	LogInfo() << "MNB fetch: WinHttpOpen (incl. WPAD proxy auto-detect) took " << ms_since(t_start) << " ms";
	auto t_connect = Clock::now();
	// Without explicit timeouts, a stalled DNS lookup or connection attempt (dead network, silently
	// dropping firewall, missing proxy) blocks this call - and the whole UI thread - indefinitely.
	WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 20000);
	HINTERNET hConnect = WinHttpConnect(hSession, MNB_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		LogError() << "MNB fetch: WinHttpConnect failed (error " << (unsigned long)GetLastError() << ")";
		WinHttpCloseHandle(hSession);
		return response;
	}
	LogInfo() << "MNB fetch: WinHttpConnect (DNS + TCP connect) took " << ms_since(t_connect) << " ms";
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", MNB_PATH, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		LogError() << "MNB fetch: could not open HTTPS request (error " << (unsigned long)GetLastError() << ")";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return response;
	}
	// The TLS handshake below reliably takes 15-20+ seconds - confirmed via a plain `curl.exe`
	// request to the same URL (twice, from separate processes, both ~18-19s) that this is a
	// property of connecting to MNB's server from this network, not of this app's WinHTTP code
	// or WinHTTP's (opt-in, never-enabled-here) certificate revocation checking. There is no
	// client-side fix for this; it's inherent to reaching this specific host.
	report("Handshaking (TLS)...");
	auto t_send = Clock::now();
	bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
	if (!ok) {
		LogError() << "MNB fetch: WinHttpSendRequest failed (error " << (unsigned long)GetLastError() << ")";
	}
	LogInfo() << "MNB fetch: WinHttpSendRequest (incl. TLS handshake) took " << ms_since(t_send) << " ms";
	auto t_headers = Clock::now();
	ok = ok && (WinHttpReceiveResponse(hRequest, NULL) != FALSE);
	if (!ok) {
		LogError() << "MNB fetch: WinHttpReceiveResponse failed (error " << (unsigned long)GetLastError() << ")";
	}
	LogInfo() << "MNB fetch: WinHttpReceiveResponse (server time-to-first-byte) took " << ms_since(t_headers) << " ms";
	if (ok) {
		DWORD status = 0, status_size = sizeof(status);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
		if (status != 200) {
			LogError() << "MNB fetch: server returned HTTP status " << status;
		}
		report("Downloading exchange rate archive...");
		auto t_read = Clock::now();
		DWORD available = 0;
		while (WinHttpQueryDataAvailable(hRequest, &available) && (available > 0)) {
			std::vector<char> buffer(available);
			DWORD read = 0;
			if (!WinHttpReadData(hRequest, buffer.data(), available, &read)) {
				LogError() << "MNB fetch: WinHttpReadData failed (error " << (unsigned long)GetLastError() << ")";
				break;
			}
			response.append(buffer.data(), read);
		}
		LogInfo() << "MNB fetch: downloaded " << response.size() << " byte(s), body transfer took " << ms_since(t_read) << " ms";
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	LogInfo() << "MNB fetch: HttpGet total " << ms_since(t_start) << " ms";
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

void ParseWorksheet(ExchangeRateHistory& history, const std::vector<std::string>& shared_strings, const std::string& sheet_xml) {
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
		if (date != 0) {
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

bool DownloadAllRates(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase) {
	auto report = [&](const std::string& phase) { if (report_phase) report_phase(phase); };

	using Clock = std::chrono::steady_clock;
	auto ms_since = [](Clock::time_point since) {
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
	};
	try {
		std::string xlsx = HttpGet(report_phase);
		if (xlsx.empty()) {
			return false; // already logged by HttpGet
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
		ParseWorksheet(history, shared_strings, sheet_xml);
		LogInfo() << "MNB fetch: parsing (shared strings + worksheet) took " << ms_since(t_parse) << " ms";
		return true;
	} catch (const std::exception& e) {
		LogError() << "MNB fetch: unexpected error while processing the archive: " << e.what();
	} catch (...) {
		LogError() << "MNB fetch: unexpected error while processing the archive";
	}
	return false;
}

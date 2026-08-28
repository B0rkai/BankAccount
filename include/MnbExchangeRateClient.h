#pragma once
#include <functional>
#include <string>

class ExchangeRateHistory;

// Downloads MNB's official daily exchange-rate archive - a single spreadsheet covering every
// published currency back to 1949, refreshed daily - via a plain HTTPS GET, and adds every
// EUR/USD/GBP/CHF rate it contains to 'history'. Returns false (having logged why) on any
// network or parsing failure - callers keep using whatever rates are already cached instead of
// failing. MNB's SOAP query API blocks POST requests at the WAF level (confirmed independently
// of this app), which is why this reads the full published archive instead.
//
// This call is synchronous and can take anywhere from a few seconds to 20-30+ seconds (the TLS
// handshake to MNB's server, specifically, has been observed taking that long on its own - see
// the phase timings this logs at INFO level). If report_phase is set, it's called from whatever
// thread this function runs on with a short human-readable description of the current phase
// (connecting/downloading/parsing) - callers driving a UI progress indicator should run this
// function on a background thread and marshal report_phase's calls back to the UI thread
// themselves; this function does no threading or UI work of its own.
bool DownloadAllRates(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase = nullptr);

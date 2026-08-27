#pragma once

class ExchangeRateHistory;

// Downloads MNB's official daily exchange-rate archive - a single spreadsheet covering every
// published currency back to 1949, refreshed daily - via a plain HTTPS GET, and adds every
// EUR/USD/GBP/CHF rate it contains to 'history'. Returns false (having logged why) on any
// network or parsing failure - callers keep using whatever rates are already cached instead of
// failing. MNB's SOAP query API blocks POST requests at the WAF level (confirmed independently
// of this app), which is why this reads the full published archive instead.
bool DownloadAllRates(ExchangeRateHistory& history);

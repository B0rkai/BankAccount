#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <cstdint>

class ExchangeRateHistory;

// Cooperative cancellation for an in-flight DownloadAllRates() call. WinHttpCloseHandle() on a
// handle that has a blocking call outstanding on another thread makes that call fail promptly -
// the documented way to cancel a synchronous WinHTTP operation mid-flight - so Cancel() just
// closes whichever WinHTTP handle HttpGet() currently has open. Safe to call from any thread,
// including while no fetch is in progress (a no-op then).
class FetchCancelToken {
public:
    void Cancel();
    bool IsCancelled() const { return m_cancelled.load(); }
    void SetActiveHandle(void* handle); // WinHTTP HINTERNET, kept as void* so this header stays windows.h-free
    // Atomically clears 'handle' iff it's still the one registered, returning true in that case.
    // Both the normal cleanup path and Cancel() must go through this before closing a handle, so
    // that whichever one reaches it first "wins" and the other skips its own close - otherwise a
    // Cancel() racing against normal completion could close the same handle twice.
    bool TryClaimHandle(void* handle);
private:
    std::atomic<bool> m_cancelled{ false };
    std::mutex m_mutex;
    void* m_active_handle = nullptr;
};

// Downloads MNB's official daily exchange-rate archive - a single spreadsheet covering every
// published currency back to 1949, refreshed daily - via a plain HTTPS GET, and adds every
// EUR/USD/GBP/CHF rate it contains to 'history'. Returns false (having logged why) on any
// network or parsing failure - callers keep using whatever rates are already cached instead of
// failing. MNB's SOAP query API blocks POST requests at the WAF level (confirmed independently
// of this app), which is why this reads the full published archive instead.
//
// If wanted_dates is non-null, only rates for dates in that set are kept - the archive covers
// every day since 1949 for currencies this app doesn't even always hold, which bloats the saved
// database with tens of thousands of rates nobody's transactions ever need; pass the set of
// dates actually referenced by real transactions to keep only what's useful. Pass nullptr to
// keep the old "store everything" behavior.
//
// This call is synchronous and can take anywhere from a few seconds to 20-30+ seconds (the TLS
// handshake to MNB's server, specifically, has been observed taking that long on its own - see
// the phase timings this logs at INFO level), and - rarely - can hang far longer if the
// connection dies without the OS noticing. If report_phase is set, it's called from whatever
// thread this function runs on with a short human-readable description of the current phase
// (connecting/downloading/parsing) - callers driving a UI progress indicator should run this
// function on a background thread and marshal report_phase's calls back to the UI thread
// themselves; this function does no threading or UI work of its own. If cancel_token is set,
// calling cancel_token->Cancel() from another thread aborts the fetch promptly instead of
// leaving it to hang indefinitely.
bool DownloadAllRates(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase = nullptr,
                      const std::set<uint16_t>* wanted_dates = nullptr, FetchCancelToken* cancel_token = nullptr);

// Injectable seam around the network fetch above, so AccountManager::UpdateExchangeRates() can
// be exercised (e.g. in a test) without ever making a real HTTPS request to MNB - the domain
// code depends only on this interface, not on WinHTTP or the MNB archive format directly.
class IExchangeRateFetcher {
public:
    virtual ~IExchangeRateFetcher() = default;
    virtual bool Fetch(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase,
                        const std::set<uint16_t>* wanted_dates, FetchCancelToken* cancel_token) = 0;
};

// Production default: a thin pass-through to DownloadAllRates() above - MnbExchangeRateClient's
// actual WinHTTP/parsing logic is completely unchanged by this seam's existence.
class MnbExchangeRateFetcher : public IExchangeRateFetcher {
public:
    static MnbExchangeRateFetcher& Instance();
    virtual bool Fetch(ExchangeRateHistory& history, const std::function<void(const std::string&)>& report_phase,
                        const std::set<uint16_t>* wanted_dates, FetchCancelToken* cancel_token) override;
};

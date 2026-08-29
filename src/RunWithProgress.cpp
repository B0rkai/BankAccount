#include "RunWithProgress.h"

#include "wx/progdlg.h"
#include "wx/utils.h"

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

void RunBlockingWithProgress(wxWindow* parent, const wxString& title, const wxString& initial_message,
                              const std::function<void(const std::function<void(const std::string&)>&)>& work,
                              const std::function<void()>& on_cancel, int hard_timeout_seconds) {
    std::mutex mtx;
    std::string phase(initial_message.utf8_str());
    std::atomic<bool> done{ false };

    std::function<void(const std::string&)> report_phase = [&](const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx);
        phase = text;
    };

    std::thread worker([&]() {
        work(report_phase);
        done = true;
    });

    int style = wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_SMOOTH;
    if (on_cancel) {
        style |= wxPD_CAN_ABORT;
    }
    // wxProgressDialog::Pulse() pumps pending events internally (the standard wx idiom for
    // keeping a dialog responsive from a polling loop like this one), so the dialog - and the
    // rest of the app - stays repainting/interactive for the whole wait instead of freezing.
    wxProgressDialog dialog(title, initial_message, 100, parent, style);
    auto start = std::chrono::steady_clock::now();
    bool cancel_sent = false;
    while (!done.load()) {
        std::string current;
        {
            std::lock_guard<std::mutex> lock(mtx);
            current = phase;
        }
        bool user_cancelled = dialog.WasCancelled();
        bool timed_out = (hard_timeout_seconds > 0) &&
            (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() >= hard_timeout_seconds);
        if (!cancel_sent && on_cancel && (user_cancelled || timed_out)) {
            // Fire exactly once: on_cancel is expected to make the underlying operation fail
            // promptly (e.g. by closing an OS handle it's blocked on), not to be safely callable
            // repeatedly, and there's nothing more useful to do than wait for 'done' either way.
            cancel_sent = true;
            dialog.Pulse(timed_out ? wxString("Taking too long - cancelling...") : wxString("Cancelling..."));
            on_cancel();
        } else {
            dialog.Pulse(wxString(current.c_str(), wxConvUTF8));
        }
        wxMilliSleep(80);
    }
    worker.join();
}

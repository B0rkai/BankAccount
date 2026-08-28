#include "RunWithProgress.h"

#include "wx/progdlg.h"
#include "wx/utils.h"

#include <thread>
#include <mutex>
#include <atomic>

void RunBlockingWithProgress(wxWindow* parent, const wxString& title, const wxString& initial_message,
                              const std::function<void(const std::function<void(const std::string&)>&)>& work) {
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

    // wxProgressDialog::Pulse() pumps pending events internally (the standard wx idiom for
    // keeping a dialog responsive from a polling loop like this one), so the dialog - and the
    // rest of the app - stays repainting/interactive for the whole wait instead of freezing.
    wxProgressDialog dialog(title, initial_message, 100, parent, wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_SMOOTH);
    while (!done.load()) {
        std::string current;
        {
            std::lock_guard<std::mutex> lock(mtx);
            current = phase;
        }
        dialog.Pulse(wxString(current.c_str(), wxConvUTF8));
        wxMilliSleep(80);
    }
    worker.join();
}

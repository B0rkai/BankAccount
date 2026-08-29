#pragma once
#include "wx/string.h"
#include <functional>
#include <string>

class wxWindow;

// Runs 'work' on a background thread while showing an app-modal, indeterminate wxProgressDialog
// on the main thread, so a slow synchronous backend call (network fetch, heavy parse, ...) no
// longer freezes the whole UI ("Not Responding") for its duration. Blocks the calling (main)
// thread until 'work' completes - same as calling it directly would from the caller's point of
// view - the only difference is the UI keeps repainting/responding meanwhile.
//
// 'work' is called on the background thread and receives a thread-safe report_phase callback it
// can call (from that same background thread) with a short human-readable description of what's
// currently happening; the dialog's message is updated with the latest one on each pulse.
//
// If on_cancel is set, the dialog shows a Cancel button, and clicking it (or a wall-clock wait
// past hard_timeout_seconds, whichever comes first) calls on_cancel() exactly once, from the
// main thread. on_cancel is responsible for actually making 'work' return promptly (e.g. by
// closing whatever OS handle it's blocked on) - this function has no way to forcibly stop a
// background thread itself, short of leaking it (see below), which would defeat the purpose.
// Without on_cancel, a 'work' that hangs forever hangs this call (and the whole UI) forever too -
// this happened for real once with the MNB exchange-rate fetch, which is why every caller of
// this function should now supply one together with a way to cancel the specific thing it does.
void RunBlockingWithProgress(wxWindow* parent, const wxString& title, const wxString& initial_message,
                              const std::function<void(const std::function<void(const std::string&)>&)>& work,
                              const std::function<void()>& on_cancel = nullptr,
                              int hard_timeout_seconds = 60);

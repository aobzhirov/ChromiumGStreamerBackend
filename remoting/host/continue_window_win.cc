// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/current_module.h"
#include "remoting/host/continue_window.h"
#include "remoting/host/win/core_resource.h"

namespace remoting {

namespace {

class ContinueWindowWin : public ContinueWindow {
 public:
  ContinueWindowWin();
  ~ContinueWindowWin() override;

 protected:
  // ContinueWindow overrides.
  void ShowUi() override;
  void HideUi() override;

 private:
  static BOOL CALLBACK DialogProc(HWND hwmd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);

  BOOL OnDialogMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void EndDialog();

  HWND hwnd_;

  DISALLOW_COPY_AND_ASSIGN(ContinueWindowWin);
};

ContinueWindowWin::ContinueWindowWin()
    : hwnd_(nullptr) {
}

ContinueWindowWin::~ContinueWindowWin() {
  EndDialog();
}

void ContinueWindowWin::ShowUi() {
  DCHECK(CalledOnValidThread());
  DCHECK(!hwnd_);

  hwnd_ = CreateDialogParam(CURRENT_MODULE(), MAKEINTRESOURCE(IDD_CONTINUE),
                            nullptr, (DLGPROC)DialogProc, (LPARAM) this);
  if (!hwnd_) {
    LOG(ERROR) << "Unable to create Disconnect dialog for remoting.";
    return;
  }

  ShowWindow(hwnd_, SW_SHOW);
}

void ContinueWindowWin::HideUi() {
  DCHECK(CalledOnValidThread());

  EndDialog();
}

BOOL CALLBACK ContinueWindowWin::DialogProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam) {
  ContinueWindowWin* win = nullptr;
  if (msg == WM_INITDIALOG) {
    win = reinterpret_cast<ContinueWindowWin*>(lParam);
    CHECK(win);
    SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)win);
  } else {
    LONG_PTR lp = GetWindowLongPtr(hwnd, DWLP_USER);
    win = reinterpret_cast<ContinueWindowWin*>(lp);
  }
  if (win == nullptr)
    return FALSE;
  return win->OnDialogMessage(hwnd, msg, wParam, lParam);
}

BOOL ContinueWindowWin::OnDialogMessage(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
  DCHECK(CalledOnValidThread());

  switch (msg) {
    case WM_CLOSE:
      // Ignore close messages.
      return TRUE;
    case WM_DESTROY:
      // Ensure we don't try to use the HWND anymore.
      hwnd_ = nullptr;
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDC_CONTINUE_DEFAULT:
          ContinueSession();
          ::EndDialog(hwnd, LOWORD(wParam));
          hwnd_ = nullptr;
          return TRUE;
        case IDC_CONTINUE_CANCEL:
          DisconnectSession();
          ::EndDialog(hwnd, LOWORD(wParam));
          hwnd_ = nullptr;
          return TRUE;
      }
  }
  return FALSE;
}

void ContinueWindowWin::EndDialog() {
  DCHECK(CalledOnValidThread());

  if (hwnd_) {
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

}  // namespace

// static
scoped_ptr<HostWindow> HostWindow::CreateContinueWindow() {
  return make_scoped_ptr(new ContinueWindowWin());
}

}  // namespace remoting

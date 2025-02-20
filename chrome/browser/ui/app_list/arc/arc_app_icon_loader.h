// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_LOADER_H_
#define CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_LOADER_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/app_icon_loader.h"
#include "chrome/browser/ui/app_list/arc/arc_app_icon.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"

class Profile;

class ArcAppIconLoader : public AppIconLoader,
                         public ArcAppListPrefs::Observer,
                         public ArcAppIcon::Observer {
 public:
  ArcAppIconLoader(Profile* profile,
                   int icon_size,
                   AppIconLoaderDelegate* delegate);
  ~ArcAppIconLoader() override;

  // Overrides AppIconLoader:
  bool CanLoadImageForApp(const std::string& app_id) override;
  void FetchImage(const std::string& id) override;
  void ClearImage(const std::string& id) override;
  void UpdateImage(const std::string& id) override;

  // Overrides ArcAppListPrefs::Observer:
  void OnAppReadyChanged(const std::string& id, bool ready) override;
  void OnAppIconUpdated(const std::string& id,
                        ui::ScaleFactor scale_factor) override;

  // Overrides ArcAppIcon::Observer:
  void OnIconUpdated(ArcAppIcon* icon) override;

 private:
  using AppIDToIconMap = std::map<std::string, scoped_ptr<ArcAppIcon>>;

  // Unowned pointer.
  ArcAppListPrefs* arc_prefs_;

  AppIDToIconMap icon_map_;

  DISALLOW_COPY_AND_ASSIGN(ArcAppIconLoader);
};

#endif  // CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_LOADER_H_

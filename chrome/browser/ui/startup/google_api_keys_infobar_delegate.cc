// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/google_api_keys_infobar_delegate.h"

#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/grit/chromium_strings.h"
#include "components/infobars/core/infobar.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/google_api_keys.h"
#include "grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"


// static
void GoogleApiKeysInfoBarDelegate::Create(InfoBarService* infobar_service) {
  if (google_apis::HasKeysConfigured())
    return;

  infobar_service->AddInfoBar(infobar_service->CreateConfirmInfoBar(
      scoped_ptr<ConfirmInfoBarDelegate>(new GoogleApiKeysInfoBarDelegate())));
}

GoogleApiKeysInfoBarDelegate::GoogleApiKeysInfoBarDelegate()
    : ConfirmInfoBarDelegate() {
}

GoogleApiKeysInfoBarDelegate::~GoogleApiKeysInfoBarDelegate() {
}

infobars::InfoBarDelegate::InfoBarIdentifier
GoogleApiKeysInfoBarDelegate::GetIdentifier() const {
  return GOOGLE_API_KEYS_INFOBAR_DELEGATE;
}

base::string16 GoogleApiKeysInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_MISSING_GOOGLE_API_KEYS);
}

int GoogleApiKeysInfoBarDelegate::GetButtons() const {
  return BUTTON_NONE;
}

base::string16 GoogleApiKeysInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

GURL GoogleApiKeysInfoBarDelegate::GetLinkURL() const {
  return GURL(google_apis::kAPIKeysDevelopersHowToURL);
}

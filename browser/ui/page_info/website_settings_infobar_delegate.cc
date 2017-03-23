// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/page_info/website_settings_infobar_delegate.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/infobars/core/infobar.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

// static
void WebsiteSettingsInfoBarDelegate::Create(InfoBarService* infobar_service) {
  infobar_service->AddInfoBar(infobar_service->CreateConfirmInfoBar(
      std::unique_ptr<ConfirmInfoBarDelegate>(
          new WebsiteSettingsInfoBarDelegate())));
}

WebsiteSettingsInfoBarDelegate::WebsiteSettingsInfoBarDelegate()
    : ConfirmInfoBarDelegate() {}

WebsiteSettingsInfoBarDelegate::~WebsiteSettingsInfoBarDelegate() {}

infobars::InfoBarDelegate::Type WebsiteSettingsInfoBarDelegate::GetInfoBarType()
    const {
  return PAGE_ACTION_TYPE;
}

infobars::InfoBarDelegate::InfoBarIdentifier
WebsiteSettingsInfoBarDelegate::GetIdentifier() const {
  return WEBSITE_SETTINGS_INFOBAR_DELEGATE;
}

const gfx::VectorIcon& WebsiteSettingsInfoBarDelegate::GetVectorIcon() const {
  return kGlobeIcon;
}

base::string16 WebsiteSettingsInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_WEBSITE_SETTINGS_INFOBAR_TEXT);
}

int WebsiteSettingsInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

base::string16 WebsiteSettingsInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(IDS_WEBSITE_SETTINGS_INFOBAR_BUTTON);
}

bool WebsiteSettingsInfoBarDelegate::Accept() {
  content::WebContents* web_contents =
      InfoBarService::WebContentsFromInfoBar(infobar());
  web_contents->GetController().Reload(content::ReloadType::NORMAL, true);
  return true;
}

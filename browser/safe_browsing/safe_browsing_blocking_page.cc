// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Implementation of the SafeBrowsingBlockingPage class.

#include "chrome/browser/safe_browsing/safe_browsing_blocking_page.h"

#include "base/lazy_instance.h"
#include "chrome/browser/interstitials/chrome_controller_client.h"
#include "chrome/browser/interstitials/chrome_metrics_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/safe_browsing/threat_details.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing_db/safe_browsing_prefs.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

using content::BrowserThread;
using content::InterstitialPage;
using content::WebContents;
using security_interstitials::SafeBrowsingErrorUI;
using security_interstitials::SecurityInterstitialControllerClient;

namespace safe_browsing {

namespace {

// Constants for the Experience Sampling instrumentation.
const char kEventNameMalware[] = "safebrowsing_interstitial_";
const char kEventNameHarmful[] = "harmful_interstitial_";
const char kEventNamePhishing[] = "phishing_interstitial_";
const char kEventNameOther[] = "safebrowsing_other_interstitial_";

}  // namespace

// static
SafeBrowsingBlockingPageFactory* SafeBrowsingBlockingPage::factory_ = NULL;

// The default SafeBrowsingBlockingPageFactory.  Global, made a singleton so we
// don't leak it.
class SafeBrowsingBlockingPageFactoryImpl
    : public SafeBrowsingBlockingPageFactory {
 public:
  SafeBrowsingBlockingPage* CreateSafeBrowsingPage(
      BaseUIManager* ui_manager,
      WebContents* web_contents,
      const GURL& main_frame_url,
      const SafeBrowsingBlockingPage::UnsafeResourceList& unsafe_resources)
      override {
    // Create appropriate display options for this blocking page.
    PrefService* prefs =
        Profile::FromBrowserContext(web_contents->GetBrowserContext())
            ->GetPrefs();
    bool is_extended_reporting_opt_in_allowed =
        prefs->GetBoolean(prefs::kSafeBrowsingExtendedReportingOptInAllowed);
    bool is_proceed_anyway_disabled =
        prefs->GetBoolean(prefs::kSafeBrowsingProceedAnywayDisabled);

    // Determine if any prefs need to be updated prior to showing the security
    // interstitial. This must happen before querying IsScout to populate the
    // Display Options below.
    safe_browsing::UpdatePrefsBeforeSecurityInterstitial(prefs);

    SafeBrowsingErrorUI::SBErrorDisplayOptions display_options(
        BaseBlockingPage::IsMainPageLoadBlocked(unsafe_resources),
        is_extended_reporting_opt_in_allowed,
        web_contents->GetBrowserContext()->IsOffTheRecord(),
        IsExtendedReportingEnabled(*prefs), IsScout(*prefs),
        is_proceed_anyway_disabled);

    return new SafeBrowsingBlockingPage(ui_manager, web_contents,
                                        main_frame_url, unsafe_resources,
                                        display_options);
  }

 private:
  friend struct base::LazyInstanceTraitsBase<
      SafeBrowsingBlockingPageFactoryImpl>;

  SafeBrowsingBlockingPageFactoryImpl() { }

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingBlockingPageFactoryImpl);
};

static base::LazyInstance<SafeBrowsingBlockingPageFactoryImpl>::DestructorAtExit
    g_safe_browsing_blocking_page_factory_impl = LAZY_INSTANCE_INITIALIZER;

// static
content::InterstitialPageDelegate::TypeID
    SafeBrowsingBlockingPage::kTypeForTesting =
        &SafeBrowsingBlockingPage::kTypeForTesting;

SafeBrowsingBlockingPage::SafeBrowsingBlockingPage(
    BaseUIManager* ui_manager,
    WebContents* web_contents,
    const GURL& main_frame_url,
    const UnsafeResourceList& unsafe_resources,
    const SafeBrowsingErrorUI::SBErrorDisplayOptions& display_options)
    : BaseBlockingPage(
          ui_manager,
          web_contents,
          unsafe_resources[0].url,
          unsafe_resources,
          CreateControllerClient(web_contents, unsafe_resources, ui_manager),
          display_options) {
  // Start computing threat details. They will be sent only
  // if the user opts-in on the blocking page later.
  // If there's more than one malicious resources, it means the user
  // clicked through the first warning, so we don't prepare additional
  // reports.
  if (unsafe_resources.size() == 1 &&
      ShouldReportThreatDetails(unsafe_resources[0].threat_type) &&
      threat_details_.get() == NULL &&
      sb_error_ui()->CanShowExtendedReportingOption()) {
    threat_details_ = ThreatDetails::NewThreatDetails(ui_manager, web_contents,
                                                      unsafe_resources[0]);
  }
}

bool SafeBrowsingBlockingPage::ShouldReportThreatDetails(
    SBThreatType threat_type) {
  return threat_type == SB_THREAT_TYPE_URL_PHISHING ||
         threat_type == SB_THREAT_TYPE_URL_MALWARE ||
         threat_type == SB_THREAT_TYPE_URL_UNWANTED ||
         threat_type == SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL ||
         threat_type == SB_THREAT_TYPE_CLIENT_SIDE_MALWARE_URL;
}

SafeBrowsingBlockingPage::~SafeBrowsingBlockingPage() {
}

void SafeBrowsingBlockingPage::OverrideRendererPrefs(
      content::RendererPreferences* prefs) {
  Profile* profile = Profile::FromBrowserContext(
      web_contents()->GetBrowserContext());
  renderer_preferences_util::UpdateFromSystemSettings(
      prefs, profile, web_contents());
}

void SafeBrowsingBlockingPage::HandleSubresourcesAfterProceed() {
  // Check to see if some new notifications of unsafe resources have been
  // received while we were showing the interstitial.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  UnsafeResourceMap::iterator iter = unsafe_resource_map->find(web_contents());
  if (iter != unsafe_resource_map->end() && !iter->second.empty()) {
    // All queued unsafe resources should be for the same page:
    UnsafeResourceList unsafe_resources = iter->second;
    content::NavigationEntry* entry =
        unsafe_resources[0].GetNavigationEntryForResource();
    // Build an interstitial for all the unsafe resources notifications.
    // Don't show it now as showing an interstitial while an interstitial is
    // already showing would cause DontProceed() to be invoked.
    SafeBrowsingBlockingPage* blocking_page = factory_->CreateSafeBrowsingPage(
        ui_manager(), web_contents(), entry ? entry->GetURL() : GURL(),
        unsafe_resources);
    unsafe_resource_map->erase(iter);

    // Now that this interstitial is gone, we can show the new one.
    blocking_page->Show();
  }
}

content::InterstitialPageDelegate::TypeID
SafeBrowsingBlockingPage::GetTypeForTesting() const {
  return SafeBrowsingBlockingPage::kTypeForTesting;
}

void SafeBrowsingBlockingPage::FinishThreatDetails(const base::TimeDelta& delay,
                                                   bool did_proceed,
                                                   int num_visits) {
  if (threat_details_.get() == NULL)
    return;  // Not all interstitials have threat details (eg., incognito mode).

  const bool enabled =
      sb_error_ui()->is_extended_reporting_enabled() &&
      sb_error_ui()->is_extended_reporting_opt_in_allowed();
  if (!enabled)
    return;

  controller()->metrics_helper()->RecordUserInteraction(
      security_interstitials::MetricsHelper::EXTENDED_REPORTING_IS_ENABLED);
  // Finish the malware details collection, send it over.
  BrowserThread::PostDelayedTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&ThreatDetails::FinishCollection, threat_details_,
                 did_proceed, num_visits),
      delay);
}

// static
SafeBrowsingBlockingPage* SafeBrowsingBlockingPage::CreateBlockingPage(
    BaseUIManager* ui_manager,
    WebContents* web_contents,
    const GURL& main_frame_url,
    const UnsafeResource& unsafe_resource) {
  const UnsafeResourceList resources{unsafe_resource};
  // Set up the factory if this has not been done already (tests do that
  // before this method is called).
  if (!factory_)
    factory_ = g_safe_browsing_blocking_page_factory_impl.Pointer();
  return factory_->CreateSafeBrowsingPage(ui_manager, web_contents,
                                          main_frame_url, resources);
}

// static
void SafeBrowsingBlockingPage::ShowBlockingPage(
    BaseUIManager* ui_manager,
    const UnsafeResource& unsafe_resource) {
  DVLOG(1) << __func__ << " " << unsafe_resource.url.spec();
  WebContents* web_contents = unsafe_resource.web_contents_getter.Run();

  if (InterstitialPage::GetInterstitialPage(web_contents) &&
      unsafe_resource.is_subresource) {
    // This is an interstitial for a page's resource, let's queue it.
    UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
    (*unsafe_resource_map)[web_contents].push_back(unsafe_resource);
  } else {
    // There is no interstitial currently showing in that tab, or we are about
    // to display a new one for the main frame. If there is already an
    // interstitial, showing the new one will automatically hide the old one.
    content::NavigationEntry* entry =
        unsafe_resource.GetNavigationEntryForResource();
    SafeBrowsingBlockingPage* blocking_page =
        CreateBlockingPage(ui_manager, web_contents,
                           entry ? entry->GetURL() : GURL(), unsafe_resource);
    blocking_page->Show();
  }
}

// static
std::string SafeBrowsingBlockingPage::GetSamplingEventName(
    SafeBrowsingErrorUI::SBInterstitialReason interstitial_reason) {
  switch (interstitial_reason) {
    case SafeBrowsingErrorUI::SB_REASON_MALWARE:
      return kEventNameMalware;
    case SafeBrowsingErrorUI::SB_REASON_HARMFUL:
      return kEventNameHarmful;
    case SafeBrowsingErrorUI::SB_REASON_PHISHING:
      return kEventNamePhishing;
    default:
      return kEventNameOther;
  }
}

// static
std::unique_ptr<SecurityInterstitialControllerClient>
SafeBrowsingBlockingPage::CreateControllerClient(
    WebContents* web_contents,
    const UnsafeResourceList& unsafe_resources,
    const BaseUIManager* ui_manager) {
  Profile* profile = Profile::FromBrowserContext(
      web_contents->GetBrowserContext());
  DCHECK(profile);

  std::unique_ptr<ChromeMetricsHelper> metrics_helper =
      base::MakeUnique<ChromeMetricsHelper>(
          web_contents, unsafe_resources[0].url,
          GetReportingInfo(unsafe_resources),
          GetSamplingEventName(GetInterstitialReason(unsafe_resources)));

  return base::MakeUnique<SecurityInterstitialControllerClient>(
      web_contents, std::move(metrics_helper), profile->GetPrefs(),
      ui_manager->app_locale(), ui_manager->default_safe_page());
}

}  // namespace safe_browsing

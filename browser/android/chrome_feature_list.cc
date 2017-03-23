// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/chrome_feature_list.h"

#include <stddef.h>

#include <string>

#include "base/android/jni_string.h"
#include "base/feature_list.h"
#include "base/macros.h"
#include "base/metrics/field_trial_params.h"
#include "chrome/common/chrome_features.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/ntp_snippets/features.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "content/public/common/content_features.h"
#include "jni/ChromeFeatureList_jni.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace chrome {
namespace android {

namespace {

// Array of features exposed through the Java ChromeFeatureList API. Entries in
// this array may either refer to features defined in the header of this file or
// in other locations in the code base (e.g. chrome/, components/, etc).
const base::Feature* kFeaturesExposedToJava[] = {
    &autofill::kAutofillScanCardholderName,
    &features::kConsistentOmniboxGeolocation,
    &features::kCredentialManagementAPI,
    &features::kNativeAndroidHistoryManager,
    &features::kServiceWorkerPaymentApps,
    &features::kSimplifiedFullscreenUI,
    &features::kVrShell,
    &features::kWebPayments,
    &kAndroidPayIntegrationV1,
    &kAndroidPayIntegrationV2,
    &kAndroidPaymentApps,
    &kAndroidPaymentAppsFilter,
    &kCCTExternalLinkHandling,
    &kCCTPostMessageAPI,
    &kChromeHomeFeature,
    &kContextualSearchSingleActions,
    &kContextualSearchUrlActions,
    &kCustomFeedbackUi,
    &kImportantSitesInCBD,
    &kImprovedA2HS,
    &kNewPhotoPicker,
    &kNoCreditCardAbort,
    &kNTPCondensedLayoutFeature,
    &kNTPCondensedTileLayoutFeature,
    &kNTPFakeOmniboxTextFeature,
    &kNTPLaunchAfterInactivity,
    &kNTPOfflinePagesFeature,
    &NTPShowGoogleGInOmniboxFeature,
    &kPhysicalWebFeature,
    &kPhysicalWebSharing,
    &kSpecialLocaleFeature,
    &kSpecialLocaleWrapper,
    &kTabsInCBD,
    &kTabReparenting,
    &kUploadCrashReportsUsingJobScheduler,
    &kWebPaymentsModifiers,
    &kWebPaymentsSingleAppUiSkip,
    &kWebVRCardboardSupport,
    &ntp_snippets::kIncreasedVisibility,
    &ntp_snippets::kForeignSessionsSuggestionsFeature,
    &ntp_snippets::kOfflineBadgeFeature,
    &ntp_snippets::kSaveToOfflineFeature,
    &offline_pages::kBackgroundLoaderForDownloadsFeature,
    &offline_pages::kOfflinePagesCTFeature,  // See crbug.com/620421.
    &offline_pages::kOfflinePagesSharingFeature,
    &password_manager::features::kViewPasswords,
};

const base::Feature* FindFeatureExposedToJava(const std::string& feature_name) {
  for (size_t i = 0; i < arraysize(kFeaturesExposedToJava); ++i) {
    if (kFeaturesExposedToJava[i]->name == feature_name)
      return kFeaturesExposedToJava[i];
  }
  NOTREACHED() << "Queried feature cannot be found in ChromeFeatureList: "
               << feature_name;
  return nullptr;
}

}  // namespace

// Alphabetical:
const base::Feature kAndroidPayIntegrationV1{"AndroidPayIntegrationV1",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidPayIntegrationV2{"AndroidPayIntegrationV2",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidPaymentApps{"AndroidPaymentApps",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kAndroidPaymentAppsFilter{
    "AndroidPaymentAppsFilter", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCCTExternalLinkHandling{"CCTExternalLinkHandling",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTPostMessageAPI{"CCTPostMessageAPI",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kChromeHomeFeature{"ChromeHome",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchSingleActions{
    "ContextualSearchSingleActions", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kContextualSearchUrlActions{
    "ContextualSearchUrlActions", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCustomFeedbackUi{"CustomFeedbackUi",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kDownloadAutoResumptionThrottling{
    "DownloadAutoResumptionThrottling", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kImportantSitesInCBD{"ImportantSitesInCBD",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

// Makes "Add to Home screen" in the app menu generate an APK for the shortcut
// URL which opens Chrome in fullscreen.
const base::Feature kImprovedA2HS{"ImprovedA2HS",
                                  base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNewPhotoPicker{"NewPhotoPicker",
                                    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNoCreditCardAbort{"NoCreditCardAbort",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNTPCondensedLayoutFeature{
    "NTPCondensedLayout", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNTPCondensedTileLayoutFeature{
    "NTPCondensedTileLayout", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNTPFakeOmniboxTextFeature{
    "NTPFakeOmniboxText", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNTPLaunchAfterInactivity{
    "NTPLaunchAfterInactivity", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNTPOfflinePagesFeature{"NTPOfflinePages",
                                            base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature NTPShowGoogleGInOmniboxFeature{
    "NTPShowGoogleGInOmnibox", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPhysicalWebFeature{"PhysicalWeb",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kPhysicalWebSharing{"PhysicalWebSharing",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSpecialLocaleFeature{"SpecialLocale",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSpecialLocaleWrapper{"SpecialLocaleWrapper",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTabsInCBD{"TabsInCBD", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabReparenting{"TabReparenting",
                                    base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kUploadCrashReportsUsingJobScheduler{
    "UploadCrashReportsUsingJobScheduler", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kUserMediaScreenCapturing{
    "UserMediaScreenCapturing", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kWebPaymentsModifiers{"WebPaymentsModifiers",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kWebPaymentsSingleAppUiSkip{
    "WebPaymentsSingleAppUiSkip", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kWebVRCardboardSupport{
    "WebVRCardboardSupport", base::FEATURE_ENABLED_BY_DEFAULT};

static jboolean IsEnabled(JNIEnv* env,
                          const JavaParamRef<jclass>& clazz,
                          const JavaParamRef<jstring>& jfeature_name) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  return base::FeatureList::IsEnabled(*feature);
}

static ScopedJavaLocalRef<jstring> GetFieldTrialParamByFeature(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  const std::string& param_value =
      base::GetFieldTrialParamValueByFeature(*feature, param_name);
  return ConvertUTF8ToJavaString(env, param_value);
}

static jint GetFieldTrialParamByFeatureAsInt(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jint jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsInt(*feature, param_name,
                                                jdefault_value);
}

static jdouble GetFieldTrialParamByFeatureAsDouble(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jdouble jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsDouble(*feature, param_name,
                                                   jdefault_value);
}

static jboolean GetFieldTrialParamByFeatureAsBoolean(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jboolean jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsBool(*feature, param_name,
                                                 jdefault_value);
}

bool RegisterChromeFeatureListJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android
}  // namespace chrome

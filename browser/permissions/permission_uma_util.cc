// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/permission_uma_util.h"

#include <utility>

#include "base/command_line.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker.h"
#include "chrome/browser/permissions/permission_request.h"
#include "chrome/browser/permissions/permission_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/browser_sync/profile_sync_service.h"
#include "components/prefs/pref_service.h"
#include "components/rappor/public/rappor_utils.h"
#include "components/rappor/rappor_service_impl.h"
#include "content/public/browser/permission_type.h"
#include "content/public/common/origin_util.h"
#include "url/gurl.h"

// UMA keys need to be statically initialized so plain function would not
// work. Use macros instead.
#define PERMISSION_ACTION_UMA(secure_origin, permission, permission_secure,  \
                              permission_insecure, action)                   \
  UMA_HISTOGRAM_ENUMERATION(permission, static_cast<int>(action),            \
                            static_cast<int>(PermissionAction::NUM));        \
  if (secure_origin) {                                                       \
    UMA_HISTOGRAM_ENUMERATION(permission_secure, static_cast<int>(action),   \
                              static_cast<int>(PermissionAction::NUM));      \
  } else {                                                                   \
    UMA_HISTOGRAM_ENUMERATION(permission_insecure, static_cast<int>(action), \
                              static_cast<int>(PermissionAction::NUM));      \
  }

#define PERMISSION_BUBBLE_TYPE_UMA(metric_name, permission_bubble_type) \
  UMA_HISTOGRAM_ENUMERATION(                                            \
      metric_name,                                                      \
      static_cast<base::HistogramBase::Sample>(permission_bubble_type), \
      static_cast<base::HistogramBase::Sample>(PermissionRequestType::NUM))

#define PERMISSION_BUBBLE_GESTURE_TYPE_UMA(gesture_metric_name,              \
                                           no_gesture_metric_name,           \
                                           gesture_type,                     \
                                           permission_bubble_type)           \
  if (gesture_type == PermissionRequestGestureType::GESTURE) {               \
    PERMISSION_BUBBLE_TYPE_UMA(gesture_metric_name, permission_bubble_type); \
  } else if (gesture_type == PermissionRequestGestureType::NO_GESTURE) {     \
    PERMISSION_BUBBLE_TYPE_UMA(no_gesture_metric_name,                       \
                               permission_bubble_type);                      \
  }

using content::PermissionType;

namespace {

const std::string GetRapporMetric(ContentSettingsType permission,
                                  PermissionAction action) {
  std::string action_str;
  switch (action) {
    case PermissionAction::GRANTED:
      action_str = "Granted";
      break;
    case PermissionAction::DENIED:
      action_str = "Denied";
      break;
    case PermissionAction::DISMISSED:
      action_str = "Dismissed";
      break;
    case PermissionAction::IGNORED:
      action_str = "Ignored";
      break;
    case PermissionAction::REVOKED:
      action_str = "Revoked";
      break;
    default:
      NOTREACHED();
      break;
  }

  std::string permission_str = PermissionUtil::GetPermissionString(permission);
  if (permission_str.empty())
    return "";
  return base::StringPrintf("ContentSettings.PermissionActions_%s.%s.Url2",
                            permission_str.c_str(), action_str.c_str());
}

void RecordPermissionRequest(ContentSettingsType content_type,
                             const GURL& requesting_origin,
                             const GURL& embedding_origin,
                             Profile* profile) {
  rappor::RapporServiceImpl* rappor_service =
      g_browser_process->rappor_service();
  if (rappor_service) {
    if (content_type == CONTENT_SETTINGS_TYPE_GEOLOCATION) {
      rappor_service->RecordSampleString(
          "ContentSettings.PermissionRequested.Geolocation.Url2",
          rappor::LOW_FREQUENCY_ETLD_PLUS_ONE_RAPPOR_TYPE,
          rappor::GetDomainAndRegistrySampleFromGURL(requesting_origin));
    } else if (content_type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
      rappor_service->RecordSampleString(
          "ContentSettings.PermissionRequested.Notifications.Url2",
          rappor::LOW_FREQUENCY_ETLD_PLUS_ONE_RAPPOR_TYPE,
          rappor::GetDomainAndRegistrySampleFromGURL(requesting_origin));
    } else if (content_type == CONTENT_SETTINGS_TYPE_MIDI ||
               content_type == CONTENT_SETTINGS_TYPE_MIDI_SYSEX) {
      rappor_service->RecordSampleString(
          "ContentSettings.PermissionRequested.Midi.Url2",
          rappor::LOW_FREQUENCY_ETLD_PLUS_ONE_RAPPOR_TYPE,
          rappor::GetDomainAndRegistrySampleFromGURL(requesting_origin));
    } else if (content_type ==
               CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER) {
      rappor_service->RecordSampleString(
          "ContentSettings.PermissionRequested.ProtectedMedia.Url2",
          rappor::LOW_FREQUENCY_ETLD_PLUS_ONE_RAPPOR_TYPE,
          rappor::GetDomainAndRegistrySampleFromGURL(requesting_origin));
    }
  }

  PermissionType permission;
  bool success = PermissionUtil::GetPermissionType(content_type, &permission);
  DCHECK(success);

  bool secure_origin = content::IsOriginSecure(requesting_origin);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.PermissionRequested",
      static_cast<base::HistogramBase::Sample>(permission),
      static_cast<base::HistogramBase::Sample>(PermissionType::NUM));
  if (secure_origin) {
    UMA_HISTOGRAM_ENUMERATION(
        "ContentSettings.PermissionRequested_SecureOrigin",
        static_cast<base::HistogramBase::Sample>(permission),
        static_cast<base::HistogramBase::Sample>(PermissionType::NUM));
  } else {
    UMA_HISTOGRAM_ENUMERATION(
        "ContentSettings.PermissionRequested_InsecureOrigin",
        static_cast<base::HistogramBase::Sample>(permission),
        static_cast<base::HistogramBase::Sample>(PermissionType::NUM));
  }
}

}  // anonymous namespace

// PermissionReportInfo -------------------------------------------------------
PermissionReportInfo::PermissionReportInfo(
    const GURL& origin,
    ContentSettingsType permission,
    PermissionAction action,
    PermissionSourceUI source_ui,
    PermissionRequestGestureType gesture_type,
    PermissionPersistDecision persist_decision,
    int num_prior_dismissals,
    int num_prior_ignores)
    : origin(origin), permission(permission), action(action),
      source_ui(source_ui), gesture_type(gesture_type),
      persist_decision(persist_decision),
      num_prior_dismissals(num_prior_dismissals),
      num_prior_ignores(num_prior_ignores) {}

PermissionReportInfo::PermissionReportInfo(
    const PermissionReportInfo& other) = default;

// PermissionUmaUtil ----------------------------------------------------------

const char PermissionUmaUtil::kPermissionsPromptShown[] =
    "Permissions.Prompt.Shown";
const char PermissionUmaUtil::kPermissionsPromptShownGesture[] =
    "Permissions.Prompt.Shown.Gesture";
const char PermissionUmaUtil::kPermissionsPromptShownNoGesture[] =
    "Permissions.Prompt.Shown.NoGesture";
const char PermissionUmaUtil::kPermissionsPromptAccepted[] =
    "Permissions.Prompt.Accepted";
const char PermissionUmaUtil::kPermissionsPromptAcceptedGesture[] =
    "Permissions.Prompt.Accepted.Gesture";
const char PermissionUmaUtil::kPermissionsPromptAcceptedNoGesture[] =
    "Permissions.Prompt.Accepted.NoGesture";
const char PermissionUmaUtil::kPermissionsPromptDenied[] =
    "Permissions.Prompt.Denied";
const char PermissionUmaUtil::kPermissionsPromptDeniedGesture[] =
    "Permissions.Prompt.Denied.Gesture";
const char PermissionUmaUtil::kPermissionsPromptDeniedNoGesture[] =
    "Permissions.Prompt.Denied.NoGesture";
const char PermissionUmaUtil::kPermissionsPromptRequestsPerPrompt[] =
    "Permissions.Prompt.RequestsPerPrompt";
const char PermissionUmaUtil::kPermissionsPromptMergedBubbleTypes[] =
    "Permissions.Prompt.MergedBubbleTypes";
const char PermissionUmaUtil::kPermissionsPromptMergedBubbleAccepted[] =
    "Permissions.Prompt.MergedBubbleAccepted";
const char PermissionUmaUtil::kPermissionsPromptMergedBubbleDenied[] =
    "Permissions.Prompt.MergedBubbleDenied";
const char
    PermissionUmaUtil::kPermissionsPromptAcceptedPriorDismissCountPrefix[] =
        "Permissions.Prompt.Accepted.PriorDismissCount.";
const char
    PermissionUmaUtil::kPermissionsPromptAcceptedPriorIgnoreCountPrefix[] =
        "Permissions.Prompt.Accepted.PriorIgnoreCount.";
const char
    PermissionUmaUtil::kPermissionsPromptDeniedPriorDismissCountPrefix[] =
        "Permissions.Prompt.Denied.PriorDismissCount.";
const char
    PermissionUmaUtil::kPermissionsPromptDeniedPriorIgnoreCountPrefix[] =
        "Permissions.Prompt.Denied.PriorIgnoreCount.";
const char
    PermissionUmaUtil::kPermissionsPromptDismissedPriorDismissCountPrefix[] =
        "Permissions.Prompt.Dismissed.PriorDismissCount.";
const char
    PermissionUmaUtil::kPermissionsPromptDismissedPriorIgnoreCountPrefix[] =
        "Permissions.Prompt.Dismissed.PriorIgnoreCount.";
const char
    PermissionUmaUtil::kPermissionsPromptIgnoredPriorDismissCountPrefix[] =
        "Permissions.Prompt.Ignored.PriorDismissCount.";
const char
    PermissionUmaUtil::kPermissionsPromptIgnoredPriorIgnoreCountPrefix[] =
        "Permissions.Prompt.Ignored.PriorIgnoreCount.";

// Make sure you update histograms.xml permission histogram_suffix if you
// add new permission
void PermissionUmaUtil::PermissionRequested(ContentSettingsType content_type,
                                            const GURL& requesting_origin,
                                            const GURL& embedding_origin,
                                            Profile* profile) {
  RecordPermissionRequest(content_type, requesting_origin, embedding_origin,
                          profile);
}

void PermissionUmaUtil::PermissionGranted(
    ContentSettingsType permission,
    PermissionRequestGestureType gesture_type,
    const GURL& requesting_origin,
    Profile* profile) {
  PermissionDecisionAutoBlocker* autoblocker =
      PermissionDecisionAutoBlocker::GetForProfile(profile);
  RecordPermissionAction(permission, PermissionAction::GRANTED,
                         PermissionSourceUI::PROMPT, gesture_type,
                         requesting_origin, profile);
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptAcceptedPriorDismissCountPrefix,
      autoblocker->GetDismissCount(requesting_origin, permission));
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptAcceptedPriorIgnoreCountPrefix,
      autoblocker->GetIgnoreCount(requesting_origin, permission));
}

void PermissionUmaUtil::PermissionDenied(
    ContentSettingsType permission,
    PermissionRequestGestureType gesture_type,
    const GURL& requesting_origin,
    Profile* profile) {
  PermissionDecisionAutoBlocker* autoblocker =
      PermissionDecisionAutoBlocker::GetForProfile(profile);
  RecordPermissionAction(permission, PermissionAction::DENIED,
                         PermissionSourceUI::PROMPT, gesture_type,
                         requesting_origin, profile);
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptDeniedPriorDismissCountPrefix,
      autoblocker->GetDismissCount(requesting_origin, permission));
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptDeniedPriorIgnoreCountPrefix,
      autoblocker->GetIgnoreCount(requesting_origin, permission));
}

void PermissionUmaUtil::PermissionDismissed(
    ContentSettingsType permission,
    PermissionRequestGestureType gesture_type,
    const GURL& requesting_origin,
    Profile* profile) {
  PermissionDecisionAutoBlocker* autoblocker =
      PermissionDecisionAutoBlocker::GetForProfile(profile);
  RecordPermissionAction(permission, PermissionAction::DISMISSED,
                         PermissionSourceUI::PROMPT, gesture_type,
                         requesting_origin, profile);
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptDismissedPriorDismissCountPrefix,
      autoblocker->GetDismissCount(requesting_origin, permission));
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptDismissedPriorIgnoreCountPrefix,
      autoblocker->GetIgnoreCount(requesting_origin, permission));
}

void PermissionUmaUtil::PermissionIgnored(
    ContentSettingsType permission,
    PermissionRequestGestureType gesture_type,
    const GURL& requesting_origin,
    Profile* profile) {
  PermissionDecisionAutoBlocker* autoblocker =
      PermissionDecisionAutoBlocker::GetForProfile(profile);
  RecordPermissionAction(permission, PermissionAction::IGNORED,
                         PermissionSourceUI::PROMPT, gesture_type,
                         requesting_origin, profile);
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptIgnoredPriorDismissCountPrefix,
      autoblocker->GetDismissCount(requesting_origin, permission));
  RecordPermissionPromptPriorCount(
      permission, kPermissionsPromptIgnoredPriorIgnoreCountPrefix,
      autoblocker->GetIgnoreCount(requesting_origin, permission));

  // RecordPermission* methods need to be called before RecordIgnore in the
  // blocker because they record the number of prior ignore and dismiss values,
  // and we don't want to include the current ignore.
  autoblocker->RecordIgnore(requesting_origin, permission);
}

void PermissionUmaUtil::PermissionRevoked(ContentSettingsType permission,
                                          PermissionSourceUI source_ui,
                                          const GURL& revoked_origin,
                                          Profile* profile) {
  // TODO(tsergeant): Expand metrics definitions for revocation to include all
  // permissions.
  if (permission == CONTENT_SETTINGS_TYPE_NOTIFICATIONS ||
      permission == CONTENT_SETTINGS_TYPE_GEOLOCATION ||
      permission == CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC ||
      permission == CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA) {
    // An unknown gesture type is passed in since gesture type is only
    // applicable in prompt UIs where revocations are not possible.
    RecordPermissionAction(permission, PermissionAction::REVOKED, source_ui,
                           PermissionRequestGestureType::UNKNOWN,
                           revoked_origin, profile);
  }
}

void PermissionUmaUtil::RecordEmbargoPromptSuppression(
    PermissionEmbargoStatus embargo_status) {
  UMA_HISTOGRAM_ENUMERATION("Permissions.AutoBlocker.EmbargoPromptSuppression",
                            static_cast<int>(embargo_status),
                            static_cast<int>(PermissionEmbargoStatus::NUM));
}

void PermissionUmaUtil::RecordEmbargoPromptSuppressionFromSource(
    PermissionStatusSource source) {
  // Explicitly switch to ensure that any new PermissionStatusSource values are
  // dealt with appropriately.
  switch (source) {
    case PermissionStatusSource::MULTIPLE_DISMISSALS:
      PermissionUmaUtil::RecordEmbargoPromptSuppression(
          PermissionEmbargoStatus::REPEATED_DISMISSALS);
      break;
    case PermissionStatusSource::SAFE_BROWSING_BLACKLIST:
      PermissionUmaUtil::RecordEmbargoPromptSuppression(
          PermissionEmbargoStatus::PERMISSIONS_BLACKLISTING);
      break;
    case PermissionStatusSource::UNSPECIFIED:
    case PermissionStatusSource::KILL_SWITCH:
      // The permission wasn't under embargo, so don't record anything. We may
      // embargo it later.
      break;
  }
}

void PermissionUmaUtil::RecordEmbargoStatus(
    PermissionEmbargoStatus embargo_status) {
  UMA_HISTOGRAM_ENUMERATION("Permissions.AutoBlocker.EmbargoStatus",
                            static_cast<int>(embargo_status),
                            static_cast<int>(PermissionEmbargoStatus::NUM));
}

void PermissionUmaUtil::RecordSafeBrowsingResponse(
    base::TimeDelta response_time,
    SafeBrowsingResponse response) {
  UMA_HISTOGRAM_TIMES("Permissions.AutoBlocker.SafeBrowsingResponseTime",
                      response_time);
  UMA_HISTOGRAM_ENUMERATION("Permissions.AutoBlocker.SafeBrowsingResponse",
                            static_cast<int>(response),
                            static_cast<int>(SafeBrowsingResponse::NUM));
}

void PermissionUmaUtil::PermissionPromptShown(
    const std::vector<PermissionRequest*>& requests) {
  DCHECK(!requests.empty());

  PermissionRequestType permission_prompt_type =
      PermissionRequestType::MULTIPLE;
  PermissionRequestGestureType permission_gesture_type =
      PermissionRequestGestureType::UNKNOWN;
  if (requests.size() == 1) {
    permission_prompt_type = requests[0]->GetPermissionRequestType();
    permission_gesture_type = requests[0]->GetGestureType();
  }

  RecordPermissionPromptShown(permission_prompt_type, permission_gesture_type);

  UMA_HISTOGRAM_ENUMERATION(
      kPermissionsPromptRequestsPerPrompt,
      static_cast<base::HistogramBase::Sample>(requests.size()),
      static_cast<base::HistogramBase::Sample>(10));

  if (requests.size() > 1) {
    for (const auto* request : requests) {
      PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptMergedBubbleTypes,
                                 request->GetPermissionRequestType());
    }
  }
}

void PermissionUmaUtil::PermissionPromptAccepted(
    const std::vector<PermissionRequest*>& requests,
    const std::vector<bool>& accept_states) {
  DCHECK(!requests.empty());
  DCHECK(requests.size() == accept_states.size());

  bool all_accepted = accept_states[0];
  PermissionRequestType permission_prompt_type =
      requests[0]->GetPermissionRequestType();
  PermissionRequestGestureType permission_gesture_type =
      requests[0]->GetGestureType();
  if (requests.size() > 1) {
    permission_prompt_type = PermissionRequestType::MULTIPLE;
    permission_gesture_type = PermissionRequestGestureType::UNKNOWN;
    for (size_t i = 0; i < requests.size(); ++i) {
      const auto* request = requests[i];
      if (accept_states[i]) {
        PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptMergedBubbleAccepted,
                                   request->GetPermissionRequestType());
      } else {
        all_accepted = false;
        PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptMergedBubbleDenied,
                                   request->GetPermissionRequestType());
      }
    }
  }

  if (all_accepted) {
    RecordPermissionPromptAccepted(permission_prompt_type,
                                   permission_gesture_type);
  } else {
    RecordPermissionPromptDenied(permission_prompt_type,
                                 permission_gesture_type);
  }
}

void PermissionUmaUtil::PermissionPromptDenied(
    const std::vector<PermissionRequest*>& requests) {
  DCHECK(!requests.empty());
  DCHECK(requests.size() == 1);

  RecordPermissionPromptDenied(requests[0]->GetPermissionRequestType(),
                               requests[0]->GetGestureType());
}

void PermissionUmaUtil::RecordPermissionPromptShown(
    PermissionRequestType request_type,
    PermissionRequestGestureType gesture_type) {
  PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptShown, request_type);
  PERMISSION_BUBBLE_GESTURE_TYPE_UMA(
      kPermissionsPromptShownGesture, kPermissionsPromptShownNoGesture,
      gesture_type, request_type);
}

void PermissionUmaUtil::RecordPermissionPromptAccepted(
    PermissionRequestType request_type,
    PermissionRequestGestureType gesture_type) {
  PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptAccepted, request_type);
  PERMISSION_BUBBLE_GESTURE_TYPE_UMA(kPermissionsPromptAcceptedGesture,
                                     kPermissionsPromptAcceptedNoGesture,
                                     gesture_type, request_type);
}

void PermissionUmaUtil::RecordPermissionPromptDenied(
    PermissionRequestType request_type,
    PermissionRequestGestureType gesture_type) {
  PERMISSION_BUBBLE_TYPE_UMA(kPermissionsPromptDenied, request_type);
  PERMISSION_BUBBLE_GESTURE_TYPE_UMA(kPermissionsPromptDeniedGesture,
                                     kPermissionsPromptDeniedNoGesture,
                                     gesture_type, request_type);
}

void PermissionUmaUtil::RecordPermissionPromptPriorCount(
    ContentSettingsType permission,
    const std::string& prefix,
    int count) {
  // The user is not prompted for this permissions, thus there is no prompt
  // event to record a prior count for.
  DCHECK_NE(CONTENT_SETTINGS_TYPE_BACKGROUND_SYNC, permission);

  // Expand UMA_HISTOGRAM_COUNTS_100 so that we can use a dynamically suffixed
  // histogram name.
  base::Histogram::FactoryGet(
      prefix + PermissionUtil::GetPermissionString(permission), 1, 100, 50,
      base::HistogramBase::kUmaTargetedHistogramFlag)
      ->Add(count);
}

void PermissionUmaUtil::PermissionPromptAcceptedWithPersistenceToggle(
    ContentSettingsType permission,
    bool toggle_enabled) {
  switch (permission) {
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Accepted.Persisted.Geolocation",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.Notifications",
          toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MIDI_SYSEX:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Accepted.Persisted.MidiSysEx",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PUSH_MESSAGING:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.PushMessaging",
          toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.ProtectedMedia",
          toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_DURABLE_STORAGE:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.DurableStorage",
          toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.AudioCapture", toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Accepted.Persisted.VideoCapture", toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PLUGINS:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Accepted.Persisted.Flash",
                            toggle_enabled);
      break;
    // The user is not prompted for these permissions, thus there is no accept
    // recorded for them.
    default:
      NOTREACHED() << "PERMISSION "
                   << PermissionUtil::GetPermissionString(permission)
                   << " not accounted for";
  }
}

void PermissionUmaUtil::PermissionPromptDeniedWithPersistenceToggle(
    ContentSettingsType permission,
    bool toggle_enabled) {
  switch (permission) {
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.Geolocation",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.Notifications",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MIDI_SYSEX:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.MidiSysEx",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PUSH_MESSAGING:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.PushMessaging",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Denied.Persisted.ProtectedMedia", toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_DURABLE_STORAGE:
      UMA_HISTOGRAM_BOOLEAN(
          "Permissions.Prompt.Denied.Persisted.DurableStorage", toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.AudioCapture",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.VideoCapture",
                            toggle_enabled);
      break;
    case CONTENT_SETTINGS_TYPE_PLUGINS:
      UMA_HISTOGRAM_BOOLEAN("Permissions.Prompt.Denied.Persisted.Flash",
                            toggle_enabled);
      break;
    // The user is not prompted for these permissions, thus there is no deny
    // recorded for them.
    default:
      NOTREACHED() << "PERMISSION "
                   << PermissionUtil::GetPermissionString(permission)
                   << " not accounted for";
  }
}

bool PermissionUmaUtil::IsOptedIntoPermissionActionReporting(Profile* profile) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisablePermissionActionReporting)) {
    return false;
  }

  DCHECK(profile);
  if (profile->GetProfileType() == Profile::INCOGNITO_PROFILE)
    return false;
  if (!profile->GetPrefs()->GetBoolean(prefs::kSafeBrowsingEnabled))
    return false;

  browser_sync::ProfileSyncService* profile_sync_service =
      ProfileSyncServiceFactory::GetForProfile(profile);

  // Do not report if profile can't get a profile sync service or sync cannot
  // start.
  if (!(profile_sync_service && profile_sync_service->CanSyncStart()))
    return false;

  // Do not report for users with a Custom passphrase set. We need to wait for
  // Sync to be active in order to check the passphrase, so we don't report if
  // Sync is not active yet.
  if (!profile_sync_service->IsSyncActive() ||
      profile_sync_service->IsUsingSecondaryPassphrase()) {
    return false;
  }

  syncer::ModelTypeSet preferred_data_types =
      profile_sync_service->GetPreferredDataTypes();
  if (!(preferred_data_types.Has(syncer::PROXY_TABS) &&
        preferred_data_types.Has(syncer::PRIORITY_PREFERENCES))) {
    return false;
  }

  return true;
}

void PermissionUmaUtil::RecordPermissionAction(
    ContentSettingsType permission,
    PermissionAction action,
    PermissionSourceUI source_ui,
    PermissionRequestGestureType gesture_type,
    const GURL& requesting_origin,
    Profile* profile) {
  if (IsOptedIntoPermissionActionReporting(profile)) {
    PermissionDecisionAutoBlocker* autoblocker =
        PermissionDecisionAutoBlocker::GetForProfile(profile);
    // TODO(kcarattini): Pass in the actual persist decision when it becomes
    // available.
    PermissionReportInfo report_info(
        requesting_origin, permission, action, source_ui, gesture_type,
        PermissionPersistDecision::UNSPECIFIED,
        autoblocker->GetDismissCount(requesting_origin, permission),
        autoblocker->GetIgnoreCount(requesting_origin, permission));
    g_browser_process->safe_browsing_service()
        ->ui_manager()->ReportPermissionAction(report_info);
  }

  bool secure_origin = content::IsOriginSecure(requesting_origin);

  switch (permission) {
    // Geolocation, MidiSysEx, Push, Durable Storage, and Media permissions are
    // disabled on insecure origins, so there's no need to record metrics for
    // secure/insecue.
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.Geolocation",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
      PERMISSION_ACTION_UMA(secure_origin, "Permissions.Action.Notifications",
                            "Permissions.Action.SecureOrigin.Notifications",
                            "Permissions.Action.InsecureOrigin.Notifications",
                            action);
      break;
    case CONTENT_SETTINGS_TYPE_MIDI_SYSEX:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.MidiSysEx",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_PUSH_MESSAGING:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.PushMessaging",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER:
      PERMISSION_ACTION_UMA(secure_origin, "Permissions.Action.ProtectedMedia",
                            "Permissions.Action.SecureOrigin.ProtectedMedia",
                            "Permissions.Action.InsecureOrigin.ProtectedMedia",
                            action);
      break;
    case CONTENT_SETTINGS_TYPE_DURABLE_STORAGE:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.DurableStorage",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.AudioCapture",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA:
      UMA_HISTOGRAM_ENUMERATION("Permissions.Action.VideoCapture",
                                static_cast<int>(action),
                                static_cast<int>(PermissionAction::NUM));
      break;
    case CONTENT_SETTINGS_TYPE_PLUGINS:
      PERMISSION_ACTION_UMA(secure_origin, "Permissions.Action.Flash",
                            "Permissions.Action.SecureOrigin.Flash",
                            "Permissions.Action.InsecureOrigin.Flash", action);
      break;
    // The user is not prompted for these permissions, thus there is no
    // permission action recorded for them.
    default:
      NOTREACHED() << "PERMISSION "
                   << PermissionUtil::GetPermissionString(permission)
                   << " not accounted for";
  }

  const std::string rappor_metric = GetRapporMetric(permission, action);
  rappor::RapporServiceImpl* rappor_service =
      g_browser_process->rappor_service();
  if (!rappor_metric.empty() && rappor_service) {
    rappor_service->RecordSampleString(
        rappor_metric, rappor::LOW_FREQUENCY_ETLD_PLUS_ONE_RAPPOR_TYPE,
        rappor::GetDomainAndRegistrySampleFromGURL(requesting_origin));
  }
}

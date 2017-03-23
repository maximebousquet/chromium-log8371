// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/guest_mode_policy_handler.h"

#include "base/values.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_value_map.h"

namespace policy {

GuestModePolicyHandler::GuestModePolicyHandler()
    : TypeCheckingPolicyHandler(key::kBrowserGuestModeEnabled,
                                base::Value::Type::BOOLEAN) {}

GuestModePolicyHandler::~GuestModePolicyHandler() {}

void GuestModePolicyHandler::ApplyPolicySettings(const PolicyMap& policies,
                                                 PrefValueMap* prefs) {
  const base::Value* guest_mode_value = policies.GetValue(policy_name());
  bool is_guest_mode_enabled;
  if (guest_mode_value &&
      guest_mode_value->GetAsBoolean(&is_guest_mode_enabled)) {
    prefs->SetBoolean(prefs::kBrowserGuestModeEnabled, is_guest_mode_enabled);
    return;
  }
  // Disable guest mode by default if force signin is enabled.
  const base::Value* force_signin_value =
      policies.GetValue(key::kForceBrowserSignin);
  bool is_force_signin_enabled;
  if (force_signin_value &&
      force_signin_value->GetAsBoolean(&is_force_signin_enabled) &&
      is_force_signin_enabled) {
    prefs->SetBoolean(prefs::kBrowserGuestModeEnabled, false);
  }
}

}  // namespace policy

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/feature_utilities.h"

#include "jni/FeatureUtilities_jni.h"

using base::android::JavaParamRef;

namespace {
bool custom_tab_visible = false;
bool is_in_multi_window_mode = false;
} // namespace

namespace chrome {
namespace android {

CustomTabsVisibilityHistogram GetCustomTabsVisibleValue() {
  return custom_tab_visible ? VISIBLE_CUSTOM_TAB :
      VISIBLE_CHROME_TAB;
}

bool GetIsInMultiWindowModeValue() {
  return is_in_multi_window_mode;
}

} // namespace android
} // namespace chrome

static void SetCustomTabVisible(JNIEnv* env,
                                const JavaParamRef<jclass>& clazz,
                                jboolean visible) {
  custom_tab_visible = visible;
}

static void SetIsInMultiWindowMode(JNIEnv* env,
                                   const JavaParamRef<jclass>& clazz,
                                   jboolean j_is_in_multi_window_mode) {
  is_in_multi_window_mode = j_is_in_multi_window_mode;
}

bool RegisterFeatureUtilities(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

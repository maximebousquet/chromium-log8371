// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_UTILS_H_
#define CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_UTILS_H_

#include "base/strings/string16.h"
#include "components/browsing_data/core/browsing_data_utils.h"
#include "components/browsing_data/core/counters/browsing_data_counter.h"

// Whether the browsing data counters experiment is enabled.
bool AreCountersEnabled();

// Whether the site data counter is enabled.
bool IsSiteDataCounterEnabled();

// Constructs the text to be displayed by a counter from the given |result|.
base::string16 GetChromeCounterTextFromResult(
    const browsing_data::BrowsingDataCounter::Result* result);

#endif  // CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_UTILS_H_

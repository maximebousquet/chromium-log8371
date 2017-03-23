// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_FINAL_STATUS_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_FINAL_STATUS_H_

#include "chrome/browser/prerender/prerender_origin.h"

namespace prerender {

// FinalStatus indicates whether |this| was used, or why it was cancelled.
// NOTE: New values need to be appended, since they are used in histograms.
enum FinalStatus {
  FINAL_STATUS_USED = 0,
  FINAL_STATUS_TIMED_OUT = 1,
  // Obsolete: FINAL_STATUS_EVICTED = 2,
  FINAL_STATUS_MANAGER_SHUTDOWN = 3,
  FINAL_STATUS_CLOSED = 4,
  FINAL_STATUS_CREATE_NEW_WINDOW = 5,
  FINAL_STATUS_PROFILE_DESTROYED = 6,
  FINAL_STATUS_APP_TERMINATING = 7,
  FINAL_STATUS_JAVASCRIPT_ALERT = 8,
  FINAL_STATUS_AUTH_NEEDED = 9,
  // Obsolete: FINAL_STATUS_HTTPS = 10,
  FINAL_STATUS_DOWNLOAD = 11,
  FINAL_STATUS_MEMORY_LIMIT_EXCEEDED = 12,
  // Obsolete: FINAL_STATUS_JS_OUT_OF_MEMORY = 13,
  // Obsolete: FINAL_STATUS_RENDERER_UNRESPONSIVE = 14,
  FINAL_STATUS_TOO_MANY_PROCESSES = 15,
  FINAL_STATUS_RATE_LIMIT_EXCEEDED = 16,
  // Obsolete: FINAL_STATUS_PENDING_SKIPPED = 17,
  // Obsolete: FINAL_STATUS_CONTROL_GROUP = 18,
  // Obsolete: FINAL_STATUS_HTML5_MEDIA = 19,
  // Obsolete: FINAL_STATUS_SOURCE_RENDER_VIEW_CLOSED = 20,
  FINAL_STATUS_RENDERER_CRASHED = 21,
  FINAL_STATUS_UNSUPPORTED_SCHEME = 22,
  FINAL_STATUS_INVALID_HTTP_METHOD = 23,
  FINAL_STATUS_WINDOW_PRINT = 24,
  FINAL_STATUS_RECENTLY_VISITED = 25,
  FINAL_STATUS_WINDOW_OPENER = 26,
  // Obsolete: FINAL_STATUS_PAGE_ID_CONFLICT = 27,
  FINAL_STATUS_SAFE_BROWSING = 28,
  // Obsolete: FINAL_STATUS_FRAGMENT_MISMATCH = 29,
  FINAL_STATUS_SSL_CLIENT_CERTIFICATE_REQUESTED = 30,
  FINAL_STATUS_CACHE_OR_HISTORY_CLEARED = 31,
  FINAL_STATUS_CANCELLED = 32,
  FINAL_STATUS_SSL_ERROR = 33,
  FINAL_STATUS_CROSS_SITE_NAVIGATION_PENDING = 34,
  FINAL_STATUS_DEVTOOLS_ATTACHED = 35,
  // Obsolete: FINAL_STATUS_SESSION_STORAGE_NAMESPACE_MISMATCH = 36,
  // Obsolete: FINAL_STATUS_NO_USE_GROUP = 37,
  // Obsolete: FINAL_STATUS_MATCH_COMPLETE_DUMMY = 38,
  FINAL_STATUS_DUPLICATE = 39,
  FINAL_STATUS_OPEN_URL = 40,
  // Obsolete: FINAL_STATUS_WOULD_HAVE_BEEN_USED = 41,
  FINAL_STATUS_REGISTER_PROTOCOL_HANDLER = 42,
  FINAL_STATUS_CREATING_AUDIO_STREAM = 43,
  FINAL_STATUS_PAGE_BEING_CAPTURED = 44,
  FINAL_STATUS_BAD_DEFERRED_REDIRECT = 45,
  FINAL_STATUS_NAVIGATION_UNCOMMITTED = 46,
  FINAL_STATUS_NEW_NAVIGATION_ENTRY = 47,
  // Obsolete: FINAL_STATUS_COOKIE_STORE_NOT_LOADED = 48,
  // Obsolete: FINAL_STATUS_COOKIE_CONFLICT = 49,
  FINAL_STATUS_NON_EMPTY_BROWSING_INSTANCE = 50,
  FINAL_STATUS_NAVIGATION_INTERCEPTED = 51,
  FINAL_STATUS_PRERENDERING_DISABLED = 52,
  FINAL_STATUS_CELLULAR_NETWORK = 53,
  FINAL_STATUS_BLOCK_THIRD_PARTY_COOKIES = 54,
  FINAL_STATUS_CREDENTIAL_MANAGER_API = 55,
  FINAL_STATUS_NOSTATE_PREFETCH_FINISHED = 56,
  FINAL_STATUS_LOW_END_DEVICE = 57,
  FINAL_STATUS_MAX,
};

// Return a human-readable name for |final_status|. |final_status|
// is expected to be a valid value.
const char* NameFromFinalStatus(FinalStatus final_status);

}  // namespace prerender

#endif  // CHROME_BROWSER_PRERENDER_PRERENDER_FINAL_STATUS_H_

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_BLUETOOTH_INTERNALS_BLUETOOTH_INTERNALS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_BLUETOOTH_INTERNALS_BLUETOOTH_INTERNALS_UI_H_

#include "base/macros.h"
#include "content/public/browser/web_ui_controller.h"

// The WebUI for chrome://bluetooth-internals
class BluetoothInternalsUI : public content::WebUIController {
 public:
  explicit BluetoothInternalsUI(content::WebUI* web_ui);
  ~BluetoothInternalsUI() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothInternalsUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_BLUETOOTH_INTERNALS_BLUETOOTH_INTERNALS_UI_H_

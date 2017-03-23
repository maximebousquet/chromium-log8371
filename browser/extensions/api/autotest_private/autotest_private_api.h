// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_AUTOTEST_PRIVATE_AUTOTEST_PRIVATE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_AUTOTEST_PRIVATE_AUTOTEST_PRIVATE_API_H_

#include <string>

#include "base/compiler_specific.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "ui/message_center/notification_types.h"

namespace extensions {

class AutotestPrivateLogoutFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.logout", AUTOTESTPRIVATE_LOGOUT)

 private:
  ~AutotestPrivateLogoutFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateRestartFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.restart", AUTOTESTPRIVATE_RESTART)

 private:
  ~AutotestPrivateRestartFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateShutdownFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.shutdown",
                             AUTOTESTPRIVATE_SHUTDOWN)

 private:
  ~AutotestPrivateShutdownFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateLoginStatusFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.loginStatus",
                             AUTOTESTPRIVATE_LOGINSTATUS)

 private:
  ~AutotestPrivateLoginStatusFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateLockScreenFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.lockScreen",
                             AUTOTESTPRIVATE_LOCKSCREEN)

 private:
  ~AutotestPrivateLockScreenFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateGetExtensionsInfoFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.getExtensionsInfo",
                             AUTOTESTPRIVATE_GETEXTENSIONSINFO)

 private:
  ~AutotestPrivateGetExtensionsInfoFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSimulateAsanMemoryBugFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.simulateAsanMemoryBug",
                             AUTOTESTPRIVATE_SIMULATEASANMEMORYBUG)

 private:
  ~AutotestPrivateSimulateAsanMemoryBugFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetTouchpadSensitivityFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setTouchpadSensitivity",
                             AUTOTESTPRIVATE_SETTOUCHPADSENSITIVITY)

 private:
  ~AutotestPrivateSetTouchpadSensitivityFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetTapToClickFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setTapToClick",
                             AUTOTESTPRIVATE_SETTAPTOCLICK)

 private:
  ~AutotestPrivateSetTapToClickFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetThreeFingerClickFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setThreeFingerClick",
                             AUTOTESTPRIVATE_SETTHREEFINGERCLICK)

 private:
  ~AutotestPrivateSetThreeFingerClickFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetTapDraggingFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setTapDragging",
                             AUTOTESTPRIVATE_SETTAPDRAGGING)

 private:
  ~AutotestPrivateSetTapDraggingFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetNaturalScrollFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setNaturalScroll",
                             AUTOTESTPRIVATE_SETNATURALSCROLL)

 private:
  ~AutotestPrivateSetNaturalScrollFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetMouseSensitivityFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setMouseSensitivity",
                             AUTOTESTPRIVATE_SETMOUSESENSITIVITY)

 private:
  ~AutotestPrivateSetMouseSensitivityFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateSetPrimaryButtonRightFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.setPrimaryButtonRight",
                             AUTOTESTPRIVATE_SETPRIMARYBUTTONRIGHT)

 private:
  ~AutotestPrivateSetPrimaryButtonRightFunction() override {}
  ResponseAction Run() override;
};

class AutotestPrivateGetVisibleNotificationsFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("autotestPrivate.getVisibleNotifications",
                             AUTOTESTPRIVATE_GETVISIBLENOTIFICATIONS)

 private:
  static std::string ConvertToString(message_center::NotificationType type);

  ~AutotestPrivateGetVisibleNotificationsFunction() override {}
  ResponseAction Run() override;
};

// Don't kill the browser when we're in a browser test.
void SetAutotestPrivateTest();

// The profile-keyed service that manages the autotestPrivate extension API.
class AutotestPrivateAPI : public BrowserContextKeyedAPI {
 public:
  static BrowserContextKeyedAPIFactory<AutotestPrivateAPI>*
      GetFactoryInstance();

  // TODO(achuith): Replace these with a mock object for system calls.
  bool test_mode() const { return test_mode_; }
  void set_test_mode(bool test_mode) { test_mode_ = test_mode; }

 private:
  friend class BrowserContextKeyedAPIFactory<AutotestPrivateAPI>;

  AutotestPrivateAPI();
  ~AutotestPrivateAPI() override;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() { return "AutotestPrivateAPI"; }
  static const bool kServiceIsNULLWhileTesting = true;
  static const bool kServiceRedirectedInIncognito = true;

  bool test_mode_;  // true for ExtensionApiTest.AutotestPrivate browser test.
};

template <>
KeyedService*
    BrowserContextKeyedAPIFactory<AutotestPrivateAPI>::BuildServiceInstanceFor(
        content::BrowserContext* context) const;

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_AUTOTEST_PRIVATE_AUTOTEST_PRIVATE_API_H_

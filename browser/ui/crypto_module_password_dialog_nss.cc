// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/crypto_module_password_dialog_nss.h"

#include <pk11pub.h>
#include <stddef.h>

#include "base/bind.h"
#include "base/logging.h"
#include "content/public/browser/browser_thread.h"
#include "net/cert/x509_certificate.h"

using content::BrowserThread;

namespace {

bool ShouldShowDialog(PK11SlotInfo* slot) {
  // The wincx arg is unused since we don't call PK11_SetIsLoggedInFunc.
  return (PK11_NeedLogin(slot) && !PK11_IsLoggedIn(slot, NULL /* wincx */));
}

// Basically an asynchronous implementation of NSS's PK11_DoPassword.
// Note: This currently handles only the simple case.  See the TODOs in
// GotPassword for what is yet unimplemented.
class SlotUnlocker {
 public:
  SlotUnlocker(std::vector<crypto::ScopedPK11Slot> modules,
               chrome::CryptoModulePasswordReason reason,
               const net::HostPortPair& server,
               gfx::NativeWindow parent,
               const base::Closure& callback);

  void Start();

 private:
  void GotPassword(const std::string& password);
  void Done();

  size_t current_;
  std::vector<crypto::ScopedPK11Slot> modules_;
  chrome::CryptoModulePasswordReason reason_;
  net::HostPortPair server_;
  gfx::NativeWindow parent_;
  base::Closure callback_;
  PRBool retry_;
};

SlotUnlocker::SlotUnlocker(std::vector<crypto::ScopedPK11Slot> modules,
                           chrome::CryptoModulePasswordReason reason,
                           const net::HostPortPair& server,
                           gfx::NativeWindow parent,
                           const base::Closure& callback)
    : current_(0),
      modules_(std::move(modules)),
      reason_(reason),
      server_(server),
      parent_(parent),
      callback_(callback),
      retry_(PR_FALSE) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void SlotUnlocker::Start() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (; current_ < modules_.size(); ++current_) {
    if (ShouldShowDialog(modules_[current_].get())) {
      ShowCryptoModulePasswordDialog(
          PK11_GetTokenName(modules_[current_].get()), retry_, reason_,
          server_.host(), parent_,
          base::Bind(&SlotUnlocker::GotPassword, base::Unretained(this)));
      return;
    }
  }
  Done();
}

void SlotUnlocker::GotPassword(const std::string& password) {
  // TODO(mattm): PK11_DoPassword has something about PK11_Global.verifyPass.
  // Do we need it?
  // http://mxr.mozilla.org/mozilla/source/security/nss/lib/pk11wrap/pk11auth.c#577

  if (password.empty()) {
    // User cancelled entering password.  Oh well.
    ++current_;
    Start();
    return;
  }

  // TODO(mattm): handle protectedAuthPath
  SECStatus rv =
      PK11_CheckUserPassword(modules_[current_].get(), password.c_str());
  if (rv == SECWouldBlock) {
    // Incorrect password.  Try again.
    retry_ = PR_TRUE;
    Start();
    return;
  }

  // TODO(mattm): PK11_DoPassword calls nssTrustDomain_UpdateCachedTokenCerts on
  // non-friendly slots.  How important is that?

  // Correct password (SECSuccess) or too many attempts/other failure
  // (SECFailure).  Either way we're done with this slot.
  ++current_;
  Start();
}

void SlotUnlocker::Done() {
  DCHECK_EQ(current_, modules_.size());
  callback_.Run();
  delete this;
}

}  // namespace

namespace chrome {

void UnlockSlotsIfNecessary(std::vector<crypto::ScopedPK11Slot> modules,
                            chrome::CryptoModulePasswordReason reason,
                            const net::HostPortPair& server,
                            gfx::NativeWindow parent,
                            const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (size_t i = 0; i < modules.size(); ++i) {
    if (ShouldShowDialog(modules[i].get())) {
      (new SlotUnlocker(std::move(modules), reason, server, parent, callback))
          ->Start();
      return;
    }
  }
  callback.Run();
}

void UnlockCertSlotIfNecessary(net::X509Certificate* cert,
                               chrome::CryptoModulePasswordReason reason,
                               const net::HostPortPair& server,
                               gfx::NativeWindow parent,
                               const base::Closure& callback) {
  std::vector<crypto::ScopedPK11Slot> modules;
  modules.push_back(
      crypto::ScopedPK11Slot(PK11_ReferenceSlot(cert->os_cert_handle()->slot)));
  UnlockSlotsIfNecessary(std::move(modules), reason, server, parent, callback);
}

}  // namespace chrome

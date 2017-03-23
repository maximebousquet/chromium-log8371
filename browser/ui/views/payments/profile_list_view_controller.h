// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_PROFILE_LIST_VIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_PROFILE_LIST_VIEW_CONTROLLER_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "chrome/browser/ui/views/payments/payment_request_item_list.h"
#include "chrome/browser/ui/views/payments/payment_request_sheet_controller.h"

namespace autofill {
class AutofillProfile;
}

namespace views {
class Button;
class View;
}

namespace payments {

class PaymentRequestSpec;
class PaymentRequestState;
class PaymentRequestDialogView;

// This base class encapsulates common view logic for contexts which display
// a list of profiles and allow exactly one of them to be selected.
class ProfileListViewController : public PaymentRequestSheetController {
 public:
  ~ProfileListViewController() override;

  // Creates a controller which lists and allows selection of profiles
  // for shipping address.
  static std::unique_ptr<ProfileListViewController>
  GetShippingProfileViewController(PaymentRequestSpec* spec,
                                   PaymentRequestState* state,
                                   PaymentRequestDialogView* dialog);

  // Creates a controller which lists and allows selection of profiles
  // for contact info.
  static std::unique_ptr<ProfileListViewController>
  GetContactProfileViewController(PaymentRequestSpec* spec,
                                  PaymentRequestState* state,
                                  PaymentRequestDialogView* dialog);

  // PaymentRequestSheetController:
  std::unique_ptr<views::View> CreateView() override;

  // Returns a representation of the given profile appropriate for display
  // in this context.
  virtual std::unique_ptr<views::View> GetLabel(
      autofill::AutofillProfile* profile) = 0;

 protected:
  // Does not take ownership of the arguments, which should outlive this object.
  ProfileListViewController(PaymentRequestSpec* spec,
                            PaymentRequestState* state,
                            PaymentRequestDialogView* dialog);

  // Returns the profiles cached by |request| which are appropriate for display
  // in this context.
  virtual std::vector<autofill::AutofillProfile*> GetProfiles() = 0;

  // Returns the string displayed at the top of the view.
  virtual base::string16 GetHeaderString() = 0;

 private:
  std::unique_ptr<views::Button> CreateRow(autofill::AutofillProfile* profile);
  PaymentRequestItemList list_;

  DISALLOW_COPY_AND_ASSIGN(ProfileListViewController);
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_PROFILE_LIST_VIEW_CONTROLLER_H_
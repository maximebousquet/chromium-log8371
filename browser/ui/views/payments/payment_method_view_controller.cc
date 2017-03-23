// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_method_view_controller.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/memory/ptr_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "chrome/browser/ui/views/payments/payment_request_row_view.h"
#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "chrome/grit/generated_resources.h"
#include "components/payments/content/payment_request_state.h"
#include "components/payments/core/payment_instrument.h"
#include "components/strings/grit/components_strings.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/vector_icons.h"

namespace payments {

namespace {

constexpr int kFirstTagValue = static_cast<int>(
    payments::PaymentRequestCommonTags::PAYMENT_REQUEST_COMMON_TAG_MAX);

enum class PaymentMethodViewControllerTags : int {
  // The tag for the button that triggers the "add card" flow. Starts at
  // |kFirstTagValue| not to conflict with tags common to all views.
  ADD_CREDIT_CARD_BUTTON = kFirstTagValue,
};

class PaymentMethodListItem : public payments::PaymentRequestItemList::Item {
 public:
  // Does not take ownership of |instrument|, which  should not be null and
  // should outlive this object. |list| is the PaymentRequestItemList object
  // that will own this.
  PaymentMethodListItem(PaymentInstrument* instrument,
                        PaymentRequestSpec* spec,
                        PaymentRequestState* state,
                        PaymentRequestItemList* list,
                        bool selected)
      : payments::PaymentRequestItemList::Item(spec, state, list, selected),
        instrument_(instrument) {}
  ~PaymentMethodListItem() override {}

 private:
  // payments::PaymentRequestItemList::Item:
  std::unique_ptr<views::View> CreateExtraView() override {
    std::unique_ptr<views::ImageView> card_icon_view = CreateInstrumentIconView(
        instrument_->icon_resource_id(), instrument_->label());
    card_icon_view->SetImageSize(gfx::Size(32, 20));
    return std::move(card_icon_view);
  }

  std::unique_ptr<views::View> CreateContentView() override {
    std::unique_ptr<views::View> card_info_container =
        base::MakeUnique<views::View>();
    card_info_container->set_can_process_events_within_subtree(false);

    std::unique_ptr<views::BoxLayout> box_layout =
        base::MakeUnique<views::BoxLayout>(views::BoxLayout::kVertical, 0,
                                           kPaymentRequestRowVerticalInsets, 0);
    box_layout->set_cross_axis_alignment(
        views::BoxLayout::CROSS_AXIS_ALIGNMENT_START);
    card_info_container->SetLayoutManager(box_layout.release());

    card_info_container->AddChildView(new views::Label(instrument_->label()));
    card_info_container->AddChildView(
        new views::Label(instrument_->sublabel()));
    // TODO(anthonyvd): Add the "card is incomplete" label once the
    // completedness logic is implemented.
    return card_info_container;
  }

  void SelectedStateChanged() override {
    state()->SetSelectedInstrument(instrument_);
  }

  bool CanBeSelected() const override {
    // TODO(anthonyvd): Check for card completedness.
    return true;
  }

  void PerformSelectionFallback() override {
    // TODO(anthonyvd): Open the editor pre-populated with this card's data.
  }

  PaymentInstrument* instrument_;
  std::unique_ptr<views::ImageView> checkmark_;

  DISALLOW_COPY_AND_ASSIGN(PaymentMethodListItem);
};

}  // namespace

PaymentMethodViewController::PaymentMethodViewController(
    PaymentRequestSpec* spec,
    PaymentRequestState* state,
    PaymentRequestDialogView* dialog)
    : PaymentRequestSheetController(spec, state, dialog) {
  const std::vector<std::unique_ptr<PaymentInstrument>>& available_instruments =
      state->available_instruments();

  for (const std::unique_ptr<PaymentInstrument>& instrument :
       available_instruments) {
    std::unique_ptr<PaymentMethodListItem> item =
        base::MakeUnique<PaymentMethodListItem>(
            instrument.get(), spec, state, &payment_method_list_,
            instrument.get() == state->selected_instrument());
    payment_method_list_.AddItem(std::move(item));
  }
}

PaymentMethodViewController::~PaymentMethodViewController() {}

std::unique_ptr<views::View> PaymentMethodViewController::CreateView() {
  std::unique_ptr<views::View> list_view =
      payment_method_list_.CreateListView();
  list_view->set_id(
      static_cast<int>(DialogViewID::PAYMENT_METHOD_SHEET_LIST_VIEW));
  return CreatePaymentView(
      CreateSheetHeaderView(
          true,
          l10n_util::GetStringUTF16(
              IDS_PAYMENT_REQUEST_PAYMENT_METHOD_SECTION_NAME),
          this),
      std::move(list_view));
}

std::unique_ptr<views::View>
PaymentMethodViewController::CreateExtraFooterView() {
  std::unique_ptr<views::View> extra_view = base::MakeUnique<views::View>();

  extra_view->SetLayoutManager(new views::BoxLayout(
      views::BoxLayout::kHorizontal, 0, 0, kPaymentRequestButtonSpacing));

  views::LabelButton* button = views::MdTextButton::CreateSecondaryUiButton(
      this, l10n_util::GetStringUTF16(IDS_AUTOFILL_ADD_CREDITCARD_CAPTION));
  button->set_tag(static_cast<int>(
      PaymentMethodViewControllerTags::ADD_CREDIT_CARD_BUTTON));
  button->set_id(
      static_cast<int>(DialogViewID::PAYMENT_METHOD_ADD_CARD_BUTTON));
  extra_view->AddChildView(button);

  return extra_view;
}

void PaymentMethodViewController::ButtonPressed(views::Button* sender,
                                                const ui::Event& event) {
  switch (sender->tag()) {
    case static_cast<int>(
        PaymentMethodViewControllerTags::ADD_CREDIT_CARD_BUTTON):
      dialog()->ShowCreditCardEditor();
      break;
    default:
      PaymentRequestSheetController::ButtonPressed(sender, event);
      break;
  }
}

}  // namespace payments

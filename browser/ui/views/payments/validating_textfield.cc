// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/validating_textfield.h"

#include <utility>

namespace payments {

ValidatingTextfield::ValidatingTextfield(
    std::unique_ptr<ValidationDelegate> delegate)
    : Textfield(), delegate_(std::move(delegate)), was_blurred_(false) {}

ValidatingTextfield::~ValidatingTextfield() {}

void ValidatingTextfield::OnBlur() {
  Textfield::OnBlur();

  // The first validation should be on a blur. The subsequent validations will
  // occur when the content changes.
  if (!was_blurred_) {
    was_blurred_ = true;
    Validate();
  }
}

void ValidatingTextfield::OnContentsChanged() {
  // Validation on every keystroke only happens if the field has been validated
  // before as part of a blur.
  if (!was_blurred_)
    return;

  Validate();
}

void ValidatingTextfield::Validate() {
  // ValidateTextfield may have side-effects, such as displaying errors.
  SetInvalid(!delegate_->ValidateTextfield(this));
}

}  // namespace payments

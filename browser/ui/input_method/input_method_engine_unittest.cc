// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/input_method/input_method_engine.h"
#include "chrome/browser/ui/input_method/input_method_engine_base.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ime/ime_bridge.h"
#include "ui/base/ime/ime_engine_handler_interface.h"
#include "ui/base/ime/mock_ime_input_context_handler.h"
#include "ui/base/ime/text_input_flags.h"

namespace input_method {
namespace {
const char kTestExtensionId[] = "test_extension";

enum CallsBitmap {
  NONE = 0U,
  ONACTIVATE = 1U,
  ONDEACTIVATED = 2U,
  ONFOCUS = 4U,
  ONBLUR = 8U,
  ONKEYEVENT = 16U,
  ONRESET = 32U,
  ONCOMPOSITIONBOUNDSCHANGED = 64U,
  ONSURROUNDINGTEXTCHANGED = 128U
};

// The surrounding information.
struct SurroundingInfo {
  std::string text;
  int focus;
  int anchor;
  int offset;
};

class KeyEventDoneCallback {
 public:
  explicit KeyEventDoneCallback(bool expected_argument)
      : expected_argument_(expected_argument), is_called_(false) {}
  ~KeyEventDoneCallback() {}

  void Run(bool consumed) {
    if (consumed == expected_argument_) {
      base::MessageLoop::current()->QuitWhenIdle();
      is_called_ = true;
    }
  }

  void WaitUntilCalled() {
    while (!is_called_)
      content::RunMessageLoop();
  }

 private:
  bool expected_argument_;
  bool is_called_;

  DISALLOW_COPY_AND_ASSIGN(KeyEventDoneCallback);
};

class TestObserver : public InputMethodEngineBase::Observer {
 public:
  TestObserver() : calls_bitmap_(NONE) {}
  ~TestObserver() override {}

  // InputMethodEngineBase::Observer:
  void OnActivate(const std::string& engine_id) override {
    calls_bitmap_ |= ONACTIVATE;
  }
  void OnFocus(
      const ui::IMEEngineHandlerInterface::InputContext& context) override {
    calls_bitmap_ |= ONFOCUS;
  }
  void OnBlur(int context_id) override { calls_bitmap_ |= ONBLUR; }
  void OnKeyEvent(
      const std::string& engine_id,
      const InputMethodEngineBase::KeyboardEvent& event,
      ui::IMEEngineHandlerInterface::KeyEventDoneCallback& key_data) override {
    calls_bitmap_ |= ONKEYEVENT;
    key_event_ = event;
  }
  void OnReset(const std::string& engine_id) override {
    calls_bitmap_ |= ONRESET;
  }
  void OnDeactivated(const std::string& engine_id) override {
    calls_bitmap_ |= ONDEACTIVATED;
  }
  void OnCompositionBoundsChanged(
      const std::vector<gfx::Rect>& bounds) override {
    calls_bitmap_ |= ONCOMPOSITIONBOUNDSCHANGED;
    composition_bounds_ = bounds;
  }
  bool IsInterestedInKeyEvent() const override { return true; }
  void OnSurroundingTextChanged(const std::string& engine_id,
                                const std::string& text,
                                int cursor_pos,
                                int anchor_pos,
                                int offset) override {
    calls_bitmap_ |= ONSURROUNDINGTEXTCHANGED;
    surrounding_info_.text = text;
    surrounding_info_.focus = cursor_pos;
    surrounding_info_.anchor = anchor_pos;
    surrounding_info_.offset = offset;
  }
  void OnRequestEngineSwitch() override {}

  // Returns and resets the bitmap |calls_bitmap_|.
  unsigned char GetCallsBitmapAndReset() {
    unsigned char ret = calls_bitmap_;
    calls_bitmap_ = NONE;
    return ret;
  }

  const InputMethodEngineBase::KeyboardEvent& GetKeyEvent() {
    return key_event_;
  }

  const std::vector<gfx::Rect>& GetCompositionBounds() {
    return composition_bounds_;
  }

  const SurroundingInfo& GetSurroundingInfo() { return surrounding_info_; }

 private:
  unsigned char calls_bitmap_;
  InputMethodEngineBase::KeyboardEvent key_event_;
  std::vector<gfx::Rect> composition_bounds_;
  SurroundingInfo surrounding_info_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

class InputMethodEngineTest : public testing::Test {
 public:
  InputMethodEngineTest() : observer_(nullptr) {
    ui::IMEBridge::Initialize();
    mock_ime_input_context_handler_.reset(new ui::MockIMEInputContextHandler());
    ui::IMEBridge::Get()->SetInputContextHandler(
        mock_ime_input_context_handler_.get());
    CreateEngine(kTestExtensionId);
  }
  ~InputMethodEngineTest() override {
    ui::IMEBridge::Get()->SetInputContextHandler(nullptr);
    engine_.reset();
  }

 protected:
  void CreateEngine(const char* extension_id) {
    engine_.reset(new InputMethodEngine());
    observer_ = new TestObserver();
    std::unique_ptr<InputMethodEngineBase::Observer> observer_ptr(observer_);
    engine_->Initialize(std::move(observer_ptr), extension_id, profile_.get());
  }

  void FocusIn(ui::TextInputType input_type) {
    ui::IMEEngineHandlerInterface::InputContext input_context(
        input_type, ui::TEXT_INPUT_MODE_DEFAULT, ui::TEXT_INPUT_FLAG_NONE);
    engine_->FocusIn(input_context);
    ui::IMEBridge::Get()->SetCurrentInputContext(input_context);
  }

  std::unique_ptr<InputMethodEngine> engine_;

  TestObserver* observer_;
  std::unique_ptr<ui::MockIMEInputContextHandler>
      mock_ime_input_context_handler_;
  // The associated testing profile.
  std::unique_ptr<TestingProfile> profile_;

 private:
  DISALLOW_COPY_AND_ASSIGN(InputMethodEngineTest);
};

}  // namespace

// Tests input.ime.onActivate/onDeactivate/onFocus/onBlur API.
TEST_F(InputMethodEngineTest, TestActivateAndFocus) {
  // Enables the extension with focus.
  engine_->Enable("");
  FocusIn(ui::TEXT_INPUT_TYPE_URL);
  EXPECT_EQ(ONACTIVATE | ONFOCUS, observer_->GetCallsBitmapAndReset());
  // Focus out.
  engine_->FocusOut();
  EXPECT_EQ(ONBLUR, observer_->GetCallsBitmapAndReset());
  // Disables the extension.
  engine_->Disable();
  EXPECT_EQ(ONDEACTIVATED, observer_->GetCallsBitmapAndReset());
}

// Tests input.ime.onKeyEvent API.
TEST_F(InputMethodEngineTest, TestKeyEvent) {
  // Enables the extension with focus.
  engine_->Enable("");
  FocusIn(ui::TEXT_INPUT_TYPE_URL);
  EXPECT_EQ(ONACTIVATE | ONFOCUS, observer_->GetCallsBitmapAndReset());
  // Sends key events.
  ui::KeyEvent key_event(ui::ET_KEY_PRESSED, ui::VKEY_A, ui::EF_NONE);
  KeyEventDoneCallback callback(false);
  ui::IMEEngineHandlerInterface::KeyEventDoneCallback keyevent_callback =
      base::Bind(&KeyEventDoneCallback::Run, base::Unretained(&callback));
  engine_->ProcessKeyEvent(key_event, keyevent_callback);
  EXPECT_EQ(ONKEYEVENT, observer_->GetCallsBitmapAndReset());
  EXPECT_EQ("keydown", observer_->GetKeyEvent().type);
  EXPECT_EQ("a", observer_->GetKeyEvent().key);
  EXPECT_EQ("KeyA", observer_->GetKeyEvent().code);
  EXPECT_EQ('A', observer_->GetKeyEvent().key_code);
  EXPECT_FALSE(observer_->GetKeyEvent().alt_key);
  EXPECT_FALSE(observer_->GetKeyEvent().ctrl_key);
  EXPECT_FALSE(observer_->GetKeyEvent().shift_key);
  EXPECT_FALSE(observer_->GetKeyEvent().caps_lock);
}

// Tests input.ime.onReset API.
TEST_F(InputMethodEngineTest, TestReset) {
  // Enables the extension with focus.
  engine_->Enable("");
  FocusIn(ui::TEXT_INPUT_TYPE_URL);
  EXPECT_EQ(ONACTIVATE | ONFOCUS, observer_->GetCallsBitmapAndReset());

  // Resets the engine.
  engine_->Reset();
  EXPECT_EQ(ONRESET, observer_->GetCallsBitmapAndReset());
}

// Tests input.ime.onCompositionBoundsChanged API.
TEST_F(InputMethodEngineTest, TestCompositionBoundsChanged) {
  // Enables the extension with focus.
  engine_->Enable("");
  FocusIn(ui::TEXT_INPUT_TYPE_URL);
  EXPECT_EQ(ONACTIVATE | ONFOCUS, observer_->GetCallsBitmapAndReset());
  // Sets the composition to trigger the composition bounds changed event.
  std::vector<gfx::Rect> rects;
  rects.push_back(gfx::Rect());
  engine_->SetCompositionBounds(rects);
  EXPECT_EQ(ONCOMPOSITIONBOUNDSCHANGED, observer_->GetCallsBitmapAndReset());
  EXPECT_EQ(1U, observer_->GetCompositionBounds().size());
}

// Tests input.ime.onSurroundingTextChanged API.
TEST_F(InputMethodEngineTest, TestSurroundingTextChanged) {
  // Enables the extension with focus.
  engine_->Enable("");
  FocusIn(ui::TEXT_INPUT_TYPE_URL);
  EXPECT_EQ(ONACTIVATE | ONFOCUS, observer_->GetCallsBitmapAndReset());
  // Sets the surrounding text.
  engine_->SetSurroundingText("text" /* Surrounding text*/,
                              0 /*focused position*/, 1 /*anchor position*/,
                              0 /*offset position*/);
  EXPECT_EQ(ONSURROUNDINGTEXTCHANGED, observer_->GetCallsBitmapAndReset());
  EXPECT_EQ("text", observer_->GetSurroundingInfo().text);
  EXPECT_EQ(0, observer_->GetSurroundingInfo().focus);
  EXPECT_EQ(1, observer_->GetSurroundingInfo().anchor);
  EXPECT_EQ(0, observer_->GetSurroundingInfo().offset);
}

}  // namespace input_method

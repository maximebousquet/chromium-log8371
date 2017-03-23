// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/icon_label_bubble_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/test/views_test_base.h"

#if defined(USE_ASH)
#include "ui/aura/window.h"
#endif

namespace {

const int kStayOpenTimeMS = 100;
const int kOpenTimeMS = 100;
const int kAnimationDurationMS = (kOpenTimeMS * 2) + kStayOpenTimeMS;
const int kImageSize = 15;
const SkColor kTestColor = SkColorSetRGB(64, 64, 64);
const int kNumberOfSteps = 300;

class TestIconLabelBubbleView : public IconLabelBubbleView {
 public:
  enum State {
    GROWING,
    STEADY,
    SHRINKING,
  };

  explicit TestIconLabelBubbleView(const gfx::FontList& font_list)
      : IconLabelBubbleView(font_list, false), value_(0) {
    GetImageView()->SetImageSize(gfx::Size(kImageSize, kImageSize));
    SetLabel(base::ASCIIToUTF16("Label"));
  }

  void SetCurrentAnimationValue(int value) {
    value_ = value;
    SizeToPreferredSize();
  }

  int width() const { return bounds().width(); }
  bool IsLabelVisible() const { return label()->visible(); }
  void SetLabelVisible(bool visible) { label()->SetVisible(visible); }
  const gfx::Rect& GetLabelBounds() const { return label()->bounds(); }

  State state() const {
    const double kOpenFraction =
        static_cast<double>(kOpenTimeMS) / kAnimationDurationMS;
    double state = value_ / (double)kNumberOfSteps;
    if (state < kOpenFraction)
      return GROWING;
    if (state > (1.0 - kOpenFraction))
      return SHRINKING;
    return STEADY;
  }

 protected:
  // IconLabelBubbleView:
  SkColor GetTextColor() const override { return kTestColor; }

  bool ShouldShowLabel() const override {
    return !IsShrinking() ||
           (width() > (image()->GetPreferredSize().width() +
                       2 * LocationBarView::kIconInteriorPadding +
                       2 * GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING)));
  }

  double WidthMultiplier() const override {
    const double kOpenFraction =
        static_cast<double>(kOpenTimeMS) / kAnimationDurationMS;
    double fraction = value_ / (double)kNumberOfSteps;
    switch (state()) {
      case GROWING:
        return fraction / kOpenFraction;
      case STEADY:
        return 1.0;
      case SHRINKING:
        return (1.0 - fraction) / kOpenFraction;
    }
    NOTREACHED();
    return 1.0;
  }

  bool IsShrinking() const override { return state() == SHRINKING; }

 private:
  int value_;
  DISALLOW_COPY_AND_ASSIGN(TestIconLabelBubbleView);
};

}  // namespace

class IconLabelBubbleViewTest : public views::ViewsTestBase {
 public:
  IconLabelBubbleViewTest()
      : views::ViewsTestBase(),
        steady_reached_(false),
        shrinking_reached_(false),
        minimum_size_reached_(false),
        previous_width_(0),
        initial_image_x_(0) {}
  ~IconLabelBubbleViewTest() override {}

 protected:
  // views::ViewsTestBase:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    gfx::FontList font_list;
    view_.reset(new TestIconLabelBubbleView(font_list));
  }

  void VerifyWithAnimationStep(int step) {
    Reset();
    for (int value = 0; value < kNumberOfSteps; value += step) {
      SetValue(value);
      VerifyAnimationStep();
    }
    view_->SetLabelVisible(false);
  }

 private:
  void Reset() {
    view_->SetLabelVisible(true);
    SetValue(0);
    steady_reached_ = false;
    shrinking_reached_ = false;
    minimum_size_reached_ = false;
    previous_width_ = 0;
    initial_image_x_ = GetImageBounds().x();
    EXPECT_EQ(0, initial_image_x_);
  }

  void VerifyAnimationStep() {
    switch (state()) {
      case TestIconLabelBubbleView::State::GROWING: {
        EXPECT_GE(width(), previous_width_);
        EXPECT_EQ(initial_image_x_, GetImageBounds().x());
        EXPECT_GE(GetImageBounds().x(), 0);
        if (GetImageBounds().width() > 0)
          EXPECT_LE(GetImageBounds().right(), width());
        EXPECT_TRUE(IsLabelVisible());
        if (GetLabelBounds().width() > 0) {
          EXPECT_GT(GetLabelBounds().x(), GetImageBounds().right());
          EXPECT_LT(GetLabelBounds().right(), width());
        }
        break;
      }
      case TestIconLabelBubbleView::State::STEADY: {
        if (steady_reached_)
          EXPECT_EQ(previous_width_, width());
        EXPECT_EQ(initial_image_x_, GetImageBounds().x());
        EXPECT_LT(GetImageBounds().right(), width());
        EXPECT_TRUE(IsLabelVisible());
        EXPECT_GT(GetLabelBounds().x(), GetImageBounds().right());
        EXPECT_LT(GetLabelBounds().right(), width());
        steady_reached_ = true;
        break;
      }
      case TestIconLabelBubbleView::State::SHRINKING: {
        if (shrinking_reached_)
          EXPECT_LE(width(), previous_width_);
        if (minimum_size_reached_)
          EXPECT_EQ(previous_width_, width());

        EXPECT_GE(GetImageBounds().x(), 0);
        if (width() <= initial_image_x_ + kImageSize) {
          EXPECT_EQ(width(), GetImageBounds().right());
          EXPECT_EQ(0, GetLabelBounds().width());
        } else {
          EXPECT_EQ(initial_image_x_, GetImageBounds().x());
          EXPECT_LE(GetImageBounds().right(), width());
        }
        if (GetLabelBounds().width() > 0) {
          EXPECT_GT(GetLabelBounds().x(), GetImageBounds().right());
          EXPECT_LT(GetLabelBounds().right(), width());
        }
        shrinking_reached_ = true;
        if (width() == kImageSize)
          minimum_size_reached_ = true;
        break;
      }
    }
    previous_width_ = width();
  }

  void SetValue(int value) { view_->SetCurrentAnimationValue(value); }

  TestIconLabelBubbleView::State state() const { return view_->state(); }

  int width() { return view_->width(); }

  bool IsLabelVisible() { return view_->IsLabelVisible(); }

  const gfx::Rect& GetLabelBounds() const { return view_->GetLabelBounds(); }

  const gfx::Rect& GetImageBounds() const {
    return view_->GetImageView()->bounds();
  }

  std::unique_ptr<TestIconLabelBubbleView> view_;

  bool steady_reached_;
  bool shrinking_reached_;
  bool minimum_size_reached_;
  int previous_width_;
  int initial_image_x_;
};

// Tests layout rules for IconLabelBubbleView while simulating animation.
// The animation is first growing the bubble from zero, then keeping its size
// constant and finally shrinking it down to its minimum size which is the image
// size.
// Various step sizes during animation simulate different possible timing.
TEST_F(IconLabelBubbleViewTest, AnimateLayout) {
  VerifyWithAnimationStep(1);
  VerifyWithAnimationStep(5);
  VerifyWithAnimationStep(10);
  VerifyWithAnimationStep(25);
}

#if defined(USE_ASH)
// Verifies IconLabelBubbleView::GetPreferredSize() doesn't crash when there is
// a widget but no compositor.
using IconLabelBubbleViewCrashTest = views::ViewsTestBase;

TEST_F(IconLabelBubbleViewCrashTest,
       GetPreferredSizeDoesntCrashWhenNoCompositor) {
  gfx::FontList font_list;
  views::Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  views::Widget widget;
  widget.Init(params);
  IconLabelBubbleView* icon_label_bubble_view =
      new TestIconLabelBubbleView(font_list);
  icon_label_bubble_view->SetLabel(base::ASCIIToUTF16("x"));
  widget.GetContentsView()->AddChildView(icon_label_bubble_view);
  aura::Window* widget_native_view = widget.GetNativeView();
  // Remove the window from its parent. This means GetWidget() in
  // IconLabelBubbleView will return non-null, but GetWidget()->GetCompositor()
  // will return null.
  ASSERT_TRUE(widget_native_view->parent());
  widget_native_view->parent()->RemoveChild(widget_native_view);
  static_cast<views::View*>(icon_label_bubble_view)->GetPreferredSize();
}
#endif

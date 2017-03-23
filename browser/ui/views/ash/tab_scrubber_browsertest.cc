// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/ash/tab_scrubber.h"

#include <memory>

#include "ash/display/event_transformation_handler.h"
#include "ash/shell.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_utils.h"
#include "ui/aura/window.h"
#include "ui/events/event_utils.h"
#include "ui/events/test/event_generator.h"

#if defined(OS_CHROMEOS)
#include "chromeos/chromeos_switches.h"
#endif

namespace {

// Waits until the immersive mode reveal ends, and therefore the top view of
// the browser is no longer visible.
class ImmersiveRevealEndedWaiter : public ImmersiveModeController::Observer {
 public:
  explicit ImmersiveRevealEndedWaiter(
      ImmersiveModeController* immersive_controller)
      : immersive_controller_(immersive_controller) {
    immersive_controller_->AddObserver(this);
  }

  ~ImmersiveRevealEndedWaiter() override {
    if (immersive_controller_)
      immersive_controller_->RemoveObserver(this);
  }

  void Wait() {
    if (!immersive_controller_ || !immersive_controller_->IsRevealed())
      return;

    base::RunLoop run_loop;
    quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

 private:
  void MaybeQuitRunLoop() {
    if (!quit_closure_.is_null())
      base::ResetAndReturn(&quit_closure_).Run();
  }

  // ImmersiveModeController::Observer:
  void OnImmersiveRevealEnded() override { MaybeQuitRunLoop(); }

  void OnImmersiveModeControllerDestroyed() override {
    MaybeQuitRunLoop();
    immersive_controller_->RemoveObserver(this);
    immersive_controller_ = nullptr;
  }

  ImmersiveModeController* immersive_controller_;
  base::Closure quit_closure_;

  DISALLOW_COPY_AND_ASSIGN(ImmersiveRevealEndedWaiter);
};

class TabScrubberTest : public InProcessBrowserTest,
                        public TabStripModelObserver {
 public:
  TabScrubberTest()
      : target_index_(-1) {
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
#if defined(OS_CHROMEOS)
    command_line->AppendSwitch(chromeos::switches::kNaturalScrollDefault);
#endif
    command_line->AppendSwitch(switches::kOpenAsh);
  }

  void SetUpOnMainThread() override {
    TabScrubber::GetInstance()->set_activation_delay(0);

    // Disable external monitor scaling of coordinates.
    ash::Shell* shell = ash::Shell::GetInstance();
    shell->event_transformation_handler()->set_transformation_mode(
        ash::EventTransformationHandler::TRANSFORM_NONE);
  }

  void TearDownOnMainThread() override {
    browser()->tab_strip_model()->RemoveObserver(this);
  }

  TabStrip* GetTabStrip(Browser* browser) {
    aura::Window* window = browser->window()->GetNativeWindow();
    return BrowserView::GetBrowserViewForNativeWindow(window)->tabstrip();
  }

  float GetStartX(Browser* browser,
                  int index,
                  TabScrubber::Direction direction) {
    return static_cast<float>(TabScrubber::GetStartPoint(
        GetTabStrip(browser), index, direction).x());
  }

  float GetTabCenter(Browser* browser, int index) {
    return static_cast<float>(GetTabStrip(browser)
                                  ->tab_at(index)
                                  ->GetMirroredBounds()
                                  .CenterPoint()
                                  .x());
  }

  // The simulated scroll event's offsets are calculated in the tests rather
  // than generated by the real event system. For the offsets calculation to be
  // correct in an RTL layout, we must invert the direction since it is
  // calculated here in the tests based on the indices and they're inverted in
  // RTL layouts:
  // Tab indices in an English layout : 0 - 1 - 2 - 3 - 4.
  // Tab indices in an Arabic layout  : 4 - 3 - 2 - 1 - 0.
  TabScrubber::Direction InvertDirectionIfNeeded(
      TabScrubber::Direction direction) {
    if (base::i18n::IsRTL()) {
      return direction == TabScrubber::LEFT ? TabScrubber::RIGHT
                                            : TabScrubber::LEFT;
    }

    return direction;
  }

  // Sends one scroll event synchronously without initial or final
  // fling events.
  void SendScrubEvent(Browser* browser, int index) {
    aura::Window* window = browser->window()->GetNativeWindow();
    aura::Window* root = window->GetRootWindow();
    ui::test::EventGenerator event_generator(root, window);
    int active_index = browser->tab_strip_model()->active_index();
    TabScrubber::Direction direction = index < active_index ?
        TabScrubber::LEFT : TabScrubber::RIGHT;

    direction = InvertDirectionIfNeeded(direction);

    float offset = GetTabCenter(browser, index) -
        GetStartX(browser, active_index, direction);
    ui::ScrollEvent scroll_event(ui::ET_SCROLL, gfx::Point(0, 0),
                                 ui::EventTimeForNow(), 0, offset, 0, offset, 0,
                                 3);
    event_generator.Dispatch(&scroll_event);
  }

  enum ScrubType {
    EACH_TAB,
    SKIP_TABS,
    REPEAT_TABS,
  };

  // Sends asynchronous events and waits for tab at |index| to become
  // active.
  void Scrub(Browser* browser, int index, ScrubType scrub_type) {
    aura::Window* window = browser->window()->GetNativeWindow();
    aura::Window* root = window->GetRootWindow();
    ui::test::EventGenerator event_generator(root, window);
    event_generator.set_async(true);
    activation_order_.clear();
    int active_index = browser->tab_strip_model()->active_index();
    ASSERT_NE(index, active_index);
    ASSERT_TRUE(scrub_type != SKIP_TABS || ((index - active_index) % 2) == 0);
    TabScrubber::Direction direction;
    int increment;
    if (index < active_index) {
      direction = TabScrubber::LEFT;
      increment = -1;
    } else {
      direction = TabScrubber::RIGHT;
      increment = 1;
    }

    direction = InvertDirectionIfNeeded(direction);

    if (scrub_type == SKIP_TABS)
      increment *= 2;
    float last = GetStartX(browser, active_index, direction);
    std::vector<gfx::PointF> offsets;
    for (int i = active_index + increment; i != (index + increment);
         i += increment) {
      float tab_center = GetTabCenter(browser, i);
      offsets.push_back(gfx::PointF(tab_center - last, 0));
      last = GetStartX(browser, i, direction);
      if (scrub_type == REPEAT_TABS) {
        offsets.push_back(gfx::PointF(static_cast<float>(increment), 0));
        last += increment;
      }
    }
    event_generator.ScrollSequence(gfx::Point(0, 0),
                                   base::TimeDelta::FromMilliseconds(100),
                                   offsets,
                                   3);
    RunUntilTabActive(browser, index);
  }

  // Sends events and waits for tab at |index| to become active
  // if it's different from the currently active tab.
  // If the active tab is expected to stay the same, send events
  // synchronously (as we don't have anything to wait for).
  void SendScrubSequence(Browser* browser, float x_offset, int index) {
    aura::Window* window = browser->window()->GetNativeWindow();
    aura::Window* root = window->GetRootWindow();
    ui::test::EventGenerator event_generator(root, window);
    bool wait_for_active = false;
    if (index != browser->tab_strip_model()->active_index()) {
      wait_for_active = true;
      event_generator.set_async(true);
    }
    event_generator.ScrollSequence(gfx::Point(0, 0),
                                   base::TimeDelta::FromMilliseconds(100),
                                   x_offset, 0, 1, 3);
    if (wait_for_active)
      RunUntilTabActive(browser, index);
  }

  void AddTabs(Browser* browser, int num_tabs) {
    TabStrip* tab_strip = GetTabStrip(browser);
    for (int i = 0; i < num_tabs; ++i)
      AddBlankTabAndShow(browser);
    ASSERT_EQ(num_tabs + 1, browser->tab_strip_model()->count());
    ASSERT_EQ(num_tabs, browser->tab_strip_model()->active_index());
    tab_strip->StopAnimating(true);
    ASSERT_FALSE(tab_strip->IsAnimating());
  }

  // TabStripModelObserver overrides.
  void ActiveTabChanged(content::WebContents* old_contents,
                        content::WebContents* new_contents,
                        int index,
                        int reason) override {
    activation_order_.push_back(index);
    if (index == target_index_)
      quit_closure_.Run();
  }

  // History of tab activation. Scrub() resets it.
  std::vector<int> activation_order_;

 private:
  void RunUntilTabActive(Browser* browser, int target) {
    base::RunLoop run_loop;
    quit_closure_ = content::GetDeferredQuitTaskForRunLoop(&run_loop);
    browser->tab_strip_model()->AddObserver(this);
    target_index_ = target;
    content::RunThisRunLoop(&run_loop);
    browser->tab_strip_model()->RemoveObserver(this);
    target_index_ = -1;
  }

  base::Closure quit_closure_;
  int target_index_;

  DISALLOW_COPY_AND_ASSIGN(TabScrubberTest);
};

}  // namespace

#if defined(OS_CHROMEOS)
// Swipe a single tab in each direction.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, Single) {
  AddTabs(browser(), 1);

  Scrub(browser(), 0, EACH_TAB);
  EXPECT_EQ(1U, activation_order_.size());
  EXPECT_EQ(0, activation_order_[0]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 1, EACH_TAB);
  EXPECT_EQ(1U, activation_order_.size());
  EXPECT_EQ(1, activation_order_[0]);
  EXPECT_EQ(1, browser()->tab_strip_model()->active_index());
}

// Swipe 4 tabs in each direction. Each of the tabs should become active.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, Multi) {
  AddTabs(browser(), 4);

  Scrub(browser(), 0, EACH_TAB);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(3, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(1, activation_order_[2]);
  EXPECT_EQ(0, activation_order_[3]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 4, EACH_TAB);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(1, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(3, activation_order_[2]);
  EXPECT_EQ(4, activation_order_[3]);
  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
}

IN_PROC_BROWSER_TEST_F(TabScrubberTest, MultiBrowser) {
  AddTabs(browser(), 1);
  Scrub(browser(), 0, EACH_TAB);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Browser* browser2 = CreateBrowser(browser()->profile());
  browser2->window()->Activate();
  ASSERT_TRUE(browser2->window()->IsActive());
  ASSERT_FALSE(browser()->window()->IsActive());
  AddTabs(browser2, 1);

  Scrub(browser2, 0, EACH_TAB);
  EXPECT_EQ(0, browser2->tab_strip_model()->active_index());
}

// Tests that tab scrubbing works correctly for a full-screen browser.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, FullScreenBrowser) {
  AddTabs(browser(), 6);
  browser()->tab_strip_model()->ActivateTabAt(4, false);

  chrome::ToggleFullscreenMode(browser());
  BrowserView* browser_view = BrowserView::GetBrowserViewForNativeWindow(
      browser()->window()->GetNativeWindow());
  ImmersiveModeController* immersive_controller =
      browser_view->immersive_mode_controller();
  EXPECT_TRUE(immersive_controller->IsEnabled());

  ImmersiveRevealEndedWaiter waiter(immersive_controller);
  waiter.Wait();

  EXPECT_FALSE(immersive_controller->IsRevealed());

  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
  Scrub(browser(), 0, EACH_TAB);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());
  EXPECT_EQ(4U, activation_order_.size());
  EXPECT_EQ(3, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(1, activation_order_[2]);
  EXPECT_EQ(0, activation_order_[3]);
}

// Swipe 4 tabs in each direction with an extra swipe within each. The same
// 4 tabs should become active.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, Repeated) {
  AddTabs(browser(), 4);

  Scrub(browser(), 0, REPEAT_TABS);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(3, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(1, activation_order_[2]);
  EXPECT_EQ(0, activation_order_[3]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 4, REPEAT_TABS);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(1, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(3, activation_order_[2]);
  EXPECT_EQ(4, activation_order_[3]);
  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
}

// Confirm that we get the last tab made active when we skip tabs.
// These tests have 5 total tabs. We will only received scroll events
// on tabs 0, 2 and 4.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, Skipped) {
  AddTabs(browser(), 4);

  Scrub(browser(), 0, SKIP_TABS);
  EXPECT_EQ(2U, activation_order_.size());
  EXPECT_EQ(2, activation_order_[0]);
  EXPECT_EQ(0, activation_order_[1]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 4, SKIP_TABS);
  EXPECT_EQ(2U, activation_order_.size());
  EXPECT_EQ(2, activation_order_[0]);
  EXPECT_EQ(4, activation_order_[1]);
  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
}

// Confirm that nothing happens when the swipe is small.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, NoChange) {
  AddTabs(browser(), 1);

  SendScrubSequence(browser(), -1, 1);
  EXPECT_EQ(1, browser()->tab_strip_model()->active_index());

  SendScrubSequence(browser(), 1, 1);
  EXPECT_EQ(1, browser()->tab_strip_model()->active_index());
}

// Confirm that very large swipes go to the beginning and and of the tabstrip.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, Bounds) {
  AddTabs(browser(), 1);

  SendScrubSequence(browser(), -10000, 0);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  SendScrubSequence(browser(), 10000, 1);
  EXPECT_EQ(1, browser()->tab_strip_model()->active_index());
}

IN_PROC_BROWSER_TEST_F(TabScrubberTest, DeleteHighlighted) {
  AddTabs(browser(), 1);

  SendScrubEvent(browser(), 0);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->CloseWebContentsAt(0,
                                                   TabStripModel::CLOSE_NONE);
  EXPECT_FALSE(TabScrubber::GetInstance()->IsActivationPending());
}

// Delete the currently highlighted tab. Make sure the TabScrubber is aware.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, DeleteBeforeHighlighted) {
  AddTabs(browser(), 2);

  SendScrubEvent(browser(), 1);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->CloseWebContentsAt(0,
                                                   TabStripModel::CLOSE_NONE);
  EXPECT_EQ(0, TabScrubber::GetInstance()->highlighted_tab());
}

// Move the currently highlighted tab and confirm it gets tracked.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, MoveHighlighted) {
  AddTabs(browser(), 1);

  SendScrubEvent(browser(), 0);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->ToggleSelectionAt(0);
  browser()->tab_strip_model()->ToggleSelectionAt(1);
  browser()->tab_strip_model()->MoveSelectedTabsTo(1);
  EXPECT_EQ(1, TabScrubber::GetInstance()->highlighted_tab());
}

// Move a tab to before the highlighted one. Make sure that the highlighted tab
// index is updated correctly.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, MoveBefore) {
  AddTabs(browser(), 2);

  SendScrubEvent(browser(), 1);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->ToggleSelectionAt(0);
  browser()->tab_strip_model()->ToggleSelectionAt(2);
  browser()->tab_strip_model()->MoveSelectedTabsTo(2);
  EXPECT_EQ(0, TabScrubber::GetInstance()->highlighted_tab());
}

// Move a tab to after the highlighted one. Make sure that the highlighted tab
// index is updated correctly.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, MoveAfter) {
  AddTabs(browser(), 2);

  SendScrubEvent(browser(), 1);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->MoveSelectedTabsTo(0);
  EXPECT_EQ(2, TabScrubber::GetInstance()->highlighted_tab());
}

// Close the browser while an activation is pending.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, CloseBrowser) {
  AddTabs(browser(), 1);

  SendScrubEvent(browser(), 0);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->window()->Close();
  EXPECT_FALSE(TabScrubber::GetInstance()->IsActivationPending());
}

// In an RTL layout, swipe 4 tabs in each direction. Each of the tabs should
// become active.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, RTLMulti) {
  base::i18n::SetICUDefaultLocale("ar");
  ASSERT_TRUE(base::i18n::IsRTL());

  AddTabs(browser(), 4);

  Scrub(browser(), 0, EACH_TAB);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(3, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(1, activation_order_[2]);
  EXPECT_EQ(0, activation_order_[3]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 4, EACH_TAB);
  ASSERT_EQ(4U, activation_order_.size());
  EXPECT_EQ(1, activation_order_[0]);
  EXPECT_EQ(2, activation_order_[1]);
  EXPECT_EQ(3, activation_order_[2]);
  EXPECT_EQ(4, activation_order_[3]);
  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
}

// In an RTL layout, confirm that we get the last tab made active when we skip
// tabs. These tests have 5 total tabs. We will only received scroll events
// on tabs 0, 2 and 4.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, RTLSkipped) {
  base::i18n::SetICUDefaultLocale("ar");
  ASSERT_TRUE(base::i18n::IsRTL());

  AddTabs(browser(), 4);

  Scrub(browser(), 0, SKIP_TABS);
  EXPECT_EQ(2U, activation_order_.size());
  EXPECT_EQ(2, activation_order_[0]);
  EXPECT_EQ(0, activation_order_[1]);
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());

  Scrub(browser(), 4, SKIP_TABS);
  EXPECT_EQ(2U, activation_order_.size());
  EXPECT_EQ(2, activation_order_[0]);
  EXPECT_EQ(4, activation_order_[1]);
  EXPECT_EQ(4, browser()->tab_strip_model()->active_index());
}

// In an RTL layout, move a tab to before the highlighted one. Make sure that
// the highlighted tab index is updated correctly.
IN_PROC_BROWSER_TEST_F(TabScrubberTest, RTLMoveBefore) {
  base::i18n::SetICUDefaultLocale("ar");
  ASSERT_TRUE(base::i18n::IsRTL());

  AddTabs(browser(), 2);

  SendScrubEvent(browser(), 1);
  EXPECT_TRUE(TabScrubber::GetInstance()->IsActivationPending());
  browser()->tab_strip_model()->ToggleSelectionAt(0);
  browser()->tab_strip_model()->ToggleSelectionAt(2);
  browser()->tab_strip_model()->MoveSelectedTabsTo(2);
  EXPECT_EQ(0, TabScrubber::GetInstance()->highlighted_tab());
}

#endif  // defined(OS_CHROMEOS)

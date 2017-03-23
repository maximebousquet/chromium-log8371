// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/profiles/signin_view_controller_delegate_mac.h"

#import <Cocoa/Cocoa.h>

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/ui/browser.h"
#import "chrome/browser/ui/cocoa/browser_window_utils.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_sheet.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/url_constants.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

namespace {

// Dimensions of the web contents containing the old-style signin flow with the
// username and password challenge on the same form.
const CGFloat kPasswordCombinedFixedGaiaViewHeight = 440;
const CGFloat kPasswordCombinedFixedGaiaViewWidth = 360;

// Width of the different dialogs that make up the signin flow.
const int kModalDialogWidth = 448;

// Height of the tab-modal dialog displaying the password-separated signin
// flow. It matches the dimensions of the server content the dialog displays.
const CGFloat kFixedGaiaViewHeight = 612;

// Initial height of the sync confirmation modal dialog.
const int kSyncConfirmationDialogHeight = 351;

// Initial height of the signin error modal dialog.
const int kSigninErrorDialogHeight = 164;

CGFloat GetSyncConfirmationDialogPreferredHeight(Profile* profile) {
  // If sync is disabled, then the sync confirmation dialog looks like an error
  // dialog and thus it has the same preferred size.
  return profile->IsSyncAllowed() ? kSyncConfirmationDialogHeight
                                  : kSigninErrorDialogHeight;
}

}  // namespace

SigninViewControllerDelegateMac::SigninViewControllerDelegateMac(
    SigninViewController* signin_view_controller,
    std::unique_ptr<content::WebContents> web_contents,
    Browser* browser,
    NSRect frame,
    ui::ModalType dialog_modal_type,
    bool wait_for_size)
    : SigninViewControllerDelegate(signin_view_controller, web_contents.get()),
      web_contents_(std::move(web_contents)),
      browser_(browser),
      dialog_modal_type_(dialog_modal_type),
      window_frame_(frame) {
  DCHECK(browser_);
  DCHECK(browser_->tab_strip_model()->GetActiveWebContents())
      << "A tab must be active to present the sign-in modal dialog.";

  if (!wait_for_size)
    DisplayModal();
}

SigninViewControllerDelegateMac::~SigninViewControllerDelegateMac() {}

void SigninViewControllerDelegateMac::OnConstrainedWindowClosed(
    ConstrainedWindowMac* window) {
  CleanupAndDeleteThis();
}

// static
std::unique_ptr<content::WebContents>
SigninViewControllerDelegateMac::CreateGaiaWebContents(
    content::WebContentsDelegate* delegate,
    profiles::BubbleViewMode mode,
    Profile* profile,
    signin_metrics::AccessPoint access_point) {
  GURL url =
      signin::GetSigninURLFromBubbleViewMode(profile, mode, access_point);

  std::unique_ptr<content::WebContents> web_contents(
      content::WebContents::Create(
          content::WebContents::CreateParams(profile)));

  if (delegate)
    web_contents->SetDelegate(delegate);

  web_contents->GetController().LoadURL(url, content::Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                        std::string());
  NSView* webview = web_contents->GetNativeView();
  [webview
      setFrameSize:switches::UsePasswordSeparatedSigninFlow()
                       ? NSMakeSize(kModalDialogWidth, kFixedGaiaViewHeight)
                       : NSMakeSize(kPasswordCombinedFixedGaiaViewWidth,
                                    kPasswordCombinedFixedGaiaViewHeight)];

  content::RenderWidgetHostView* rwhv = web_contents->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetBackgroundColor(profiles::kAvatarBubbleGaiaBackgroundColor);

  return web_contents;
}

// static
std::unique_ptr<content::WebContents>
SigninViewControllerDelegateMac::CreateSyncConfirmationWebContents(
    Profile* profile) {
  std::unique_ptr<content::WebContents> web_contents(
      content::WebContents::Create(
          content::WebContents::CreateParams(profile)));
  web_contents->GetController().LoadURL(
      GURL(chrome::kChromeUISyncConfirmationURL), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());

  NSView* webview = web_contents->GetNativeView();
  [webview setFrameSize:NSMakeSize(
                            kModalDialogWidth,
                            GetSyncConfirmationDialogPreferredHeight(profile))];

  return web_contents;
}

// static
std::unique_ptr<content::WebContents>
SigninViewControllerDelegateMac::CreateSigninErrorWebContents(
    Profile* profile) {
  std::unique_ptr<content::WebContents> web_contents(
      content::WebContents::Create(
          content::WebContents::CreateParams(profile)));
  web_contents->GetController().LoadURL(
      GURL(chrome::kChromeUISigninErrorURL), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());

  NSView* webview = web_contents->GetNativeView();
  [webview
      setFrameSize:NSMakeSize(kModalDialogWidth, kSigninErrorDialogHeight)];

  return web_contents;
}

void SigninViewControllerDelegateMac::PerformClose() {
  switch (dialog_modal_type_) {
    case ui::MODAL_TYPE_CHILD:
      if (constrained_window_.get())
        constrained_window_->CloseWebContentsModalDialog();
      break;
    case ui::MODAL_TYPE_WINDOW:
      if (window_.get()) {
        [window_.get().sheetParent endSheet:window_];
        window_.reset(nil);
        CleanupAndDeleteThis();
      }
      break;
    default:
      NOTREACHED() << "Unsupported dialog modal type " << dialog_modal_type_;
  }
}

void SigninViewControllerDelegateMac::ResizeNativeView(int height) {
  if (!window_) {
    window_frame_.size = NSMakeSize(kModalDialogWidth, height);
    DisplayModal();
  }
}

void SigninViewControllerDelegateMac::DisplayModal() {
  DCHECK(!window_);

  content::WebContents* host_web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();

  // Avoid displaying the sign-in modal view if there are no active web
  // contents. This happens if the user closes the browser window before this
  // dialog has a chance to be displayed.
  if (!host_web_contents)
    return;

  window_.reset(
      [[ConstrainedWindowCustomWindow alloc]
          initWithContentRect:window_frame_]);
  window_.get().contentView = web_contents_->GetNativeView();
  base::scoped_nsobject<CustomConstrainedWindowSheet> sheet(
      [[CustomConstrainedWindowSheet alloc] initWithCustomWindow:window_]);
  switch (dialog_modal_type_) {
    case ui::MODAL_TYPE_CHILD:
      constrained_window_ =
          CreateAndShowWebModalDialogMac(this, host_web_contents, sheet);
      break;
    case ui::MODAL_TYPE_WINDOW:
      [host_web_contents->GetTopLevelNativeWindow() beginSheet:window_
                                             completionHandler:nil];
      break;
    default:
      NOTREACHED() << "Unsupported dialog modal type " << dialog_modal_type_;
  }
}

void SigninViewControllerDelegateMac::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  int chrome_command_id = [BrowserWindowUtils getCommandId:event];
  bool can_handle_command = [BrowserWindowUtils isTextEditingEvent:event] ||
                            chrome_command_id == IDC_CLOSE_WINDOW ||
                            chrome_command_id == IDC_EXIT;
  if ([BrowserWindowUtils shouldHandleKeyboardEvent:event] &&
      can_handle_command) {
    [[NSApp mainMenu] performKeyEquivalent:event.os_event];
  }
}

void SigninViewControllerDelegateMac::CleanupAndDeleteThis() {
  ResetSigninViewControllerDelegate();
  delete this;
}

// static
SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateModalSigninDelegate(
    SigninViewController* signin_view_controller,
    profiles::BubbleViewMode mode,
    Browser* browser,
    signin_metrics::AccessPoint access_point) {
  return new SigninViewControllerDelegateMac(
      signin_view_controller,
      SigninViewControllerDelegateMac::CreateGaiaWebContents(
          nullptr, mode, browser->profile(), access_point),
      browser, NSMakeRect(0, 0, kModalDialogWidth, kFixedGaiaViewHeight),
      ui::MODAL_TYPE_CHILD, false /* wait_for_size */);
}

// static
SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSyncConfirmationDelegate(
    SigninViewController* signin_view_controller,
    Browser* browser) {
  return new SigninViewControllerDelegateMac(
      signin_view_controller,
      SigninViewControllerDelegateMac::CreateSyncConfirmationWebContents(
          browser->profile()),
      browser,
      NSMakeRect(0, 0, kModalDialogWidth,
                 GetSyncConfirmationDialogPreferredHeight(browser->profile())),
      ui::MODAL_TYPE_WINDOW, true /* wait_for_size */);
}

// static
SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSigninErrorDelegate(
    SigninViewController* signin_view_controller,
    Browser* browser) {
  return new SigninViewControllerDelegateMac(
      signin_view_controller,
      SigninViewControllerDelegateMac::CreateSigninErrorWebContents(
          browser->profile()),
      browser, NSMakeRect(0, 0, kModalDialogWidth, kSigninErrorDialogHeight),
      ui::MODAL_TYPE_WINDOW, true /* wait_for_size */);
}

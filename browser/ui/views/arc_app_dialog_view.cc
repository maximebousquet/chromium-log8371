// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/arc/arc_app_dialog.h"

#include "base/bind.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/app_list_controller_delegate.h"
#include "chrome/browser/ui/app_list/arc/arc_app_icon_loader.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/native_window_tracker.h"
#include "chrome/browser/ui/views/harmony/layout_delegate.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace arc {

namespace {

const int kRightColumnWidth = 210;
const int kIconSize = 64;
// Currenty ARC apps only support 48*48 native icon.
const int kIconSourceSize = 48;

using ArcAppConfirmCallback =
    base::Callback<void(const std::string& app_id, Profile* profile)>;

// Helper class to hold a smaller icon in a fixed-size view.
class FixedBoundarySizeImageView : public views::ImageView {
 public:
  FixedBoundarySizeImageView() {}
  ~FixedBoundarySizeImageView() override {}
  // Overriden from View:
  gfx::Size GetPreferredSize() const override {
    return gfx::Size(kIconSize, kIconSize);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FixedBoundarySizeImageView);
};

class ArcAppDialogView : public views::DialogDelegateView,
                         public AppIconLoaderDelegate {
 public:
  ArcAppDialogView(Profile* profile,
                   AppListControllerDelegate* controller,
                   const std::string& app_id,
                   const base::string16& window_title,
                   const base::string16& heading_text,
                   const base::string16& subheading_text,
                   const base::string16& confirm_button_text,
                   const base::string16& cancel_button_text,
                   ArcAppConfirmCallback confirm_callback);
  ~ArcAppDialogView() override;

  // Public method used for test only.
  void ConfirmOrCancelForTest(bool confirm);

 private:
  // views::WidgetDelegate:
  base::string16 GetWindowTitle() const override;
  void DeleteDelegate() override;
  ui::ModalType GetModalType() const override;

  // views::DialogDelegate:
  base::string16 GetDialogButtonLabel(ui::DialogButton button) const override;
  bool Accept() override;

  // AppIconLoaderDelegate:
  void OnAppImageUpdated(const std::string& app_id,
                         const gfx::ImageSkia& image) override;

  void AddMultiLineLabel(views::View* parent, const base::string16& label_text);

  // Constructs and shows the modal dialog widget.
  void Show();

  bool initial_setup_ = true;

  views::ImageView* icon_view_ = nullptr;

  std::unique_ptr<ArcAppIconLoader> icon_loader_;

  Profile* const profile_;

  AppListControllerDelegate* controller_;

  gfx::NativeWindow parent_;

  // Tracks whether |parent_| got destroyed.
  std::unique_ptr<NativeWindowTracker> parent_window_tracker_;

  const std::string app_id_;
  const base::string16 window_title_;
  const base::string16 confirm_button_text_;
  const base::string16 cancel_button_text_;
  ArcAppConfirmCallback confirm_callback_;

  DISALLOW_COPY_AND_ASSIGN(ArcAppDialogView);
};

// Browertest use only. Global pointer of ArcAppDialogView which is shown.
ArcAppDialogView* g_current_arc_app_dialog_view = nullptr;

ArcAppDialogView::ArcAppDialogView(Profile* profile,
                                   AppListControllerDelegate* controller,
                                   const std::string& app_id,
                                   const base::string16& window_title,
                                   const base::string16& heading_text,
                                   const base::string16& subheading_text,
                                   const base::string16& confirm_button_text,
                                   const base::string16& cancel_button_text,
                                   ArcAppConfirmCallback confirm_callback)
    : profile_(profile),
      controller_(controller),
      app_id_(app_id),
      window_title_(window_title),
      confirm_button_text_(confirm_button_text),
      cancel_button_text_(cancel_button_text),
      confirm_callback_(confirm_callback) {
  DCHECK(controller);
  parent_ = controller_->GetAppListWindow();
  if (parent_)
    parent_window_tracker_ = NativeWindowTracker::Create(parent_);

  SetLayoutManager(new views::BoxLayout(
      views::BoxLayout::kHorizontal, views::kButtonHEdgeMarginNew,
      LayoutDelegate::Get()->GetMetric(
          LayoutDelegate::Metric::PANEL_CONTENT_MARGIN),
      views::kRelatedControlHorizontalSpacing));

  icon_view_ = new FixedBoundarySizeImageView();
  AddChildView(icon_view_);

  views::View* text_container = new views::View();
  views::BoxLayout* text_container_layout =
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0);
  text_container_layout->set_main_axis_alignment(
      views::BoxLayout::MAIN_AXIS_ALIGNMENT_CENTER);
  text_container_layout->set_cross_axis_alignment(
      views::BoxLayout::CROSS_AXIS_ALIGNMENT_START);
  text_container->SetLayoutManager(text_container_layout);

  AddChildView(text_container);
  DCHECK(!heading_text.empty());
  AddMultiLineLabel(text_container, heading_text);
  if (!subheading_text.empty())
    AddMultiLineLabel(text_container, subheading_text);

  icon_loader_.reset(new ArcAppIconLoader(profile_, kIconSourceSize, this));
  // The dialog will show once the icon is loaded.
  icon_loader_->FetchImage(app_id_);
}

ArcAppDialogView::~ArcAppDialogView() {
  DCHECK_EQ(this, g_current_arc_app_dialog_view);
  g_current_arc_app_dialog_view = nullptr;
}

void ArcAppDialogView::AddMultiLineLabel(views::View* parent,
                                         const base::string16& label_text) {
  views::Label* label = new views::Label(label_text);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAllowCharacterBreak(true);
  label->SizeToFit(kRightColumnWidth);
  parent->AddChildView(label);
}

void ArcAppDialogView::ConfirmOrCancelForTest(bool confirm) {
  if (confirm)
    Accept();
  else
    Cancel();
  GetWidget()->Close();
}

base::string16 ArcAppDialogView::GetWindowTitle() const {
  return window_title_;
}

void ArcAppDialogView::DeleteDelegate() {
  if (controller_)
    controller_->OnCloseChildDialog();
  DialogDelegateView::DeleteDelegate();
}

ui::ModalType ArcAppDialogView::GetModalType() const {
  return ui::MODAL_TYPE_WINDOW;
}

base::string16 ArcAppDialogView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return button == ui::DIALOG_BUTTON_CANCEL ? cancel_button_text_
                                            : confirm_button_text_;
}

bool ArcAppDialogView::Accept() {
  confirm_callback_.Run(app_id_, profile_);
  return true;
}

void ArcAppDialogView::OnAppImageUpdated(const std::string& app_id,
                                         const gfx::ImageSkia& image) {
  DCHECK_EQ(app_id, app_id_);
  DCHECK(!image.isNull());
  DCHECK_EQ(image.width(), kIconSourceSize);
  DCHECK_EQ(image.height(), kIconSourceSize);
  icon_view_->SetImageSize(image.size());
  icon_view_->SetImage(image);

  if (initial_setup_)
    Show();
}

void ArcAppDialogView::Show() {
  initial_setup_ = false;

  // The parent window was killed before the icon was loaded.
  if (parent_ && parent_window_tracker_->WasNativeWindowClosed()) {
    Cancel();
    DialogDelegateView::DeleteDelegate();
    return;
  }

  if (controller_)
    controller_->OnShowChildDialog();

  g_current_arc_app_dialog_view = this;
  constrained_window::CreateBrowserModalDialogViews(this, parent_)->Show();
}

}  // namespace

void ShowArcAppUninstallDialog(Profile* profile,
                               AppListControllerDelegate* controller,
                               const std::string& app_id) {
  ArcAppListPrefs* arc_prefs = ArcAppListPrefs::Get(profile);
  DCHECK(arc_prefs);
  std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
      arc_prefs->GetApp(app_id);

  if (!app_info)
    return;

  bool is_shortcut = app_info->shortcut;

  base::string16 window_title = l10n_util::GetStringUTF16(
      is_shortcut ? IDS_EXTENSION_UNINSTALL_PROMPT_TITLE
                  : IDS_APP_UNINSTALL_PROMPT_TITLE);

  base::string16 heading_text = base::UTF8ToUTF16(l10n_util::GetStringFUTF8(
      is_shortcut ? IDS_EXTENSION_UNINSTALL_PROMPT_HEADING
                  : IDS_NON_PLATFORM_APP_UNINSTALL_PROMPT_HEADING,
      base::UTF8ToUTF16(app_info->name)));
  base::string16 subheading_text;
  if (!is_shortcut) {
    subheading_text = l10n_util::GetStringUTF16(
        IDS_ARC_APP_UNINSTALL_PROMPT_DATA_REMOVAL_WARNING);
  }

  base::string16 confirm_button_text = l10n_util::GetStringUTF16(
      is_shortcut ? IDS_EXTENSION_PROMPT_UNINSTALL_BUTTON
                  : IDS_EXTENSION_PROMPT_UNINSTALL_APP_BUTTON);

  base::string16 cancel_button_text = l10n_util::GetStringUTF16(IDS_CANCEL);

  new ArcAppDialogView(profile, controller, app_id, window_title, heading_text,
                       subheading_text, confirm_button_text, cancel_button_text,
                       base::Bind(UninstallArcApp));
}

bool IsArcAppDialogViewAliveForTest() {
  return g_current_arc_app_dialog_view != nullptr;
}

bool CloseAppDialogViewAndConfirmForTest(bool confirm) {
  if (!g_current_arc_app_dialog_view)
    return false;

  g_current_arc_app_dialog_view->ConfirmOrCancelForTest(confirm);
  return true;
}

}  // namespace arc

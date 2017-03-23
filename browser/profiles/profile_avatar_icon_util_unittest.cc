// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_avatar_icon_util.h"

#include "chrome/grit/theme_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "url/gurl.h"

namespace {

// Helper function to check that the image is sized properly
// and supports multiple pixel densities.
void VerifyScaling(gfx::Image& image, gfx::Size& size) {
  gfx::Size canvas_size(100, 100);
  gfx::Canvas canvas(canvas_size, 1.0f, false);
  gfx::Canvas canvas2(canvas_size, 2.0f, false);

  ASSERT_FALSE(gfx::test::IsEmpty(image));
  EXPECT_EQ(image.Size(), size);

  gfx::ImageSkia image_skia = *image.ToImageSkia();
  canvas.DrawImageInt(image_skia, 15, 10);
  EXPECT_TRUE(image.ToImageSkia()->HasRepresentation(1.0f));

  canvas2.DrawImageInt(image_skia, 15, 10);
  EXPECT_TRUE(image.ToImageSkia()->HasRepresentation(2.0f));
}

TEST(ProfileInfoUtilTest, SizedMenuIcon) {
  // Test that an avatar icon isn't changed.
  const gfx::Image& profile_image(
      ResourceBundle::GetSharedInstance().GetImageNamed(IDR_PROFILE_AVATAR_0));
  gfx::Image result =
      profiles::GetSizedAvatarIcon(profile_image, false, 50, 50);

  EXPECT_FALSE(gfx::test::IsEmpty(result));
  EXPECT_TRUE(gfx::test::AreImagesEqual(profile_image, result));

  // Test that a rectangular picture (e.g., GAIA image) is changed.
  gfx::Image rect_picture(gfx::test::CreateImage());

  gfx::Size size(30, 20);
  gfx::Image result2 = profiles::GetSizedAvatarIcon(
      rect_picture, true, size.width(), size.height());

  VerifyScaling(result2, size);
}

TEST(ProfileInfoUtilTest, MenuIcon) {
  // Test that an avatar icon isn't changed.
  const gfx::Image& profile_image(
      ResourceBundle::GetSharedInstance().GetImageNamed(IDR_PROFILE_AVATAR_0));
  gfx::Image result = profiles::GetAvatarIconForMenu(profile_image, false);
  EXPECT_FALSE(gfx::test::IsEmpty(result));
  EXPECT_TRUE(gfx::test::AreImagesEqual(profile_image, result));

  // Test that a rectangular picture is changed.
  gfx::Image rect_picture(gfx::test::CreateImage());
  gfx::Size size(profiles::kAvatarIconWidth, profiles::kAvatarIconHeight);
  gfx::Image result2 = profiles::GetAvatarIconForMenu(rect_picture, true);

  VerifyScaling(result2, size);
}

TEST(ProfileInfoUtilTest, WebUIIcon) {
  // Test that an avatar icon isn't changed.
  const gfx::Image& profile_image(
      ResourceBundle::GetSharedInstance().GetImageNamed(IDR_PROFILE_AVATAR_0));
  gfx::Image result = profiles::GetAvatarIconForWebUI(profile_image, false);
  EXPECT_FALSE(gfx::test::IsEmpty(result));
  EXPECT_TRUE(gfx::test::AreImagesEqual(profile_image, result));

  // Test that a rectangular picture is changed.
  gfx::Image rect_picture(gfx::test::CreateImage());
  gfx::Size size(profiles::kAvatarIconWidth, profiles::kAvatarIconHeight);
  gfx::Image result2 = profiles::GetAvatarIconForWebUI(rect_picture, true);

  VerifyScaling(result2, size);
}

TEST(ProfileInfoUtilTest, TitleBarIcon) {
  int width = 100;
  int height = 40;

  // Test that an avatar icon isn't changed.
  const gfx::Image& profile_image(
      ResourceBundle::GetSharedInstance().GetImageNamed(IDR_PROFILE_AVATAR_0));
  gfx::Image result = profiles::GetAvatarIconForTitleBar(
      profile_image, false, width, height);
  EXPECT_FALSE(gfx::test::IsEmpty(result));
  EXPECT_TRUE(gfx::test::AreImagesEqual(profile_image, result));

  // Test that a rectangular picture is changed.
  gfx::Image rect_picture(gfx::test::CreateImage());

  gfx::Size size(width, height);
  gfx::Image result2 = profiles::GetAvatarIconForTitleBar(
      rect_picture, true, width, height);

  VerifyScaling(result2, size);
}

TEST(ProfileInfoUtilTest, GetImageURLWithThumbnailSizeNoInitialSize) {
  GURL initial_url(
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/photo.jpg");
  const std::string expected_url =
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/s128-c/photo.jpg";

  GURL transformed_url;
  EXPECT_TRUE(profiles::GetImageURLWithThumbnailSize(
      initial_url, 128, &transformed_url));

  EXPECT_EQ(transformed_url, GURL(expected_url));
}

TEST(ProfileInfoUtilTest, GetImageURLWithThumbnailSizeSizeAlreadySpecified) {
  // If there's already a size specified in the URL, it should be changed to the
  // specified size in the resulting URL.
  GURL initial_url(
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/s64-c/photo.jpg");
  const std::string expected_url =
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/s128-c/photo.jpg";

  GURL transformed_url;
  EXPECT_TRUE(profiles::GetImageURLWithThumbnailSize(
      initial_url, 128, &transformed_url));

  EXPECT_EQ(transformed_url, GURL(expected_url));
}

TEST(ProfileInfoUtilTest, GetImageURLWithThumbnailSizeSameSize) {
  // If there's already a size specified in the URL, and it's already the
  // requested size, true should be returned and the URL should remain
  // unchanged.
  GURL initial_url(
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/s64-c/photo.jpg");
  const std::string expected_url =
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/s64-c/photo.jpg";

  GURL transformed_url;
  EXPECT_TRUE(profiles::GetImageURLWithThumbnailSize(
      initial_url, 64, &transformed_url));

  EXPECT_EQ(transformed_url, GURL(expected_url));
}

TEST(ProfileInfoUtilTest, GetImageURLWithThumbnailSizeNoFileNameInPath) {
  GURL initial_url(
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/");
  const std::string expected_url =
      "https://example.com/--Abc/AAAAAAAAAAI/AAAAAAAAACQ/Efg/";

  // If there is no file path component in the URL path, we should fail the
  // modification, but return true since the URL is still potentially valid and
  // not modify the input URL.
  GURL new_url;
  EXPECT_TRUE(profiles::GetImageURLWithThumbnailSize(
      initial_url, 64, &new_url));

  EXPECT_EQ(new_url, GURL(expected_url));
}

TEST(ProfileInfoUtilTest, GetImageURLWithThumbnailInvalidURL) {
  GURL initial_url;

  GURL new_url("http://example.com");
  EXPECT_FALSE(profiles::GetImageURLWithThumbnailSize(
      initial_url, 128, &new_url));

  // The new URL should be unchanged since the transformation failed.
  EXPECT_EQ(new_url, GURL("http://example.com"));
}

}  // namespace

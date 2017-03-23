// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_launcher_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "media/base/test_data_util.h"
#include "media/media_features.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ppapi/features/features.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
#include "chrome/browser/media/pepper_cdm_test_constants.h"
#include "chrome/browser/media/pepper_cdm_test_helper.h"
#endif

#if defined(OS_ANDROID)
#error This file needs to be updated to run on Android.
#endif

#include "widevine_cdm_version.h"  // In SHARED_INTERMEDIATE_DIR.

namespace chrome {

namespace {

const char kClearKey[] = "org.w3.clearkey";
const char kExternalClearKey[] = "org.chromium.externalclearkey";
const char kWidevine[] = "com.widevine.alpha";

const char kAudioWebMMimeType[] = "audio/webm";
const char kVideoWebMMimeType[] = "video/webm";
const char kAudioMP4MimeType[] = "audio/mp4";
const char kVideoMP4MimeType[] = "video/mp4";

// These are the expected titles set by checkKeySystemWithMediaMimeType()
// in test_key_system_instantiation.html. Other titles are possible, but
// they are unexpected and will be logged with the failure.
// "Unsupported keySystem" and "None of the requested configurations were
// supported." are actually error messages generated by
// navigator.requestMediaKeySystemAccess(), and will have to change if that
// code is modified.
const char kSuccessResult[] = "success";
const char kUnsupportedResult[] = "Unsupported keySystem";
const char kNoMatchResult[] =
    "None of the requested configurations were supported.";
const char kUnexpectedResult[] = "unexpected result";

#define EXPECT_SUCCESS(test) EXPECT_EQ(kSuccessResult, test)
#define EXPECT_UNKNOWN_KEYSYSTEM(test) EXPECT_EQ(kUnsupportedResult, test)
#define EXPECT_NO_MATCH(test) EXPECT_EQ(kNoMatchResult, test)

#if BUILDFLAG(USE_PROPRIETARY_CODECS)
#define EXPECT_PROPRIETARY EXPECT_SUCCESS
#else
#define EXPECT_PROPRIETARY EXPECT_NO_MATCH
#endif

// Expectations for External Clear Key.
#if BUILDFLAG(ENABLE_PEPPER_CDMS)
#define EXPECT_ECK EXPECT_SUCCESS
#define EXPECT_ECK_PROPRIETARY EXPECT_PROPRIETARY
#define EXPECT_ECK_NO_MATCH EXPECT_NO_MATCH
#else
#define EXPECT_ECK EXPECT_UNKNOWN_KEYSYSTEM
#define EXPECT_ECK_PROPRIETARY EXPECT_UNKNOWN_KEYSYSTEM
#define EXPECT_ECK_NO_MATCH EXPECT_UNKNOWN_KEYSYSTEM
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)

// Expectations for Widevine.
#if defined(WIDEVINE_CDM_AVAILABLE)
#define EXPECT_WV_SUCCESS EXPECT_SUCCESS
#define EXPECT_WV_PROPRIETARY EXPECT_PROPRIETARY
#define EXPECT_WV_NO_MATCH EXPECT_NO_MATCH
#else  // defined(WIDEVINE_CDM_AVAILABLE)
#define EXPECT_WV_SUCCESS EXPECT_UNKNOWN_KEYSYSTEM
#define EXPECT_WV_PROPRIETARY EXPECT_UNKNOWN_KEYSYSTEM
#define EXPECT_WV_NO_MATCH EXPECT_UNKNOWN_KEYSYSTEM
#endif  // defined(WIDEVINE_CDM_AVAILABLE)

};  // namespace

class EncryptedMediaSupportedTypesTest : public InProcessBrowserTest {
 protected:
  EncryptedMediaSupportedTypesTest() {
    audio_webm_codecs_.push_back("opus");
    audio_webm_codecs_.push_back("vorbis");

    video_webm_codecs_.push_back("vp8");
    video_webm_codecs_.push_back("vp8.0");
    video_webm_codecs_.push_back("vp9");
    video_webm_codecs_.push_back("vp9.0");

    audio_mp4_codecs_.push_back("mp4a.40.2");

    video_mp4_codecs_.push_back("avc1.42001E");  // Baseline profile.
    video_mp4_codecs_.push_back("avc1.4D000C");  // Main profile.
    video_mp4_codecs_.push_back("avc3.64001F");  // High profile.

    video_mp4_codecs_.push_back("vp09.00.10.08");

    video_mp4_hi10p_codecs_.push_back("avc1.6E001E");  // Hi10P profile

#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
    video_mp4_codecs_.push_back("hvc1.1.6.L93.B0");
    video_mp4_codecs_.push_back("hev1.1.6.L93.B0");
#else
    invalid_codecs_.push_back("hvc1.1.6.L93.B0");
    invalid_codecs_.push_back("hev1.1.6.L93.B0");
#endif

    // Extended codecs are used, so make sure generic ones fail. These will be
    // tested against all initDataTypes as they should always fail to be
    // supported.
    invalid_codecs_.push_back("avc1");
    invalid_codecs_.push_back("avc1.");
    invalid_codecs_.push_back("avc3");

    // Other invalid codecs.
    invalid_codecs_.push_back("vp8.1");
    invalid_codecs_.push_back("mp4a");
    invalid_codecs_.push_back("avc2");
    invalid_codecs_.push_back("foo");

    // We only support proper long-form HEVC codec ids.
    invalid_codecs_.push_back("hev1");
    invalid_codecs_.push_back("hev1.");
    invalid_codecs_.push_back("hvc1");
    invalid_codecs_.push_back("hvc1.");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableVp9InMp4);
  }

  typedef std::vector<std::string> CodecVector;

  const CodecVector& no_codecs() const { return no_codecs_; }
  const CodecVector& audio_webm_codecs() const { return audio_webm_codecs_; }
  const CodecVector& video_webm_codecs() const { return video_webm_codecs_; }
  const CodecVector& audio_mp4_codecs() const { return audio_mp4_codecs_; }
  const CodecVector& video_mp4_codecs() const { return video_mp4_codecs_; }
  const CodecVector& video_mp4_hi10p_codecs() const {
    return video_mp4_hi10p_codecs_;
  }
  const CodecVector& invalid_codecs() const { return invalid_codecs_; }

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
  void SetUpDefaultCommandLine(base::CommandLine* command_line) override {
    base::CommandLine default_command_line(base::CommandLine::NO_PROGRAM);
    InProcessBrowserTest::SetUpDefaultCommandLine(&default_command_line);
    test_launcher_utils::RemoveCommandLineSwitch(
        default_command_line, switches::kDisableComponentUpdate, command_line);
  }
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    // Load the test page needed so that checkKeySystemWithMediaMimeType()
    // is available.
    std::unique_ptr<net::EmbeddedTestServer> http_test_server(
        new net::EmbeddedTestServer);
    http_test_server->ServeFilesFromSourceDirectory(media::GetTestDataPath());
    CHECK(http_test_server->Start());
    GURL gurl = http_test_server->GetURL("/test_key_system_instantiation.html");
    ui_test_utils::NavigateToURL(browser(), gurl);
  }

  // Create a valid JavaScript string for the content type. Format is
  // |mimeType|; codecs="|codec|", where codecs= is omitted if there
  // is no codec.
  static std::string MakeQuotedContentType(std::string mimeType,
                                           std::string codec) {
    std::string contentType(mimeType);
    if (!codec.empty()) {
      contentType.append("; codecs=\"");
      contentType.append(codec);
      contentType.append("\"");
    }
    return "'" + contentType + "'";
  }

  static std::string ExecuteCommand(content::WebContents* contents,
                                    const std::string& command) {
    content::TitleWatcher title_watcher(contents,
                                        base::ASCIIToUTF16(kSuccessResult));
    title_watcher.AlsoWaitForTitle(base::ASCIIToUTF16(kUnsupportedResult));
    title_watcher.AlsoWaitForTitle(base::ASCIIToUTF16(kNoMatchResult));
    title_watcher.AlsoWaitForTitle(base::ASCIIToUTF16(kUnexpectedResult));
    EXPECT_TRUE(content::ExecuteScript(contents, command));
    base::string16 result = title_watcher.WaitAndGetTitle();
    return base::UTF16ToASCII(result);
  }

  std::string AreCodecsSupportedByKeySystem(const std::string& mimeType,
                                            const CodecVector& codecs,
                                            const std::string& keySystem) {
    // Choose the appropriate initDataType for the subtype.
    size_t pos = mimeType.find('/');
    DCHECK(pos > 0);
    std::string subType(mimeType.substr(pos + 1));
    std::string initDataType;
    if (subType == "mp4") {
      initDataType = "cenc";
    } else {
      DCHECK(subType == "webm");
      initDataType = "webm";
    }

    bool isAudio = mimeType.compare(0, 5, "audio") == 0;
    DCHECK(isAudio || mimeType.compare(0, 5, "video") == 0);

    // Create the contentType string based on |codecs|.
    std::string contentTypeList("[");
    if (codecs.empty()) {
      contentTypeList.append(MakeQuotedContentType(mimeType, std::string()));
    } else {
      for (auto codec : codecs) {
        contentTypeList.append(MakeQuotedContentType(mimeType, codec));
        contentTypeList.append(",");
      }
      // Remove trailing comma.
      contentTypeList.erase(contentTypeList.length() - 1);
    }
    contentTypeList.append("]");

    std::string command("checkKeySystemWithMediaMimeType('");
    command.append(keySystem);
    command.append("','");
    command.append(initDataType);
    command.append("',");
    command.append(isAudio ? contentTypeList : "null");
    command.append(",");
    command.append(!isAudio ? contentTypeList : "null");
    command.append(")");

    return ExecuteCommand(browser()->tab_strip_model()->GetActiveWebContents(),
                          command);
  }

 private:
  const CodecVector no_codecs_;
  CodecVector audio_webm_codecs_;
  CodecVector video_webm_codecs_;
  CodecVector audio_mp4_codecs_;
  CodecVector video_mp4_codecs_;
  CodecVector video_mp4_hi10p_codecs_;
  CodecVector invalid_codecs_;
};

// For ClearKey, nothing additional is required.
class EncryptedMediaSupportedTypesClearKeyTest
    : public EncryptedMediaSupportedTypesTest {
};

// For ExternalClearKey tests, ensure that the ClearKey adapter is loaded.
class EncryptedMediaSupportedTypesExternalClearKeyTest
    : public EncryptedMediaSupportedTypesTest {
#if BUILDFLAG(ENABLE_PEPPER_CDMS)
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EncryptedMediaSupportedTypesTest::SetUpCommandLine(command_line);
    RegisterPepperCdm(command_line, kClearKeyCdmBaseDirectory,
                      kClearKeyCdmAdapterFileName, kClearKeyCdmDisplayName,
                      kClearKeyCdmPepperMimeType);
    command_line->AppendSwitchASCII(switches::kEnableFeatures,
                                    media::kExternalClearKeyForTesting.name);
  }
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)
};

// By default, the External Clear Key (ECK) key system is not supported even if
// present. This test case tests this behavior by not enabling
// kExternalClearKeyForTesting.
// Even registering the Pepper CDM where applicable does not enable the CDM.
class EncryptedMediaSupportedTypesExternalClearKeyNotEnabledTest
    : public EncryptedMediaSupportedTypesTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EncryptedMediaSupportedTypesTest::SetUpCommandLine(command_line);
#if BUILDFLAG(ENABLE_PEPPER_CDMS)
    RegisterPepperCdm(command_line, kClearKeyCdmBaseDirectory,
                      kClearKeyCdmAdapterFileName, kClearKeyCdmDisplayName,
                      kClearKeyCdmPepperMimeType);
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)
  }
};

class EncryptedMediaSupportedTypesWidevineTest
    : public EncryptedMediaSupportedTypesTest {
};

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
// Registers ClearKey CDM with the wrong path (filename).
class EncryptedMediaSupportedTypesClearKeyCDMRegisteredWithWrongPathTest
    : public EncryptedMediaSupportedTypesTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EncryptedMediaSupportedTypesTest::SetUpCommandLine(command_line);
    RegisterPepperCdm(command_line, kClearKeyCdmBaseDirectory,
                      "clearkeycdmadapterwrongname.dll",
                      kClearKeyCdmDisplayName, kClearKeyCdmPepperMimeType,
                      false);
    command_line->AppendSwitchASCII(switches::kEnableFeatures,
                                    media::kExternalClearKeyForTesting.name);
  }
};

// Registers Widevine CDM with the wrong path (filename).
class EncryptedMediaSupportedTypesWidevineCDMRegisteredWithWrongPathTest
    : public EncryptedMediaSupportedTypesTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EncryptedMediaSupportedTypesTest::SetUpCommandLine(command_line);
    RegisterPepperCdm(command_line, "WidevineCdm",
                      "widevinecdmadapterwrongname.dll",
                      "Widevine Content Decryption Module",
                      "application/x-ppapi-widevine-cdm", false);
  }
};

#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, Basic) {
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                               video_webm_codecs(), kClearKey));
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kAudioWebMMimeType,
                                               audio_webm_codecs(), kClearKey));
  EXPECT_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kClearKey));
  EXPECT_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, NoCodecs) {
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(kVideoWebMMimeType, no_codecs(),
                                                kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(kAudioWebMMimeType, no_codecs(),
                                                kClearKey));
  EXPECT_NO_MATCH(
      AreCodecsSupportedByKeySystem(kVideoMP4MimeType, no_codecs(), kClearKey));
  EXPECT_NO_MATCH(
      AreCodecsSupportedByKeySystem(kAudioMP4MimeType, no_codecs(), kClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest,
                       InvalidKeySystems) {
  // Case sensitive.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3.ClEaRkEy"));

  // Prefixed Clear Key key system.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "webkit-org.w3.clearkey"));

  // TLDs are not allowed.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org."));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org"));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3."));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3"));

  // Incomplete.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3.clearke"));

  // Extra character.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3.clearkeyz"));

  // There are no child key systems for Clear Key.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.w3.clearkey.foo"));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, Video_WebM) {
  // Valid video types.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kClearKey));

  // Non-video WebM codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_webm_codecs(), kClearKey));

  // Invalid or non-Webm video codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, invalid_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_mp4_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_mp4_codecs(), kClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, Audio_WebM) {
  // Valid audio types.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_webm_codecs(), kClearKey));

  // Non-audio WebM codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_webm_codecs(), kClearKey));

  // Invalid or Non-Webm codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, invalid_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_mp4_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_mp4_codecs(), kClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, Video_MP4) {
  // Valid video types.
  EXPECT_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kClearKey));

  // High 10-bit Profile is supported when using ClearKey if
  // it is supported for clear content on this platform.
#if !defined(MEDIA_DISABLE_FFMPEG) && !defined(OS_ANDROID)
  EXPECT_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_hi10p_codecs(), kClearKey));
#else
  EXPECT_NO_MATCh(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_hi10p_codecs(), kClearKey));
#endif

  // Non-video MP4 codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_mp4_codecs(), kClearKey));

  // Invalid or non-MP4 codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, invalid_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_webm_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_webm_codecs(), kClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesClearKeyTest, Audio_MP4) {
  // Valid audio types.
  EXPECT_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kClearKey));

  // Non-audio MP4 codecs.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_mp4_codecs(), kClearKey));

  // Invalid or non-MP4 codec.
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, invalid_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_webm_codecs(), kClearKey));
  EXPECT_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_webm_codecs(), kClearKey));
}

//
// External Clear Key
//

// When BUILDFLAG(ENABLE_PEPPER_CDMS), this also tests the Pepper CDM check.
IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       Basic) {
  EXPECT_ECK(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kExternalClearKey));
  EXPECT_ECK(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_webm_codecs(), kExternalClearKey));
  EXPECT_ECK_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kExternalClearKey));
  EXPECT_ECK_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kExternalClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       NoCodecs) {
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, no_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, no_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, no_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, no_codecs(), kExternalClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       InvalidKeySystems) {
  // Case sensitive.
  EXPECT_UNKNOWN_KEYSYSTEM(
      AreCodecsSupportedByKeySystem(kVideoWebMMimeType, video_webm_codecs(),
                                    "org.chromium.ExTeRnAlClEaRkEy"));

  // TLDs are not allowed.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org."));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org"));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.chromium"));
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.chromium."));

  // Incomplete.
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), "org.chromium.externalclearke"));

  // Extra character.
  EXPECT_UNKNOWN_KEYSYSTEM(
      AreCodecsSupportedByKeySystem(kVideoWebMMimeType, video_webm_codecs(),
                                    "org.chromium.externalclearkeyz"));

  // There are no child key systems for External Clear Key.
  EXPECT_UNKNOWN_KEYSYSTEM(
      AreCodecsSupportedByKeySystem(kVideoWebMMimeType, video_webm_codecs(),
                                    "org.chromium.externalclearkey.foo"));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       Video_WebM) {
  // Valid video types.
  EXPECT_ECK(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kExternalClearKey));

  // Non-video WebM codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_webm_codecs(), kExternalClearKey));

  // Invalid or non-Webm codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, invalid_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_mp4_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_mp4_codecs(), kExternalClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       Audio_WebM) {
  // Valid audio types.
  EXPECT_ECK(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_webm_codecs(), kExternalClearKey));

  // Non-audio WebM codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_webm_codecs(), kExternalClearKey));

  // Invalid or non-Webm codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, invalid_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_mp4_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_mp4_codecs(), kExternalClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       Video_MP4) {
  // Valid video types.
  EXPECT_ECK_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kExternalClearKey));

  // High 10-bit Profile is not supported when using ExternalClearKey.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_hi10p_codecs(), kExternalClearKey));

  // Non-video MP4 codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_mp4_codecs(), kExternalClearKey));

  // Invalid or non-MP4 codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, invalid_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_webm_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_webm_codecs(), kExternalClearKey));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesExternalClearKeyTest,
                       Audio_MP4) {
  // Valid audio types.
  EXPECT_ECK_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kExternalClearKey));

  // Non-audio MP4 codecs.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_mp4_codecs(), kExternalClearKey));

  // Invalid or Non-MP4 codec.
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, invalid_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_webm_codecs(), kExternalClearKey));
  EXPECT_ECK_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_webm_codecs(), kExternalClearKey));
}

// External Clear Key is disabled by default.
IN_PROC_BROWSER_TEST_F(
    EncryptedMediaSupportedTypesExternalClearKeyNotEnabledTest,
    Basic) {
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kExternalClearKey));

  // Clear Key should still be registered.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                               video_webm_codecs(), kClearKey));
}

//
// Widevine
//

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, Basic) {
  EXPECT_WV_SUCCESS(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kWidevine));
  EXPECT_WV_SUCCESS(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_webm_codecs(), kWidevine));
  EXPECT_WV_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kWidevine));
  EXPECT_WV_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kWidevine));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, NoCodecs) {
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                                   no_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(kAudioWebMMimeType,
                                                   no_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(
      AreCodecsSupportedByKeySystem(kVideoMP4MimeType, no_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(
      AreCodecsSupportedByKeySystem(kAudioMP4MimeType, no_codecs(), kWidevine));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, Video_WebM) {
  // Valid video types.
  EXPECT_WV_SUCCESS(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kWidevine));

  // Non-video WebM codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_webm_codecs(), kWidevine));

  // Invalid or non-Webm codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, invalid_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, audio_mp4_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_mp4_codecs(), kWidevine));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, Audio_WebM) {
  // Valid audio types.
  EXPECT_WV_SUCCESS(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_webm_codecs(), kWidevine));

  // Non-audio WebM codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_webm_codecs(), kWidevine));

  // Invalid or non-Webm codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, invalid_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, audio_mp4_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioWebMMimeType, video_mp4_codecs(), kWidevine));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, Video_MP4) {
  // Valid video types.
  EXPECT_WV_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_codecs(), kWidevine));

  // High 10-bit Profile is not supported when using Widevine.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_mp4_hi10p_codecs(), kWidevine));

  // Non-video MP4 codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_mp4_codecs(), kWidevine));

  // Invalid or non-MP4 codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, invalid_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, audio_webm_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kVideoMP4MimeType, video_webm_codecs(), kWidevine));
}

IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesWidevineTest, Audio_MP4) {
  // Valid audio types.
  EXPECT_WV_PROPRIETARY(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_mp4_codecs(), kWidevine));

  // Non-audio MP4 codecs.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_mp4_codecs(), kWidevine));

  // Invalid or Non-MP4 codec.
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, invalid_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, audio_webm_codecs(), kWidevine));
  EXPECT_WV_NO_MATCH(AreCodecsSupportedByKeySystem(
      kAudioMP4MimeType, video_webm_codecs(), kWidevine));
}

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
// Since this test fixture does not register the CDMs on the command line, the
// check for the CDMs in chrome_key_systems.cc should fail, and they should not
// be registered with KeySystems.
IN_PROC_BROWSER_TEST_F(EncryptedMediaSupportedTypesTest,
                       PepperCDMsNotRegistered) {
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kExternalClearKey));

// This will fail in all builds unless widevine is available.
#if !defined(WIDEVINE_CDM_AVAILABLE)
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kWidevine));
#endif

  // Clear Key should still be registered.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                               video_webm_codecs(), kClearKey));
}

// Since this test fixture does not register the CDMs on the command line, the
// check for the CDMs in chrome_key_systems.cc should fail, and they should not
// be registered with KeySystems.
IN_PROC_BROWSER_TEST_F(
    EncryptedMediaSupportedTypesClearKeyCDMRegisteredWithWrongPathTest,
    PepperCDMsRegisteredButAdapterNotPresent) {
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kExternalClearKey));

  // Clear Key should still be registered.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                               video_webm_codecs(), kClearKey));
}

// This will fail in all builds unless Widevine is available.
#if !defined(WIDEVINE_CDM_AVAILABLE)
IN_PROC_BROWSER_TEST_F(
    EncryptedMediaSupportedTypesWidevineCDMRegisteredWithWrongPathTest,
    PepperCDMsRegisteredButAdapterNotPresent) {
  EXPECT_UNKNOWN_KEYSYSTEM(AreCodecsSupportedByKeySystem(
      kVideoWebMMimeType, video_webm_codecs(), kWidevine));

  // Clear Key should still be registered.
  EXPECT_SUCCESS(AreCodecsSupportedByKeySystem(kVideoWebMMimeType,
                                               video_webm_codecs(), kClearKey));
}
#endif  // !defined(WIDEVINE_CDM_AVAILABLE)
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)

}  // namespace chrome

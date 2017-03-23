// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/background_loader_offliner.h"

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_mock_time_message_loop_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/android/offline_pages/offliner_helper.h"
#include "chrome/browser/net/prediction_options.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/offline_pages/content/background_loader/background_loader_contents_stub.h"
#include "components/offline_pages/core/background/offliner.h"
#include "components/offline_pages/core/background/save_page_request.h"
#include "components/offline_pages/core/stub_offline_page_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/web_contents_tester.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace offline_pages {

namespace {

const int64_t kRequestId = 7;
const GURL kHttpUrl("http://www.tunafish.com");
const GURL kFileUrl("file://salmon.png");
const ClientId kClientId("async_loading", "88");
const bool kUserRequested = true;

// Mock OfflinePageModel for testing the SavePage calls
class MockOfflinePageModel : public StubOfflinePageModel {
 public:
  MockOfflinePageModel() : mock_saving_(false) {}
  ~MockOfflinePageModel() override {}

  void SavePage(const SavePageParams& save_page_params,
                std::unique_ptr<OfflinePageArchiver> archiver,
                const SavePageCallback& callback) override {
    mock_saving_ = true;
    save_page_callback_ = callback;
  }

  void CompleteSavingAsArchiveCreationFailed() {
    DCHECK(mock_saving_);
    mock_saving_ = false;
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(save_page_callback_,
                              SavePageResult::ARCHIVE_CREATION_FAILED, 0));
  }

  void CompleteSavingAsSuccess() {
    DCHECK(mock_saving_);
    mock_saving_ = false;
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(save_page_callback_, SavePageResult::SUCCESS, 123456));
  }

  bool mock_saving() const { return mock_saving_; }

 private:
  bool mock_saving_;
  SavePageCallback save_page_callback_;

  DISALLOW_COPY_AND_ASSIGN(MockOfflinePageModel);
};

}  // namespace

// A BackgroundLoader that we can run tests on.
// Overrides the ResetState so we don't actually try to create any web contents.
// This is a temporary solution to test core BackgroundLoaderOffliner
// functionality until we straighten out assumptions made by RequestCoordinator
// so that the ResetState method is no longer needed.
class TestBackgroundLoaderOffliner : public BackgroundLoaderOffliner {
 public:
  explicit TestBackgroundLoaderOffliner(
      content::BrowserContext* browser_context,
      const OfflinerPolicy* policy,
      OfflinePageModel* offline_page_model);
  ~TestBackgroundLoaderOffliner() override;
  content::WebContentsTester* web_contents_tester() {
    return content::WebContentsTester::For(stub_->web_contents());
  }

  content::WebContents* web_contents() { return stub_->web_contents(); }

  bool is_loading() { return stub_->is_loading(); }

 protected:
  void ResetState() override;

 private:
  background_loader::BackgroundLoaderContentsStub* stub_;
};

TestBackgroundLoaderOffliner::TestBackgroundLoaderOffliner(
    content::BrowserContext* browser_context,
    const OfflinerPolicy* policy,
    OfflinePageModel* offline_page_model)
    : BackgroundLoaderOffliner(browser_context, policy, offline_page_model) {}

TestBackgroundLoaderOffliner::~TestBackgroundLoaderOffliner() {}

void TestBackgroundLoaderOffliner::ResetState() {
  pending_request_.reset();
  stub_ = new background_loader::BackgroundLoaderContentsStub(browser_context_);
  loader_.reset(stub_);
  content::WebContentsObserver::Observe(stub_->web_contents());
}

class BackgroundLoaderOfflinerTest : public testing::Test {
 public:
  BackgroundLoaderOfflinerTest();
  ~BackgroundLoaderOfflinerTest() override;

  void SetUp() override;

  TestBackgroundLoaderOffliner* offliner() const { return offliner_.get(); }
  Offliner::CompletionCallback const completion_callback() {
    return base::Bind(&BackgroundLoaderOfflinerTest::OnCompletion,
                      base::Unretained(this));
  }
  Offliner::ProgressCallback const progress_callback() {
    return base::Bind(&BackgroundLoaderOfflinerTest::OnProgress,
                      base::Unretained(this));
  }
  Offliner::CancelCallback const cancel_callback() {
    return base::Bind(&BackgroundLoaderOfflinerTest::OnCancel,
                      base::Unretained(this));
  }
  Profile* profile() { return &profile_; }
  bool completion_callback_called() { return completion_callback_called_; }
  Offliner::RequestStatus request_status() { return request_status_; }
  bool cancel_callback_called() { return cancel_callback_called_; }
  bool SaveInProgress() const { return model_->mock_saving(); }
  MockOfflinePageModel* model() const { return model_; }
  const base::HistogramTester& histograms() const { return histogram_tester_; }
  int64_t progress() { return progress_; }

  void CompleteLoading() {
    // For some reason, setting loading to True will call DidStopLoading
    // on the observers.
    offliner()->web_contents_tester()->TestSetIsLoading(true);
  }

  void PumpLoop() { base::RunLoop().RunUntilIdle(); }

 private:
  void OnCompletion(const SavePageRequest& request,
                    Offliner::RequestStatus status);
  void OnProgress(const SavePageRequest& request, int64_t bytes);
  void OnCancel(int64_t offline_id);
  content::TestBrowserThreadBundle thread_bundle_;
  TestingProfile profile_;
  std::unique_ptr<TestBackgroundLoaderOffliner> offliner_;
  MockOfflinePageModel* model_;
  bool completion_callback_called_;
  bool cancel_callback_called_;
  int64_t progress_;
  Offliner::RequestStatus request_status_;
  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundLoaderOfflinerTest);
};

BackgroundLoaderOfflinerTest::BackgroundLoaderOfflinerTest()
    : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
      completion_callback_called_(false),
      cancel_callback_called_(false),
      progress_(0LL),
      request_status_(Offliner::RequestStatus::UNKNOWN) {}

BackgroundLoaderOfflinerTest::~BackgroundLoaderOfflinerTest() {}

void BackgroundLoaderOfflinerTest::SetUp() {
  model_ = new MockOfflinePageModel();
  offliner_.reset(new TestBackgroundLoaderOffliner(profile(), nullptr, model_));
  offliner_->SetPageDelayForTest(0L);
}

void BackgroundLoaderOfflinerTest::OnCompletion(
    const SavePageRequest& request,
    Offliner::RequestStatus status) {
  DCHECK(!completion_callback_called_);  // Expect 1 callback per request.
  completion_callback_called_ = true;
  request_status_ = status;
}

void BackgroundLoaderOfflinerTest::OnProgress(const SavePageRequest& request,
                                              int64_t bytes) {
  progress_ = bytes;
}

void BackgroundLoaderOfflinerTest::OnCancel(int64_t offline_id) {
  DCHECK(!cancel_callback_called_);
  cancel_callback_called_ = true;
}

TEST_F(BackgroundLoaderOfflinerTest,
       LoadAndSaveBlockThirdPartyCookiesForCustomTabs) {
  base::Time creation_time = base::Time::Now();
  ClientId custom_tabs_client_id("custom_tabs", "88");
  SavePageRequest request(kRequestId, kHttpUrl, custom_tabs_client_id,
                          creation_time, kUserRequested);

  profile()->GetPrefs()->SetBoolean(prefs::kBlockThirdPartyCookies, true);
  EXPECT_FALSE(offliner()->LoadAndSave(request, completion_callback(),
                                       progress_callback()));
  histograms().ExpectBucketCount(
      "OfflinePages.Background.CctApiDisableStatus",
      static_cast<int>(OfflinePagesCctApiPrerenderAllowedStatus::
                           THIRD_PARTY_COOKIES_DISABLED),
      1);
  histograms().ExpectBucketCount("OfflinePages.Background.CctApiDisableStatus",
                                 0 /* PRERENDER_ALLOWED */, 0);
}

TEST_F(BackgroundLoaderOfflinerTest,
       LoadAndSaveNetworkPredictionDisabledForCustomTabs) {
  base::Time creation_time = base::Time::Now();
  ClientId custom_tabs_client_id("custom_tabs", "88");
  SavePageRequest request(kRequestId, kHttpUrl, custom_tabs_client_id,
                          creation_time, kUserRequested);

  profile()->GetPrefs()->SetInteger(
      prefs::kNetworkPredictionOptions,
      chrome_browser_net::NETWORK_PREDICTION_NEVER);
  EXPECT_FALSE(offliner()->LoadAndSave(request, completion_callback(),
                                       progress_callback()));
  histograms().ExpectBucketCount(
      "OfflinePages.Background.CctApiDisableStatus",
      static_cast<int>(OfflinePagesCctApiPrerenderAllowedStatus::
                           NETWORK_PREDICTION_DISABLED),
      1);
  histograms().ExpectBucketCount(
      "OfflinePages.Background.CctApiDisableStatus",
      static_cast<int>(
          OfflinePagesCctApiPrerenderAllowedStatus::PRERENDER_ALLOWED),
      0);
}

TEST_F(BackgroundLoaderOfflinerTest, LoadAndSaveStartsLoading) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  EXPECT_TRUE(offliner()->is_loading());
  EXPECT_FALSE(SaveInProgress());
  EXPECT_FALSE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::UNKNOWN, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, BytesReportedWillUpdateProgress) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->OnNetworkBytesChanged(5LL);
  EXPECT_EQ(progress(), 5LL);
  offliner()->OnNetworkBytesChanged(10LL);
  EXPECT_EQ(progress(), 15LL);
}

TEST_F(BackgroundLoaderOfflinerTest, CompleteLoadingInitiatesSave) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  CompleteLoading();
  PumpLoop();
  EXPECT_FALSE(completion_callback_called());
  EXPECT_TRUE(SaveInProgress());
  EXPECT_EQ(Offliner::RequestStatus::UNKNOWN, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, CancelWhenLoading) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->Cancel(cancel_callback());
  PumpLoop();
  offliner()->OnNetworkBytesChanged(15LL);
  EXPECT_TRUE(cancel_callback_called());
  EXPECT_FALSE(offliner()->is_loading());  // Offliner reset.
  EXPECT_EQ(progress(), 0LL);  // network bytes not recorded when not busy.
}

TEST_F(BackgroundLoaderOfflinerTest, CancelWhenLoaded) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  CompleteLoading();
  PumpLoop();
  offliner()->Cancel(cancel_callback());
  PumpLoop();

  // Subsequent save callback cause no crash.
  model()->CompleteSavingAsArchiveCreationFailed();
  PumpLoop();
  EXPECT_TRUE(cancel_callback_called());
  EXPECT_FALSE(completion_callback_called());
  EXPECT_FALSE(SaveInProgress());
  EXPECT_FALSE(offliner()->is_loading());  // Offliner reset.
}

TEST_F(BackgroundLoaderOfflinerTest, LoadedButSaveFails) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));

  CompleteLoading();
  PumpLoop();
  model()->CompleteSavingAsArchiveCreationFailed();
  PumpLoop();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::SAVE_FAILED, request_status());
  EXPECT_FALSE(offliner()->is_loading());
  EXPECT_FALSE(SaveInProgress());
}

TEST_F(BackgroundLoaderOfflinerTest, ProgressDoesNotUpdateDuringSave) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->OnNetworkBytesChanged(10LL);
  CompleteLoading();
  PumpLoop();
  offliner()->OnNetworkBytesChanged(15LL);
  EXPECT_EQ(progress(), 10LL);
}

TEST_F(BackgroundLoaderOfflinerTest, LoadAndSaveSuccess) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));

  CompleteLoading();
  PumpLoop();
  model()->CompleteSavingAsSuccess();
  PumpLoop();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::SAVED, request_status());
  EXPECT_FALSE(offliner()->is_loading());
  EXPECT_FALSE(SaveInProgress());
}

TEST_F(BackgroundLoaderOfflinerTest, FailsOnInvalidURL) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kFileUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_FALSE(offliner()->LoadAndSave(request, completion_callback(),
                                       progress_callback()));
}

TEST_F(BackgroundLoaderOfflinerTest, ReturnsOnRenderCrash) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->RenderProcessGone(
      base::TerminationStatus::TERMINATION_STATUS_PROCESS_CRASHED);

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::LOADING_FAILED_NO_NEXT, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, ReturnsOnRenderKilled) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->RenderProcessGone(
      base::TerminationStatus::TERMINATION_STATUS_PROCESS_WAS_KILLED);

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::LOADING_FAILED, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, ReturnsOnWebContentsDestroyed) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  offliner()->WebContentsDestroyed();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::LOADING_FAILED, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, FailsOnErrorPage) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  // Create handle with net error code.
  // Called after calling LoadAndSave so we have web_contents to work with.
  std::unique_ptr<content::NavigationHandle> handle(
      content::NavigationHandle::CreateNavigationHandleForTesting(
          kHttpUrl, offliner()->web_contents()->GetMainFrame(), true,
          net::Error::ERR_NAME_NOT_RESOLVED));
  // NavigationHandle destruction will trigger DidFinishNavigation code.
  handle.reset();
  histograms().ExpectBucketCount(
      "OfflinePages.Background.BackgroundLoadingFailedCode.async_loading",
      105,  // ERR_NAME_NOT_RESOLVED
      1);
  offliner()->DidStopLoading();
  PumpLoop();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::LOADING_FAILED_NO_RETRY, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, NoNextOnInternetDisconnected) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));

  // Create handle with net error code.
  // Called after calling LoadAndSave so we have web_contents to work with.
  std::unique_ptr<content::NavigationHandle> handle(
      content::NavigationHandle::CreateNavigationHandleForTesting(
          kHttpUrl, offliner()->web_contents()->GetMainFrame(), true,
          net::Error::ERR_INTERNET_DISCONNECTED));
  // Call DidFinishNavigation with handle that contains error.
  offliner()->DidFinishNavigation(handle.get());
  // NavigationHandle is always destroyed after finishing navigation.
  handle.reset();
  offliner()->DidStopLoading();
  PumpLoop();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::LOADING_FAILED_NO_NEXT, request_status());
}

TEST_F(BackgroundLoaderOfflinerTest, OnlySavesOnceOnMultipleLoads) {
  base::Time creation_time = base::Time::Now();
  SavePageRequest request(kRequestId, kHttpUrl, kClientId, creation_time,
                          kUserRequested);
  EXPECT_TRUE(offliner()->LoadAndSave(request, completion_callback(),
                                      progress_callback()));
  // First load
  CompleteLoading();
  // Second load
  offliner()->DidStopLoading();
  PumpLoop();
  model()->CompleteSavingAsSuccess();
  PumpLoop();

  EXPECT_TRUE(completion_callback_called());
  EXPECT_EQ(Offliner::RequestStatus::SAVED, request_status());
  EXPECT_FALSE(offliner()->is_loading());
  EXPECT_FALSE(SaveInProgress());
}

}  // namespace offline_pages

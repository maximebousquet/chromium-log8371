// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/download_feedback.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "chrome/browser/safe_browsing/two_phase_uploader.h"
#include "components/safe_browsing/csd.pb.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace safe_browsing {

namespace {

class FakeUploader : public TwoPhaseUploader {
 public:
  FakeUploader(net::URLRequestContextGetter* url_request_context_getter,
               base::TaskRunner* file_task_runner,
               const GURL& base_url,
               const std::string& metadata,
               const base::FilePath& file_path,
               const ProgressCallback& progress_callback,
               const FinishCallback& finish_callback);
  ~FakeUploader() override {}

  void Start() override { start_called_ = true; }

  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter_;
  scoped_refptr<base::TaskRunner> file_task_runner_;
  GURL base_url_;
  std::string metadata_;
  base::FilePath file_path_;
  ProgressCallback progress_callback_;
  FinishCallback finish_callback_;

  bool start_called_;
};

FakeUploader::FakeUploader(
    net::URLRequestContextGetter* url_request_context_getter,
    base::TaskRunner* file_task_runner,
    const GURL& base_url,
    const std::string& metadata,
    const base::FilePath& file_path,
    const ProgressCallback& progress_callback,
    const FinishCallback& finish_callback)
    : url_request_context_getter_(url_request_context_getter),
      file_task_runner_(file_task_runner),
      base_url_(base_url),
      metadata_(metadata),
      file_path_(file_path),
      progress_callback_(progress_callback),
      finish_callback_(finish_callback),
      start_called_(false) {
}

class FakeUploaderFactory : public TwoPhaseUploaderFactory {
 public:
  FakeUploaderFactory() : uploader_(nullptr) {}
  ~FakeUploaderFactory() override {}

  std::unique_ptr<TwoPhaseUploader> CreateTwoPhaseUploader(
      net::URLRequestContextGetter* url_request_context_getter,
      base::TaskRunner* file_task_runner,
      const GURL& base_url,
      const std::string& metadata,
      const base::FilePath& file_path,
      const TwoPhaseUploader::ProgressCallback& progress_callback,
      const TwoPhaseUploader::FinishCallback& finish_callback,
      const net::NetworkTrafficAnnotationTag& traffic_annotation) override;

  FakeUploader* uploader_;
};

std::unique_ptr<TwoPhaseUploader> FakeUploaderFactory::CreateTwoPhaseUploader(
    net::URLRequestContextGetter* url_request_context_getter,
    base::TaskRunner* file_task_runner,
    const GURL& base_url,
    const std::string& metadata,
    const base::FilePath& file_path,
    const TwoPhaseUploader::ProgressCallback& progress_callback,
    const TwoPhaseUploader::FinishCallback& finish_callback,
    const net::NetworkTrafficAnnotationTag& traffic_annotation) {
  EXPECT_FALSE(uploader_);

  uploader_ = new FakeUploader(url_request_context_getter, file_task_runner,
                               base_url, metadata, file_path, progress_callback,
                               finish_callback);
  return base::WrapUnique(uploader_);
}

}  // namespace

class DownloadFeedbackTest : public testing::Test {
 public:
  DownloadFeedbackTest()
      : file_task_runner_(content::BrowserThread::GetTaskRunnerForThread(
            content::BrowserThread::FILE)),
        io_task_runner_(content::BrowserThread::GetTaskRunnerForThread(
            content::BrowserThread::IO)),
        url_request_context_getter_(
            new net::TestURLRequestContextGetter(io_task_runner_)),
        feedback_finish_called_(false) {
    EXPECT_NE(io_task_runner_, file_task_runner_);
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    upload_file_path_ = temp_dir_.GetPath().AppendASCII("test file");
    upload_file_data_ = "data";
    ASSERT_EQ(static_cast<int>(upload_file_data_.size()),
              base::WriteFile(upload_file_path_, upload_file_data_.data(),
                              upload_file_data_.size()));
    TwoPhaseUploader::RegisterFactory(&two_phase_uploader_factory_);
  }

  void TearDown() override { TwoPhaseUploader::RegisterFactory(nullptr); }

  FakeUploader* uploader() const {
    return two_phase_uploader_factory_.uploader_;
  }

  void FinishCallback() {
    EXPECT_FALSE(feedback_finish_called_);
    feedback_finish_called_ = true;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath upload_file_path_;
  std::string upload_file_data_;
  content::TestBrowserThreadBundle thread_bundle_;
  scoped_refptr<base::SingleThreadTaskRunner> file_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  FakeUploaderFactory two_phase_uploader_factory_;
  scoped_refptr<net::TestURLRequestContextGetter> url_request_context_getter_;

  bool feedback_finish_called_;
};

TEST_F(DownloadFeedbackTest, CompleteUpload) {
  ClientDownloadReport expected_report_metadata;
  auto* request = expected_report_metadata.mutable_download_request();
  request->set_url("http://test");
  request->set_length(upload_file_data_.size());
  request->mutable_digests()->set_sha1("hi");
  expected_report_metadata.mutable_download_response()->set_verdict(
      ClientDownloadResponse::DANGEROUS_HOST);
  std::string ping_request(
      expected_report_metadata.download_request().SerializeAsString());
  std::string ping_response(
      expected_report_metadata.download_response().SerializeAsString());

  std::unique_ptr<DownloadFeedback> feedback = DownloadFeedback::Create(
      url_request_context_getter_.get(), file_task_runner_.get(),
      upload_file_path_, ping_request, ping_response);
  EXPECT_FALSE(uploader());

  feedback->Start(base::Bind(&DownloadFeedbackTest::FinishCallback,
                             base::Unretained(this)));
  ASSERT_TRUE(uploader());
  EXPECT_FALSE(feedback_finish_called_);
  EXPECT_TRUE(uploader()->start_called_);

  EXPECT_EQ(url_request_context_getter_,
            uploader()->url_request_context_getter_);
  EXPECT_EQ(file_task_runner_, uploader()->file_task_runner_);
  EXPECT_EQ(upload_file_path_, uploader()->file_path_);
  EXPECT_EQ(expected_report_metadata.SerializeAsString(),
            uploader()->metadata_);
  EXPECT_EQ(DownloadFeedback::kSbFeedbackURL, uploader()->base_url_.spec());

  EXPECT_TRUE(base::PathExists(upload_file_path_));

  EXPECT_FALSE(feedback_finish_called_);
  uploader()->finish_callback_.Run(
      TwoPhaseUploader::STATE_SUCCESS, net::OK, 0, "");
  EXPECT_TRUE(feedback_finish_called_);
  feedback.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(base::PathExists(upload_file_path_));
}

TEST_F(DownloadFeedbackTest, CancelUpload) {
  ClientDownloadReport expected_report_metadata;
  auto* request = expected_report_metadata.mutable_download_request();
  request->set_url("http://test");
  request->set_length(upload_file_data_.size());
  request->mutable_digests()->set_sha1("hi");
  expected_report_metadata.mutable_download_response()->set_verdict(
      ClientDownloadResponse::DANGEROUS_HOST);
  std::string ping_request(
      expected_report_metadata.download_request().SerializeAsString());
  std::string ping_response(
      expected_report_metadata.download_response().SerializeAsString());

  std::unique_ptr<DownloadFeedback> feedback = DownloadFeedback::Create(
      url_request_context_getter_.get(), file_task_runner_.get(),
      upload_file_path_, ping_request, ping_response);
  EXPECT_FALSE(uploader());

  feedback->Start(base::Bind(&DownloadFeedbackTest::FinishCallback,
                             base::Unretained(this)));
  ASSERT_TRUE(uploader());
  EXPECT_FALSE(feedback_finish_called_);
  EXPECT_TRUE(uploader()->start_called_);
  EXPECT_TRUE(base::PathExists(upload_file_path_));

  feedback.reset();
  EXPECT_FALSE(feedback_finish_called_);

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(base::PathExists(upload_file_path_));
}

}  // namespace safe_browsing

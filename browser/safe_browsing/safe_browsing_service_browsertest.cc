// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This test creates a safebrowsing service using test safebrowsing database
// and a test protocol manager. It is used to test logics in safebrowsing
// service.

#include "chrome/browser/safe_browsing/safe_browsing_service.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/field_trial.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/thread_test_helper.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/bookmarks/startup_task_runner_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/safe_browsing/client_side_detection_service.h"
#include "chrome/browser/safe_browsing/local_database_manager.h"
#include "chrome/browser/safe_browsing/protocol_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_database.h"
#include "chrome/browser/safe_browsing/test_safe_browsing_service.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/browser/safe_browsing/v4_test_utils.h"
#include "chrome/browser/subresource_filter/test_ruleset_publisher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/bookmarks/browser/startup_task_runner_service.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safebrowsing_switches.h"
#include "components/safe_browsing_db/database_manager.h"
#include "components/safe_browsing_db/metadata.pb.h"
#include "components/safe_browsing_db/test_database_manager.h"
#include "components/safe_browsing_db/util.h"
#include "components/safe_browsing_db/v4_database.h"
#include "components/safe_browsing_db/v4_feature_list.h"
#include "components/safe_browsing_db/v4_get_hash_protocol_manager.h"
#include "components/safe_browsing_db/v4_protocol_manager_util.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/browser/subresource_filter_features_test_support.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/sha2.h"
#include "net/cookies/cookie_store.h"
#include "net/cookies/cookie_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chromeos/chromeos_switches.h"
#endif

#if !defined(SAFE_BROWSING_DB_LOCAL)
#error This test requires SAFE_BROWSING_DB_LOCAL.
#endif

using content::BrowserThread;
using content::InterstitialPage;
using content::WebContents;
using ::testing::_;
using ::testing::Mock;
using ::testing::StrictMock;

namespace safe_browsing {

namespace {

const char kBlacklistResource[] = "/blacklisted/script.js";
const char kEmptyPage[] = "/empty.html";
const char kMaliciousResource[] = "/malware/script.js";
const char kMalwareFile[] = "/downloads/dangerous/dangerous.exe";
const char kMalwarePage[] = "/safe_browsing/malware.html";
const char kMalwareDelayedLoadsPage[] =
    "/safe_browsing/malware_delayed_loads.html";
const char kMalwareIFrame[] = "/safe_browsing/malware_iframe.html";
const char kMalwareImg[] = "/safe_browsing/malware_image.png";
const char kNeverCompletesPath[] = "/never_completes";
const char kPrefetchMalwarePage[] = "/safe_browsing/prefetch_malware.html";

class NeverCompletingHttpResponse : public net::test_server::HttpResponse {
 public:
  ~NeverCompletingHttpResponse() override {}

  void SendResponse(
      const net::test_server::SendBytesCallback& send,
      const net::test_server::SendCompleteCallback& done) override {
    // Do nothing. |done| is never called.
  }
};

std::unique_ptr<net::test_server::HttpResponse> HandleNeverCompletingRequests(
    const net::test_server::HttpRequest& request) {
  if (!base::StartsWith(request.relative_url, kNeverCompletesPath,
                          base::CompareCase::SENSITIVE))
    return nullptr;
  return base::MakeUnique<NeverCompletingHttpResponse>();
}

void InvokeFullHashCallback(
    SafeBrowsingProtocolManager::FullHashCallback callback,
    const std::vector<SBFullHashResult>& result) {
  callback.Run(result, base::TimeDelta::FromMinutes(45));
}

// Helper function to set up protocol config. It is used to redirects safe
// browsing queries to embeded test server. It needs to be called before
// SafeBrowsingService being created, therefore it is preferred to call this
// function before InProcessBrowserTest::SetUp().
void SetProtocolConfigURLPrefix(const std::string& url_prefix,
                                TestSafeBrowsingServiceFactory* factory) {
  SafeBrowsingProtocolConfig config;
  config.url_prefix = url_prefix;
  // Makes sure the auto update is not triggered. The tests will force the
  // update when needed.
  config.disable_auto_update = true;
  config.client_name = "browser_tests";
  factory->SetTestProtocolConfig(config);
}

class FakeSafeBrowsingUIManager : public TestSafeBrowsingUIManager {
 public:
  void MaybeReportSafeBrowsingHit(
      const safe_browsing::HitReport& hit_report) override {
    EXPECT_FALSE(got_hit_report_);
    got_hit_report_ = true;
    hit_report_ = hit_report;
    SafeBrowsingUIManager::MaybeReportSafeBrowsingHit(hit_report);
  }

  bool got_hit_report_ = false;
  safe_browsing::HitReport hit_report_;

 private:
  ~FakeSafeBrowsingUIManager() override {}
};

// A SafeBrowingDatabase class that allows us to inject the malicious URLs.
class TestSafeBrowsingDatabase : public SafeBrowsingDatabase {
 public:
  TestSafeBrowsingDatabase() {}

  ~TestSafeBrowsingDatabase() override {}

  // Initializes the database with the given filename.
  void Init(const base::FilePath& filename) override {}

  // Deletes the current database and creates a new one.
  bool ResetDatabase() override {
    badurls_.clear();
    urls_by_hash_.clear();
    return true;
  }

  // Called on the IO thread to check if the given URL is safe or not.  If we
  // can synchronously determine that the URL is safe, CheckUrl returns true,
  // otherwise it returns false.
  bool ContainsBrowseUrl(const GURL& url,
                         std::vector<SBPrefix>* prefix_hits,
                         std::vector<SBFullHashResult>* cache_hits) override {
    cache_hits->clear();
    return ContainsUrl(MALWARE, PHISH, std::vector<GURL>(1, url), prefix_hits);
  }

  bool ContainsBrowseHashes(
      const std::vector<SBFullHash>& full_hashes,
      std::vector<SBPrefix>* prefix_hits,
      std::vector<SBFullHashResult>* cache_hits) override {
    cache_hits->clear();
    return ContainsUrl(MALWARE, PHISH, UrlsForHashes(full_hashes), prefix_hits);
  }

  bool ContainsUnwantedSoftwareUrl(
      const GURL& url,
      std::vector<SBPrefix>* prefix_hits,
      std::vector<SBFullHashResult>* cache_hits) override {
    cache_hits->clear();
    return ContainsUrl(UNWANTEDURL, UNWANTEDURL, std::vector<GURL>(1, url),
                       prefix_hits);
  }

  bool ContainsUnwantedSoftwareHashes(
      const std::vector<SBFullHash>& full_hashes,
      std::vector<SBPrefix>* prefix_hits,
      std::vector<SBFullHashResult>* cache_hits) override {
    cache_hits->clear();
    return ContainsUrl(UNWANTEDURL, UNWANTEDURL, UrlsForHashes(full_hashes),
                       prefix_hits);
  }

  bool ContainsDownloadUrlPrefixes(
      const std::vector<SBPrefix>& prefixes,
      std::vector<SBPrefix>* prefix_hits) override {
    bool found = ContainsUrlPrefixes(BINURL, BINURL, prefixes, prefix_hits);
    if (!found)
      return false;
    DCHECK_LE(1U, prefix_hits->size());
    return true;
  }
  bool ContainsCsdWhitelistedUrl(const GURL& url) override { return true; }
  bool ContainsDownloadWhitelistedString(const std::string& str) override {
    return true;
  }
  bool ContainsDownloadWhitelistedUrl(const GURL& url) override { return true; }
  bool ContainsModuleWhitelistedString(const std::string& str) override {
    return true;
  }
  bool ContainsExtensionPrefixes(const std::vector<SBPrefix>& prefixes,
                                 std::vector<SBPrefix>* prefix_hits) override {
    return false;
  }
  bool ContainsMalwareIP(const std::string& ip_address) override {
    return true;
  }
  bool ContainsResourceUrlPrefixes(
      const std::vector<SBPrefix>& prefixes,
      std::vector<SBPrefix>* prefix_hits) override {
    prefix_hits->clear();
    return ContainsUrlPrefixes(RESOURCEBLACKLIST, RESOURCEBLACKLIST,
                               prefixes, prefix_hits);
  }
  bool UpdateStarted(std::vector<SBListChunkRanges>* lists) override {
    ADD_FAILURE() << "Not implemented.";
    return false;
  }
  void InsertChunks(
      const std::string& list_name,
      const std::vector<std::unique_ptr<SBChunkData>>& chunks) override {
    ADD_FAILURE() << "Not implemented.";
  }
  void DeleteChunks(const std::vector<SBChunkDelete>& chunk_deletes) override {
    ADD_FAILURE() << "Not implemented.";
  }
  void UpdateFinished(bool update_succeeded) override {
    ADD_FAILURE() << "Not implemented.";
  }
  void CacheHashResults(const std::vector<SBPrefix>& prefixes,
                        const std::vector<SBFullHashResult>& cache_hits,
                        const base::TimeDelta& cache_lifetime) override {
    // Do nothing for the cache.
  }
  bool IsMalwareIPMatchKillSwitchOn() override { return false; }
  bool IsCsdWhitelistKillSwitchOn() override { return false; }

  // Fill up the database with test URL.
  void AddUrl(const GURL& url,
              const SBFullHashResult& full_hash,
              const std::vector<SBPrefix>& prefix_hits) {
    Hits* hits_for_url = &badurls_[url.spec()];
    hits_for_url->list_ids.push_back(full_hash.list_id);
    hits_for_url->prefix_hits.insert(hits_for_url->prefix_hits.end(),
                                     prefix_hits.begin(), prefix_hits.end());
    bad_prefixes_.insert(
        std::make_pair(full_hash.list_id, full_hash.hash.prefix));
    urls_by_hash_[SBFullHashToString(full_hash.hash)] = url;
  }

 private:
  // Stores |list_ids| of safe browsing lists that match some |prefix_hits|.
  struct Hits {
    std::vector<int> list_ids;
    std::vector<SBPrefix> prefix_hits;
  };

  bool ContainsUrl(int list_id0,
                   int list_id1,
                   const std::vector<GURL>& urls,
                   std::vector<SBPrefix>* prefix_hits) {
    bool hit = false;
    for (const GURL& url : urls) {
      base::hash_map<std::string, Hits>::const_iterator badurls_it =
          badurls_.find(url.spec());

      if (badurls_it == badurls_.end())
        continue;

      std::vector<int> list_ids_for_url = badurls_it->second.list_ids;
      if (base::ContainsValue(list_ids_for_url, list_id0) ||
          base::ContainsValue(list_ids_for_url, list_id1)) {
        prefix_hits->insert(prefix_hits->end(),
                            badurls_it->second.prefix_hits.begin(),
                            badurls_it->second.prefix_hits.end());
        hit = true;
      }
    }
    return hit;
  }

  std::vector<GURL> UrlsForHashes(const std::vector<SBFullHash>& full_hashes) {
    std::vector<GURL> urls;
    for (auto hash : full_hashes) {
      auto url_it = urls_by_hash_.find(SBFullHashToString(hash));
      if (url_it != urls_by_hash_.end()) {
        urls.push_back(url_it->second);
      }
    }
    return urls;
  }

  bool ContainsUrlPrefixes(int list_id0,
                           int list_id1,
                           const std::vector<SBPrefix>& prefixes,
                           std::vector<SBPrefix>* prefix_hits) {
    bool hit = false;
    for (const SBPrefix& prefix : prefixes) {
      for (const std::pair<int, SBPrefix>& entry : bad_prefixes_) {
        if (entry.second == prefix &&
            (entry.first == list_id0 || entry.first == list_id1)) {
          prefix_hits->push_back(prefix);
          hit = true;
        }
      }
    }
    return hit;
  }

  base::hash_map<std::string, Hits> badurls_;
  base::hash_set<std::pair<int, SBPrefix>> bad_prefixes_;
  base::hash_map<std::string, GURL> urls_by_hash_;

  DISALLOW_COPY_AND_ASSIGN(TestSafeBrowsingDatabase);
};

// Factory that creates TestSafeBrowsingDatabase instances.
class TestSafeBrowsingDatabaseFactory : public SafeBrowsingDatabaseFactory {
 public:
  TestSafeBrowsingDatabaseFactory() : db_(nullptr) {}
  ~TestSafeBrowsingDatabaseFactory() override {}

  std::unique_ptr<SafeBrowsingDatabase> CreateSafeBrowsingDatabase(
      const scoped_refptr<base::SequencedTaskRunner>& db_task_runner,
      bool enable_download_protection,
      bool enable_client_side_whitelist,
      bool enable_download_whitelist,
      bool enable_extension_blacklist,
      bool enable_ip_blacklist,
      bool enabled_unwanted_software_list,
      bool enable_module_whitelist) override {
    db_ = new TestSafeBrowsingDatabase();
    return base::WrapUnique(db_);
  }
  TestSafeBrowsingDatabase* GetDb() { return db_; }

 private:
  // Owned by the SafebrowsingService.
  TestSafeBrowsingDatabase* db_;
};

// A TestProtocolManager that could return fixed responses from
// safebrowsing server for testing purpose.
class TestProtocolManager : public SafeBrowsingProtocolManager {
 public:
  TestProtocolManager(SafeBrowsingProtocolManagerDelegate* delegate,
                      net::URLRequestContextGetter* request_context_getter,
                      const SafeBrowsingProtocolConfig& config)
      : SafeBrowsingProtocolManager(delegate, request_context_getter, config) {
    create_count_++;
  }

  ~TestProtocolManager() override { delete_count_++; }

  // This function is called when there is a prefix hit in local safebrowsing
  // database and safebrowsing service issues a get hash request to backends.
  // We return a result from the prefilled full_hashes_ hash_map to simulate
  // server's response. At the same time, latency is added to simulate real
  // life network issues.
  void GetFullHash(const std::vector<SBPrefix>& prefixes,
                   SafeBrowsingProtocolManager::FullHashCallback callback,
                   bool is_download,
                   ExtendedReportingLevel reporting_level) override {
    BrowserThread::PostDelayedTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(InvokeFullHashCallback, callback, full_hashes_), delay_);
  }

  // Prepare the GetFullHash results for the next request.
  void AddGetFullHashResponse(const SBFullHashResult& full_hash_result) {
    full_hashes_.push_back(full_hash_result);
  }

  void IntroduceDelay(const base::TimeDelta& delay) { delay_ = delay; }

  static int create_count() { return create_count_; }

  static int delete_count() { return delete_count_; }

 private:
  std::vector<SBFullHashResult> full_hashes_;
  base::TimeDelta delay_;
  static int create_count_;
  static int delete_count_;
};

// static
int TestProtocolManager::create_count_ = 0;
// static
int TestProtocolManager::delete_count_ = 0;

// Factory that creates TestProtocolManager instances.
class TestSBProtocolManagerFactory : public SBProtocolManagerFactory {
 public:
  TestSBProtocolManagerFactory() : pm_(nullptr) {}
  ~TestSBProtocolManagerFactory() override {}

  std::unique_ptr<SafeBrowsingProtocolManager> CreateProtocolManager(
      SafeBrowsingProtocolManagerDelegate* delegate,
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config) override {
    pm_ = new TestProtocolManager(delegate, request_context_getter, config);
    return base::WrapUnique(pm_);
  }

  TestProtocolManager* GetProtocolManager() { return pm_; }

 private:
  // Owned by the SafeBrowsingService.
  TestProtocolManager* pm_;
};

class MockObserver : public SafeBrowsingUIManager::Observer {
 public:
  MockObserver() {}
  virtual ~MockObserver() {}
  MOCK_METHOD1(OnSafeBrowsingHit,
               void(const security_interstitials::UnsafeResource&));
};

MATCHER_P(IsUnsafeResourceFor, url, "") {
  return (arg.url.spec() == url.spec() &&
          arg.threat_type != SB_THREAT_TYPE_SAFE);
}

class ServiceEnabledHelper : public base::ThreadTestHelper {
 public:
  ServiceEnabledHelper(
      SafeBrowsingService* service,
      bool enabled,
      scoped_refptr<base::SingleThreadTaskRunner> target_thread)
      : base::ThreadTestHelper(target_thread),
        service_(service),
        expected_enabled_(enabled) {}

  void RunTest() override {
    set_test_result(service_->enabled() == expected_enabled_);
  }

 private:
  ~ServiceEnabledHelper() override {}

  scoped_refptr<SafeBrowsingService> service_;
  const bool expected_enabled_;
};

}  // namespace

// Tests the safe browsing blocking page in a browser.
class SafeBrowsingServiceTest : public InProcessBrowserTest {
 public:
  SafeBrowsingServiceTest() {}

  static void GenUrlFullHashResult(const GURL& url,
                                   int list_id,
                                   SBFullHashResult* full_hash) {
    std::string host;
    std::string path;
    V4ProtocolManagerUtil::CanonicalizeUrl(url, &host, &path, nullptr);
    full_hash->hash = SBFullHashForString(host + path);
    full_hash->list_id = list_id;
  }

  static void GenUrlFullHashResultWithMetadata(const GURL& url,
                                               int list_id,
                                               ThreatPatternType threat,
                                               SBFullHashResult* full_hash) {
    GenUrlFullHashResult(url, list_id, full_hash);
    full_hash->metadata.threat_pattern_type = threat;
  }

  void SetUp() override {
    // InProcessBrowserTest::SetUp() instantiates SafebrowsingService.
    // RegisterFactory and plugging test UI manager / protocol config have to
    // be called before SafeBrowsingService is created.
    sb_factory_.reset(new TestSafeBrowsingServiceFactory());
    sb_factory_->SetTestUIManager(new FakeSafeBrowsingUIManager());
    SetProtocolConfigURLPrefix("https://definatelynotarealdomain/safebrowsing",
                               sb_factory_.get());
    SafeBrowsingService::RegisterFactory(sb_factory_.get());
    SafeBrowsingDatabase::RegisterFactory(&db_factory_);
    SafeBrowsingProtocolManager::RegisterFactory(&pm_factory_);
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();

    // Unregister test factories after InProcessBrowserTest::TearDown
    // (which destructs SafeBrowsingService).
    SafeBrowsingDatabase::RegisterFactory(nullptr);
    SafeBrowsingProtocolManager::RegisterFactory(nullptr);
    SafeBrowsingService::RegisterFactory(nullptr);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Makes sure the auto update is not triggered during the test.
    // This test will fill up the database using testing prefixes
    // and urls.
    command_line->AppendSwitch(safe_browsing::switches::kSbDisableAutoUpdate);
    command_line->AppendSwitchASCII(
        ::switches::kEnableFeatures,
        subresource_filter::kSafeBrowsingSubresourceFilter.name);
#if defined(OS_CHROMEOS)
    command_line->AppendSwitch(
        chromeos::switches::kIgnoreUserProfileMappingForTests);
#endif
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    g_browser_process->safe_browsing_service()->ui_manager()->AddObserver(
        &observer_);
  }

  void TearDownOnMainThread() override {
    g_browser_process->safe_browsing_service()->ui_manager()->RemoveObserver(
        &observer_);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void SetUpInProcessBrowserTestFixture() override {
    base::FilePath test_data_dir;
    PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir);
    embedded_test_server()->RegisterRequestHandler(
        base::Bind(&HandleNeverCompletingRequests));
    embedded_test_server()->ServeFilesFromDirectory(test_data_dir);
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // This will setup the "url" prefix in database and prepare protocol manager
  // to respond with |full_hash|, as well as other |full_hash|es previously set
  // via this call, on GetFullHash requests.
  void SetupResponseForUrl(const GURL& url, const SBFullHashResult& full_hash) {
    std::vector<SBPrefix> prefix_hits;
    prefix_hits.push_back(full_hash.hash.prefix);

    // Make sure the full hits is empty unless we need to test the
    // full hash is hit in database's local cache.
    TestSafeBrowsingDatabase* db = db_factory_.GetDb();
    db->AddUrl(url, full_hash, prefix_hits);

    TestProtocolManager* pm = pm_factory_.GetProtocolManager();
    pm->AddGetFullHashResponse(full_hash);
  }

  bool ShowingInterstitialPage() {
    WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    InterstitialPage* interstitial_page = contents->GetInterstitialPage();
    return interstitial_page != nullptr;
  }

  bool WasSubresourceFilterProbeScriptLoaded() {
    bool script_resource_was_loaded = false;
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
        web_contents->GetMainFrame(),
        "domAutomationController.send(!!document.scriptExecuted)",
        &script_resource_was_loaded));
    return script_resource_was_loaded;
  }

  void IntroduceGetHashDelay(const base::TimeDelta& delay) {
    pm_factory_.GetProtocolManager()->IntroduceDelay(delay);
  }

  // TODO(nparker): Remove the need for this by wiring in our own
  // SafeBrowsingDatabaseManager factory and keep a ptr to the subclass.
  // Or add a Get/SetTimeout to sbdbmgr.
  static LocalSafeBrowsingDatabaseManager* LocalDatabaseManagerForService(
      SafeBrowsingService* sb_service) {
    return static_cast<LocalSafeBrowsingDatabaseManager*>(
        sb_service->database_manager().get());
  }

  static base::TimeDelta GetCheckTimeout(SafeBrowsingService* sb_service) {
    return LocalDatabaseManagerForService(sb_service)->check_timeout_;
  }

  static void SetCheckTimeout(SafeBrowsingService* sb_service,
                              const base::TimeDelta& delay) {
    LocalDatabaseManagerForService(sb_service)->check_timeout_ = delay;
  }

  void CreateCSDService() {
#if defined(SAFE_BROWSING_CSD)
    SafeBrowsingService* sb_service =
        g_browser_process->safe_browsing_service();

    // A CSD service should already exist.
    EXPECT_TRUE(sb_service->safe_browsing_detection_service());

    sb_service->services_delegate_->InitializeCsdService(nullptr);
    sb_service->RefreshState();
#endif
  }

  FakeSafeBrowsingUIManager* ui_manager() {
    return static_cast<FakeSafeBrowsingUIManager*>(
        g_browser_process->safe_browsing_service()->ui_manager().get());
  }
  bool got_hit_report() { return ui_manager()->got_hit_report_; }
  const safe_browsing::HitReport& hit_report() {
    return ui_manager()->hit_report_;
  }

 protected:
  StrictMock<MockObserver> observer_;

  // Temporary profile dir for test cases that create a second profile.  This is
  // owned by the SafeBrowsingServiceTest object so that it will not get
  // destructed until after the test Browser has been torn down, since the
  // ImportantFileWriter may still be modifying it after the Profile object has
  // been destroyed.
  base::ScopedTempDir temp_profile_dir_;

  // Waits for pending tasks on the thread |browser_thread| to complete.
  void WaitForThread(
      scoped_refptr<base::SingleThreadTaskRunner> browser_thread) {
    scoped_refptr<base::ThreadTestHelper> thread_helper(
        new base::ThreadTestHelper(browser_thread));
    ASSERT_TRUE(thread_helper->Run());
  }

  // Waits for pending tasks on the IO thread to complete. This is useful
  // to wait for the SafeBrowsingService to finish loading/stopping.
  void WaitForIOThread() {
    scoped_refptr<base::ThreadTestHelper> io_helper(new base::ThreadTestHelper(
        BrowserThread::GetTaskRunnerForThread(BrowserThread::IO).get()));
    ASSERT_TRUE(io_helper->Run());
  }

  // Waits for pending tasks on the IO thread to complete and check if the
  // SafeBrowsingService enabled state matches |enabled|.
  void WaitForIOAndCheckEnabled(SafeBrowsingService* service, bool enabled) {
    scoped_refptr<ServiceEnabledHelper> enabled_helper(new ServiceEnabledHelper(
        service, enabled,
        BrowserThread::GetTaskRunnerForThread(BrowserThread::IO).get()));
    ASSERT_TRUE(enabled_helper->Run());
  }

 protected:
  std::unique_ptr<TestSafeBrowsingServiceFactory> sb_factory_;
  TestSafeBrowsingDatabaseFactory db_factory_;
  TestSBProtocolManagerFactory pm_factory_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingServiceTest);
};

class SafeBrowsingServiceMetadataTest
    : public SafeBrowsingServiceTest,
      public ::testing::WithParamInterface<ThreatPatternType> {
 public:
  SafeBrowsingServiceMetadataTest() {}

  void GenUrlFullHashResultWithMetadata(const GURL& url,
                                        SBFullHashResult* full_hash) {
    GenUrlFullHashResult(url, MALWARE, full_hash);
    // We test with different threat_pattern_types.
    full_hash->metadata.threat_pattern_type = GetParam();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingServiceMetadataTest);
};

IN_PROC_BROWSER_TEST_P(SafeBrowsingServiceMetadataTest, MalwareMainFrame) {
  GURL url = embedded_test_server()->GetURL(kEmptyPage);

  // After adding the url to safebrowsing database and getfullhash result,
  // we should see the interstitial page.
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResultWithMetadata(url, &malware_full_hash);
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(1);
  SetupResponseForUrl(url, malware_full_hash);
  ui_test_utils::NavigateToURL(browser(), url);
  // All types should show the interstitial.
  EXPECT_TRUE(ShowingInterstitialPage());

  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(url, hit_report().malicious_url);
  EXPECT_EQ(url, hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);
  EXPECT_FALSE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_P(SafeBrowsingServiceMetadataTest, MalwareIFrame) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL iframe_url = embedded_test_server()->GetURL(kMalwareIFrame);

  // Add the iframe url as malware and then load the parent page.
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResultWithMetadata(iframe_url, &malware_full_hash);
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(iframe_url)))
      .Times(1);
  SetupResponseForUrl(iframe_url, malware_full_hash);
  ui_test_utils::NavigateToURL(browser(), main_url);
  // All types should show the interstitial.
  EXPECT_TRUE(ShowingInterstitialPage());

  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(iframe_url, hit_report().malicious_url);
  EXPECT_EQ(main_url, hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_P(SafeBrowsingServiceMetadataTest, MalwareImg) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);

  // Add the img url as malware and then load the parent page.
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResultWithMetadata(img_url, &malware_full_hash);
  switch (GetParam()) {
    case ThreatPatternType::NONE:  // Falls through.
    case ThreatPatternType::MALWARE_DISTRIBUTION:
      EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(img_url)))
          .Times(1);
      break;
    case ThreatPatternType::MALWARE_LANDING:
      // No interstitial shown, so no notifications expected.
      break;
    default:
      break;
  }
  SetupResponseForUrl(img_url, malware_full_hash);
  ui_test_utils::NavigateToURL(browser(), main_url);
  // Subresource which is tagged as a landing page should not show an
  // interstitial, the other types should.
  switch (GetParam()) {
    case ThreatPatternType::NONE:  // Falls through.
    case ThreatPatternType::MALWARE_DISTRIBUTION:
      EXPECT_TRUE(ShowingInterstitialPage());
      EXPECT_TRUE(got_hit_report());
      EXPECT_EQ(img_url, hit_report().malicious_url);
      EXPECT_EQ(main_url, hit_report().page_url);
      EXPECT_EQ(GURL(), hit_report().referrer_url);
      EXPECT_TRUE(hit_report().is_subresource);
      break;
    case ThreatPatternType::MALWARE_LANDING:
      EXPECT_FALSE(ShowingInterstitialPage());
      EXPECT_FALSE(got_hit_report());
      break;
    default:
      break;
  }
}

INSTANTIATE_TEST_CASE_P(
    MaybeSetMetadata,
    SafeBrowsingServiceMetadataTest,
    testing::Values(ThreatPatternType::NONE,
                    ThreatPatternType::MALWARE_LANDING,
                    ThreatPatternType::MALWARE_DISTRIBUTION));

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, UnwantedImgIgnored) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);

  // Add the img url as coming from a site serving UwS and then load the parent
  // page.
  SBFullHashResult uws_full_hash;
  GenUrlFullHashResult(img_url, UNWANTEDURL, &uws_full_hash);
  SetupResponseForUrl(img_url, uws_full_hash);

  ui_test_utils::NavigateToURL(browser(), main_url);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, MalwareWithWhitelist) {
  GURL url = embedded_test_server()->GetURL(kEmptyPage);

  // After adding the url to safebrowsing database and getfullhash result,
  // we should see the interstitial page.
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(url, MALWARE, &malware_full_hash);
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(1);
  SetupResponseForUrl(url, malware_full_hash);

  ui_test_utils::NavigateToURL(browser(), url);
  Mock::VerifyAndClearExpectations(&observer_);
  // There should be an InterstitialPage.
  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  InterstitialPage* interstitial_page = contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  // Proceed through it.
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
           &contents->GetController()));
  interstitial_page->Proceed();
  load_stop_observer.Wait();
  EXPECT_FALSE(ShowingInterstitialPage());

  // Navigate to kEmptyPage again -- should hit the whitelist this time.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(0);
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(ShowingInterstitialPage());
}

// This test confirms that prefetches don't themselves get the
// interstitial treatment.
IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, Prefetch) {
  GURL url = embedded_test_server()->GetURL(kPrefetchMalwarePage);
  GURL malware_url = embedded_test_server()->GetURL(kMalwarePage);

  // Even though we have added this uri to the safebrowsing database and
  // getfullhash result, we should not see the interstitial page since the
  // only malware was a prefetch target.
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(malware_url, MALWARE, &malware_full_hash);
  SetupResponseForUrl(malware_url, malware_full_hash);
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // However, when we navigate to the malware page, we should still get
  // the interstitial.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(malware_url)))
      .Times(1);
  ui_test_utils::NavigateToURL(browser(), malware_url);
  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  Mock::VerifyAndClear(&observer_);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, MainFrameHitWithReferrer) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL bad_url = embedded_test_server()->GetURL(kMalwarePage);

  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(bad_url, MALWARE, &malware_full_hash);
  SetupResponseForUrl(bad_url, malware_full_hash);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page, should show interstitial and have first page in
  // referrer.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  chrome::NavigateParams params(browser(), bad_url, ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(bad_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_FALSE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, SubresourceFilterEndToEndTest) {
  subresource_filter::testing::ScopedSubresourceFilterFeatureToggle
      scoped_feature_toggle(
          base::FeatureList::OVERRIDE_ENABLE_FEATURE,
          subresource_filter::kActivationLevelEnabled,
          subresource_filter::kActivationScopeActivationList,
          subresource_filter::kActivationListSocialEngineeringAdsInterstitial);

  subresource_filter::testing::TestRulesetCreator ruleset_creator;
  subresource_filter::testing::TestRulesetPair test_ruleset_pair;
  ruleset_creator.CreateRulesetToDisallowURLsWithPathSuffix(
      "included_script.js", &test_ruleset_pair);
  subresource_filter::testing::TestRulesetPublisher test_ruleset_publisher;
  ASSERT_NO_FATAL_FAILURE(
      test_ruleset_publisher.SetRuleset(test_ruleset_pair.unindexed));

  GURL phishing_url = embedded_test_server()->GetURL(
      "/subresource_filter/frame_with_included_script.html");
  SBFullHashResult malware_full_hash;
  GenUrlFullHashResultWithMetadata(phishing_url, PHISH,
                                   ThreatPatternType::SOCIAL_ENGINEERING_ADS,
                                   &malware_full_hash);
  SetupResponseForUrl(phishing_url, malware_full_hash);

  // Navigation to a phishing page should trigger an interstitial. If the user
  // clicks through it, the page load should proceed, but with subresource
  // filtering activated. This is verified by probing whether `included_script`
  // that is disallowed above indeed fails to load.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(phishing_url)));
  ui_test_utils::NavigateToURL(browser(), phishing_url);
  ASSERT_TRUE(Mock::VerifyAndClearExpectations(&observer_));
  ASSERT_TRUE(got_hit_report());
  content::WaitForInterstitialAttach(web_contents);
  ASSERT_TRUE(ShowingInterstitialPage());

  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &web_contents->GetController()));
  InterstitialPage* interstitial_page = web_contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  interstitial_page->Proceed();
  load_stop_observer.Wait();
  ASSERT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(WasSubresourceFilterProbeScriptLoaded());

  // Navigate to a page that loads the same script, but is not a phishing page.
  // The load should be allowed.
  GURL safe_url = embedded_test_server()->GetURL(
      "/subresource_filter/frame_with_allowed_script.html");
  ui_test_utils::NavigateToURL(browser(), safe_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_TRUE(WasSubresourceFilterProbeScriptLoaded());

  // Navigate to the phishing page again -- should be no interstitial shown, but
  // subresource filtering should still be activated.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(phishing_url)))
      .Times(0);
  ui_test_utils::NavigateToURL(browser(), phishing_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(WasSubresourceFilterProbeScriptLoaded());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameReferrer) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(bad_url, MALWARE, &malware_full_hash);
  SetupResponseForUrl(bad_url, malware_full_hash);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to page which has malware subresource, should show interstitial
  // and have first page in referrer.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameRendererInitiatedSlowLoad) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwareDelayedLoadsPage);
  GURL third_url = embedded_test_server()->GetURL(kNeverCompletesPath);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(bad_url, MALWARE, &malware_full_hash);
  SetupResponseForUrl(bad_url, malware_full_hash);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page. The malware subresources haven't loaded yet, so
  // no interstitial should show yet.
  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
           &contents->GetController()));
  // Run javascript function in the page which starts a timer to load the
  // malware image, and also starts a renderer-initiated top-level navigation to
  // a site that does not respond.  Should show interstitial and have first page
  // in referrer.
  contents->GetMainFrame()->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("navigateAndLoadMalwareImage()"));
  load_stop_observer.Wait();

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  // Report URLs should be for the current page, not the pending load.
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameBrowserInitiatedSlowLoad) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwareDelayedLoadsPage);
  GURL third_url = embedded_test_server()->GetURL(kNeverCompletesPath);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  SBFullHashResult malware_full_hash;
  GenUrlFullHashResult(bad_url, MALWARE, &malware_full_hash);
  SetupResponseForUrl(bad_url, malware_full_hash);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page. The malware subresources haven't loaded yet, so
  // no interstitial should show yet.
  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::RenderFrameHost* rfh = contents->GetMainFrame();
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
           &contents->GetController()));
  // Start a browser initiated top-level navigation to a site that does not
  // respond.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), third_url, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);

  // While the top-level navigation is pending, run javascript
  // function in the page which loads the malware image.
  rfh->ExecuteJavaScriptForTests(base::ASCIIToUTF16("loadMalwareImage()"));

  // Wait for interstitial to show.
  load_stop_observer.Wait();

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  // Report URLs should be for the current page, not the pending load.
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, SubResourceHitOnFreshTab) {
  // Allow popups.
  HostContentSettingsMapFactory::GetForProfile(browser()->profile())
      ->SetDefaultContentSetting(CONTENT_SETTINGS_TYPE_POPUPS,
                                 CONTENT_SETTING_ALLOW);

  // Add |kMalwareImg| to fake safebrowsing db.
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);
  SBFullHashResult img_full_hash;
  GenUrlFullHashResult(img_url, MALWARE, &img_full_hash);
  SetupResponseForUrl(img_url, img_full_hash);

  // Have the current tab open a new tab with window.open().
  WebContents* main_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::RenderFrameHost* main_rfh = main_contents->GetMainFrame();

  content::WebContentsAddedObserver web_contents_added_observer;
  main_rfh->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("w=window.open();"));
  WebContents* new_tab_contents = web_contents_added_observer.GetWebContents();
  content::RenderFrameHost* new_tab_rfh = new_tab_contents->GetMainFrame();
  // A fresh WebContents should not have any NavigationEntries yet. (See
  // https://crbug.com/524208.)
  EXPECT_EQ(nullptr, new_tab_contents->GetController().GetLastCommittedEntry());
  EXPECT_EQ(nullptr, new_tab_contents->GetController().GetPendingEntry());

  // Run javascript in the blank new tab to load the malware image.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(img_url)))
      .Times(1);
  new_tab_rfh->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("var img=new Image();"
                         "img.src=\"" + img_url.spec() + "\";"
                         "document.body.appendChild(img);"));

  // Wait for interstitial to show.
  content::WaitForInterstitialAttach(new_tab_contents);
  Mock::VerifyAndClearExpectations(&observer_);
  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(img_url, hit_report().malicious_url);
  EXPECT_TRUE(hit_report().is_subresource);
  // Page report URLs should be empty, since there is no URL for this page.
  EXPECT_EQ(GURL(), hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);

  // Proceed through it.
  InterstitialPage* interstitial_page = new_tab_contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  interstitial_page->Proceed();

  content::WaitForInterstitialDetach(new_tab_contents);
  EXPECT_FALSE(ShowingInterstitialPage());
}

namespace {

class TestSBClient : public base::RefCountedThreadSafe<TestSBClient>,
                     public SafeBrowsingDatabaseManager::Client {
 public:
  TestSBClient()
      : threat_type_(SB_THREAT_TYPE_SAFE),
        safe_browsing_service_(g_browser_process->safe_browsing_service()) {}

  SBThreatType GetThreatType() const { return threat_type_; }

  std::string GetThreatHash() const { return threat_hash_; }

  void CheckDownloadUrl(const std::vector<GURL>& url_chain) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&TestSBClient::CheckDownloadUrlOnIOThread, this, url_chain));
    content::RunMessageLoop();  // Will stop in OnCheckDownloadUrlResult.
  }

  void CheckBrowseUrl(const GURL& url) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&TestSBClient::CheckBrowseUrlOnIOThread, this, url));
    content::RunMessageLoop();  // Will stop in OnCheckBrowseUrlResult.
  }

  void CheckResourceUrl(const GURL& url) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&TestSBClient::CheckResourceUrlOnIOThread, this, url));
    content::RunMessageLoop();  // Will stop in OnCheckResourceUrlResult.
  }

 private:
  friend class base::RefCountedThreadSafe<TestSBClient>;
  ~TestSBClient() override {}

  void CheckDownloadUrlOnIOThread(const std::vector<GURL>& url_chain) {
    bool synchronous_safe_signal =
        safe_browsing_service_->database_manager()->CheckDownloadUrl(url_chain,
                                                                     this);
    if (synchronous_safe_signal) {
      threat_type_ = SB_THREAT_TYPE_SAFE;
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              base::Bind(&TestSBClient::CheckDone, this));
    }
  }

  void CheckBrowseUrlOnIOThread(const GURL& url) {
    // The async CheckDone() hook will not be called when we have a synchronous
    // safe signal, handle it right away.
    bool synchronous_safe_signal =
        safe_browsing_service_->database_manager()->CheckBrowseUrl(url, this);
    if (synchronous_safe_signal) {
      threat_type_ = SB_THREAT_TYPE_SAFE;
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              base::Bind(&TestSBClient::CheckDone, this));
    }
  }

  void CheckResourceUrlOnIOThread(const GURL& url) {
    bool synchronous_safe_signal =
        safe_browsing_service_->database_manager()->CheckResourceUrl(url, this);
    if (synchronous_safe_signal) {
      threat_type_ = SB_THREAT_TYPE_SAFE;
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              base::Bind(&TestSBClient::CheckDone, this));
    }
  }

  // Called when the result of checking a download URL is known.
  void OnCheckDownloadUrlResult(const std::vector<GURL>& /* url_chain */,
                                SBThreatType threat_type) override {
    threat_type_ = threat_type;
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&TestSBClient::CheckDone, this));
  }

  // Called when the result of checking a browse URL is known.
  void OnCheckBrowseUrlResult(const GURL& /* url */,
                              SBThreatType threat_type,
                              const ThreatMetadata& /* metadata */) override {
    threat_type_ = threat_type;
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&TestSBClient::CheckDone, this));
  }

  // Called when the result of checking a resource URL is known.
  void OnCheckResourceUrlResult(const GURL& /* url */,
                                SBThreatType threat_type,
                                const std::string& threat_hash) override {
    threat_type_ = threat_type;
    threat_hash_ = threat_hash;
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&TestSBClient::CheckDone, this));
  }

  void CheckDone() { base::MessageLoopForUI::current()->QuitWhenIdle(); }

  SBThreatType threat_type_;
  std::string threat_hash_;
  SafeBrowsingService* safe_browsing_service_;

  DISALLOW_COPY_AND_ASSIGN(TestSBClient);
};

}  // namespace

// These tests use SafeBrowsingService::Client to directly interact with
// SafeBrowsingService.
IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, CheckDownloadUrl) {
  GURL badbin_url = embedded_test_server()->GetURL(kMalwareFile);
  std::vector<GURL> badbin_urls(1, badbin_url);

  scoped_refptr<TestSBClient> client(new TestSBClient);
  client->CheckDownloadUrl(badbin_urls);

  // Since badbin_url is not in database, it is considered to be safe.
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

  SBFullHashResult full_hash_result;
  GenUrlFullHashResult(badbin_url, BINURL, &full_hash_result);
  SetupResponseForUrl(badbin_url, full_hash_result);

  client->CheckDownloadUrl(badbin_urls);

  // Now, the badbin_url is not safe since it is added to download database.
  EXPECT_EQ(SB_THREAT_TYPE_BINARY_MALWARE_URL, client->GetThreatType());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, CheckUnwantedSoftwareUrl) {
  const GURL bad_url = embedded_test_server()->GetURL(kMalwareFile);
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    // Since bad_url is not in database, it is considered to be
    // safe.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

    SBFullHashResult full_hash_result;
    GenUrlFullHashResult(bad_url, UNWANTEDURL, &full_hash_result);
    SetupResponseForUrl(bad_url, full_hash_result);

    // Now, the bad_url is not safe since it is added to download
    // database.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_UNWANTED, client->GetThreatType());
  }

  // The unwantedness should survive across multiple clients.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_UNWANTED, client->GetThreatType());
  }

  // An unwanted URL also marked as malware should be flagged as malware.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    SBFullHashResult full_hash_result;
    GenUrlFullHashResult(bad_url, MALWARE, &full_hash_result);
    SetupResponseForUrl(bad_url, full_hash_result);

    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, CheckBrowseUrl) {
  const GURL bad_url = embedded_test_server()->GetURL(kMalwareFile);
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    // Since bad_url is not in database, it is considered to be
    // safe.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

    SBFullHashResult full_hash_result;
    GenUrlFullHashResult(bad_url, MALWARE, &full_hash_result);
    SetupResponseForUrl(bad_url, full_hash_result);

    // Now, the bad_url is not safe since it is added to download
    // database.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }

  // The unwantedness should survive across multiple clients.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }

  // Adding the unwanted state to an existing malware URL should have no impact
  // (i.e. a malware hit should still prevail).
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    SBFullHashResult full_hash_result;
    GenUrlFullHashResult(bad_url, UNWANTEDURL, &full_hash_result);
    SetupResponseForUrl(bad_url, full_hash_result);

    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, CheckDownloadUrlRedirects) {
  GURL original_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL badbin_url = embedded_test_server()->GetURL(kMalwareFile);
  GURL final_url = embedded_test_server()->GetURL(kEmptyPage);
  std::vector<GURL> badbin_urls;
  badbin_urls.push_back(original_url);
  badbin_urls.push_back(badbin_url);
  badbin_urls.push_back(final_url);

  scoped_refptr<TestSBClient> client(new TestSBClient);
  client->CheckDownloadUrl(badbin_urls);

  // Since badbin_url is not in database, it is considered to be safe.
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

  SBFullHashResult full_hash_result;
  GenUrlFullHashResult(badbin_url, BINURL, &full_hash_result);
  SetupResponseForUrl(badbin_url, full_hash_result);

  client->CheckDownloadUrl(badbin_urls);

  // Now, the badbin_url is not safe since it is added to download database.
  EXPECT_EQ(SB_THREAT_TYPE_BINARY_MALWARE_URL, client->GetThreatType());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, CheckResourceUrl) {
  GURL blacklist_resource = embedded_test_server()->GetURL(kBlacklistResource);
  std::string blacklist_resource_hash;
  GURL malware_resource = embedded_test_server()->GetURL(kMaliciousResource);
  std::string malware_resource_hash;

  {
    SBFullHashResult full_hash;
    GenUrlFullHashResult(blacklist_resource, RESOURCEBLACKLIST, &full_hash);
    SetupResponseForUrl(blacklist_resource, full_hash);
    blacklist_resource_hash = std::string(full_hash.hash.full_hash,
                                          full_hash.hash.full_hash + 32);
  }
  {
    SBFullHashResult full_hash;
    GenUrlFullHashResult(malware_resource, MALWARE, &full_hash);
    SetupResponseForUrl(malware_resource, full_hash);
    full_hash.list_id = RESOURCEBLACKLIST;
    SetupResponseForUrl(malware_resource, full_hash);
    malware_resource_hash = std::string(full_hash.hash.full_hash,
                                        full_hash.hash.full_hash + 32);
  }

  scoped_refptr<TestSBClient> client(new TestSBClient);
  client->CheckResourceUrl(blacklist_resource);
  EXPECT_EQ(SB_THREAT_TYPE_BLACKLISTED_RESOURCE, client->GetThreatType());
  EXPECT_EQ(blacklist_resource_hash, client->GetThreatHash());

  // Since we're checking a resource url, we should receive result that it's
  // a blacklisted resource, not a malware.
  client = new TestSBClient;
  client->CheckResourceUrl(malware_resource);
  EXPECT_EQ(SB_THREAT_TYPE_BLACKLISTED_RESOURCE, client->GetThreatType());
  EXPECT_EQ(malware_resource_hash, client->GetThreatHash());

  client->CheckResourceUrl(embedded_test_server()->GetURL(kEmptyPage));
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());
}

#if defined(OS_WIN)
// http://crbug.com/396409
#define MAYBE_CheckDownloadUrlTimedOut DISABLED_CheckDownloadUrlTimedOut
#else
#define MAYBE_CheckDownloadUrlTimedOut CheckDownloadUrlTimedOut
#endif
IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest,
                       MAYBE_CheckDownloadUrlTimedOut) {
  GURL badbin_url = embedded_test_server()->GetURL(kMalwareFile);
  std::vector<GURL> badbin_urls(1, badbin_url);

  scoped_refptr<TestSBClient> client(new TestSBClient);
  SBFullHashResult full_hash_result;
  GenUrlFullHashResult(badbin_url, BINURL, &full_hash_result);
  SetupResponseForUrl(badbin_url, full_hash_result);
  client->CheckDownloadUrl(badbin_urls);

  // badbin_url is not safe since it is added to download database.
  EXPECT_EQ(SB_THREAT_TYPE_BINARY_MALWARE_URL, client->GetThreatType());

  //
  // Now introducing delays and we should hit timeout.
  //
  SafeBrowsingService* sb_service = g_browser_process->safe_browsing_service();
  base::TimeDelta default_urlcheck_timeout = GetCheckTimeout(sb_service);
  IntroduceGetHashDelay(base::TimeDelta::FromSeconds(1));
  SetCheckTimeout(sb_service, base::TimeDelta::FromMilliseconds(1));
  client->CheckDownloadUrl(badbin_urls);

  // There should be a timeout and the hash would be considered as safe.
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

  // Need to set the timeout back to the default value.
  SetCheckTimeout(sb_service, default_urlcheck_timeout);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceTest, StartAndStop) {
  CreateCSDService();
  SafeBrowsingService* sb_service = g_browser_process->safe_browsing_service();
  ClientSideDetectionService* csd_service =
      sb_service->safe_browsing_detection_service();
  PrefService* pref_service = browser()->profile()->GetPrefs();

  ASSERT_TRUE(sb_service);
  ASSERT_TRUE(csd_service);
  ASSERT_TRUE(pref_service);

  EXPECT_TRUE(pref_service->GetBoolean(prefs::kSafeBrowsingEnabled));

  // SBS might still be starting, make sure this doesn't flake.
  EXPECT_TRUE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, true);
  EXPECT_TRUE(csd_service->enabled());

  // Add a new Profile. SBS should keep running.
  ASSERT_TRUE(temp_profile_dir_.CreateUniqueTempDir());
  std::unique_ptr<Profile> profile2(Profile::CreateProfile(
      temp_profile_dir_.GetPath(), nullptr, Profile::CREATE_MODE_SYNCHRONOUS));
  ASSERT_TRUE(profile2);
  StartupTaskRunnerServiceFactory::GetForProfile(profile2.get())->
      StartDeferredTaskRunners();
  PrefService* pref_service2 = profile2->GetPrefs();
  EXPECT_TRUE(pref_service2->GetBoolean(prefs::kSafeBrowsingEnabled));
  // We don't expect the state to have changed, but if it did, wait for it.
  EXPECT_TRUE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, true);
  EXPECT_TRUE(csd_service->enabled());

  // Change one of the prefs. SBS should keep running.
  pref_service->SetBoolean(prefs::kSafeBrowsingEnabled, false);
  EXPECT_TRUE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, true);
  EXPECT_TRUE(csd_service->enabled());

  // Change the other pref. SBS should stop now.
  pref_service2->SetBoolean(prefs::kSafeBrowsingEnabled, false);

// TODO(mattm): Remove this when crbug.com/461493 is fixed.
#if defined(OS_CHROMEOS)
  // On Chrome OS we should disable safe browsing for signin profile.
  EXPECT_TRUE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, true);
  EXPECT_TRUE(csd_service->enabled());
  chromeos::ProfileHelper::GetSigninProfile()
      ->GetOriginalProfile()
      ->GetPrefs()
      ->SetBoolean(prefs::kSafeBrowsingEnabled, false);
  WaitForIOThread();
#endif
  EXPECT_FALSE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, false);
  EXPECT_FALSE(csd_service->enabled());

  // Turn it back on. SBS comes back.
  pref_service2->SetBoolean(prefs::kSafeBrowsingEnabled, true);
  EXPECT_TRUE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, true);
  EXPECT_TRUE(csd_service->enabled());

  // Delete the Profile. SBS stops again.
  pref_service2 = nullptr;
  profile2.reset();
  EXPECT_FALSE(sb_service->enabled_by_prefs());
  WaitForIOAndCheckEnabled(sb_service, false);
  EXPECT_FALSE(csd_service->enabled());
}

class SafeBrowsingServiceShutdownTest : public SafeBrowsingServiceTest {
 public:
  void TearDown() override {
    // Browser should be fully torn down by now, so we can safely check these
    // counters.
    EXPECT_EQ(1, TestProtocolManager::create_count());
    EXPECT_EQ(1, TestProtocolManager::delete_count());

    SafeBrowsingServiceTest::TearDown();
  }

  // An observer that returns back to test code after a new profile is
  // initialized.
  void OnUnblockOnProfileCreation(Profile* profile,
                                  Profile::CreateStatus status) {
    if (status == Profile::CREATE_STATUS_INITIALIZED) {
      profile2_ = profile;
      base::MessageLoop::current()->QuitWhenIdle();
    }
  }

 protected:
  Profile* profile2_;
};

IN_PROC_BROWSER_TEST_F(SafeBrowsingServiceShutdownTest,
                       DontStartAfterShutdown) {
  CreateCSDService();
  SafeBrowsingService* sb_service = g_browser_process->safe_browsing_service();
  ClientSideDetectionService* csd_service =
      sb_service->safe_browsing_detection_service();
  PrefService* pref_service = browser()->profile()->GetPrefs();

  ASSERT_TRUE(sb_service);
  ASSERT_TRUE(csd_service);
  ASSERT_TRUE(pref_service);

  EXPECT_TRUE(pref_service->GetBoolean(prefs::kSafeBrowsingEnabled));

  // SBS might still be starting, make sure this doesn't flake.
  WaitForIOThread();
  EXPECT_EQ(1, TestProtocolManager::create_count());
  EXPECT_EQ(0, TestProtocolManager::delete_count());

  // Create an additional profile.  We need to use the ProfileManager so that
  // the profile will get destroyed in the normal browser shutdown process.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(temp_profile_dir_.CreateUniqueTempDir());
  profile_manager->CreateProfileAsync(
      temp_profile_dir_.GetPath(),
      base::Bind(&SafeBrowsingServiceShutdownTest::OnUnblockOnProfileCreation,
                 base::Unretained(this)),
      base::string16(), std::string(), std::string());

  // Spin to allow profile creation to take place, loop is terminated
  // by OnUnblockOnProfileCreation when the profile is created.
  content::RunMessageLoop();

  PrefService* pref_service2 = profile2_->GetPrefs();
  EXPECT_TRUE(pref_service2->GetBoolean(prefs::kSafeBrowsingEnabled));

  // We don't expect the state to have changed, but if it did, wait for it.
  WaitForIOThread();
  EXPECT_EQ(1, TestProtocolManager::create_count());
  EXPECT_EQ(0, TestProtocolManager::delete_count());

  // End the test, shutting down the browser.
  // SafeBrowsingServiceShutdownTest::TearDown will check the create_count and
  // delete_count again.
}

class SafeBrowsingDatabaseManagerCookieTest : public InProcessBrowserTest {
 public:
  SafeBrowsingDatabaseManagerCookieTest() {}

  void SetUp() override {
    // We need to start the test server to get the host&port in the url.
    embedded_test_server()->RegisterRequestHandler(
        base::Bind(&SafeBrowsingDatabaseManagerCookieTest::HandleRequest));
    ASSERT_TRUE(embedded_test_server()->Start());

    sb_factory_ = base::MakeUnique<TestSafeBrowsingServiceFactory>();
    SetProtocolConfigURLPrefix(
        embedded_test_server()->GetURL("/testpath").spec(), sb_factory_.get());
    SafeBrowsingService::RegisterFactory(sb_factory_.get());

    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();

    SafeBrowsingService::RegisterFactory(nullptr);
  }

  bool SetUpUserDataDirectory() override {
    base::FilePath cookie_path(
        SafeBrowsingService::GetCookieFilePathForTesting());
    EXPECT_FALSE(base::PathExists(cookie_path));

    base::FilePath test_dir;
    if (!PathService::Get(chrome::DIR_TEST_DATA, &test_dir)) {
      EXPECT_TRUE(false);
      return false;
    }

    // Initialize the SafeBrowsing cookies with a pre-created cookie store.  It
    // contains a single cookie, for domain 127.0.0.1, with value a=b, and
    // expires in 2038.
    base::FilePath initial_cookies = test_dir.AppendASCII("safe_browsing")
        .AppendASCII("Safe Browsing Cookies");
    if (!base::CopyFile(initial_cookies, cookie_path)) {
      EXPECT_TRUE(false);
      return false;
    }

    sql::Connection db;
    if (!db.Open(cookie_path)) {
      EXPECT_TRUE(false);
      return false;
    }
    // Ensure the host value in the cookie file matches the test server we will
    // be connecting to.
    sql::Statement smt(
        db.GetUniqueStatement("UPDATE cookies SET host_key = ?"));
    if (!smt.is_valid()) {
      EXPECT_TRUE(false);
      return false;
    }
    if (!smt.BindString(0, embedded_test_server()->base_url().host())) {
      EXPECT_TRUE(false);
      return false;
    }
    if (!smt.Run()) {
      EXPECT_TRUE(false);
      return false;
    }
    return InProcessBrowserTest::SetUpUserDataDirectory();
  }

  void TearDownInProcessBrowserTestFixture() override {
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();

    sql::Connection db;
    base::FilePath cookie_path(
        SafeBrowsingService::GetCookieFilePathForTesting());
    ASSERT_TRUE(db.Open(cookie_path));

    sql::Statement smt(
        db.GetUniqueStatement("SELECT name, value FROM cookies ORDER BY name"));
    ASSERT_TRUE(smt.is_valid());
    ASSERT_TRUE(smt.Step());
    ASSERT_EQ("a", smt.ColumnString(0));
    ASSERT_EQ("b", smt.ColumnString(1));
    ASSERT_TRUE(smt.Step());
    ASSERT_EQ("c", smt.ColumnString(0));
    ASSERT_EQ("d", smt.ColumnString(1));
    EXPECT_FALSE(smt.Step());
  }

  void ForceUpdate() {
    sb_factory_->test_safe_browsing_service()
        ->protocol_manager()
        ->ForceScheduleNextUpdate(base::TimeDelta::FromSeconds(0));
  }

  std::unique_ptr<TestSafeBrowsingServiceFactory> sb_factory_;

 protected:
  static std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    if (!base::StartsWith(request.relative_url, "/testpath/",
                          base::CompareCase::SENSITIVE)) {
      ADD_FAILURE() << "bad path";
      return nullptr;
    }

    auto cookie_it = request.headers.find("Cookie");
    if (cookie_it == request.headers.end()) {
      ADD_FAILURE() << "no cookie header";
      return nullptr;
    }

    net::cookie_util::ParsedRequestCookies req_cookies;
    net::cookie_util::ParseRequestCookieLine(cookie_it->second, &req_cookies);
    if (req_cookies.size() != 1) {
      ADD_FAILURE() << "req_cookies.size() = " << req_cookies.size();
      return nullptr;
    }
    const net::cookie_util::ParsedRequestCookie expected_cookie(
        std::make_pair("a", "b"));
    const net::cookie_util::ParsedRequestCookie& cookie = req_cookies.front();
    if (cookie != expected_cookie) {
      ADD_FAILURE() << "bad cookie " << cookie.first << "=" << cookie.second;
      return nullptr;
    }

    std::unique_ptr<net::test_server::BasicHttpResponse> http_response(
        new net::test_server::BasicHttpResponse());
    http_response->set_content("foo");
    http_response->set_content_type("text/plain");
    http_response->AddCustomHeader(
        "Set-Cookie", "c=d; Expires=Fri, 01 Jan 2038 01:01:01 GMT");
    return std::move(http_response);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingDatabaseManagerCookieTest);
};

// Test that a Local Safe Browsing database update request both sends cookies
// and can save cookies.
IN_PROC_BROWSER_TEST_F(SafeBrowsingDatabaseManagerCookieTest,
                       TestSBUpdateCookies) {
  content::WindowedNotificationObserver observer(
      chrome::NOTIFICATION_SAFE_BROWSING_UPDATE_COMPLETE,
      content::Source<SafeBrowsingDatabaseManager>(
          sb_factory_->test_safe_browsing_service()->database_manager().get()));
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SafeBrowsingDatabaseManagerCookieTest::ForceUpdate,
                 base::Unretained(this)));
  observer.Wait();
}

// Tests the safe browsing blocking page in a browser.
class V4SafeBrowsingServiceTest : public SafeBrowsingServiceTest {
 public:
  V4SafeBrowsingServiceTest() : SafeBrowsingServiceTest() {}

  void SetUp() override {
    sb_factory_ = base::MakeUnique<TestSafeBrowsingServiceFactory>(
        V4FeatureList::V4UsageStatus::V4_ONLY);
    sb_factory_->SetTestUIManager(new FakeSafeBrowsingUIManager());
    SafeBrowsingService::RegisterFactory(sb_factory_.get());

    store_factory_ = new TestV4StoreFactory();
    V4Database::RegisterStoreFactoryForTest(base::WrapUnique(store_factory_));

    v4_db_factory_ = new TestV4DatabaseFactory();
    V4Database::RegisterDatabaseFactoryForTest(
        base::WrapUnique(v4_db_factory_));

    v4_get_hash_factory_ = new TestV4GetHashProtocolManagerFactory();
    V4GetHashProtocolManager::RegisterFactory(
        base::WrapUnique(v4_get_hash_factory_));

    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();

    // Unregister test factories after InProcessBrowserTest::TearDown
    // (which destructs SafeBrowsingService).
    V4GetHashProtocolManager::RegisterFactory(nullptr);
    V4Database::RegisterDatabaseFactoryForTest(nullptr);
    V4Database::RegisterStoreFactoryForTest(nullptr);
    SafeBrowsingService::RegisterFactory(nullptr);
  }

  void MarkUrlForListIdUnexpired(const GURL& bad_url,
                                 const ListIdentifier& list_id,
                                 ThreatPatternType threat_pattern_type) {
    FullHashInfo full_hash_info =
        GetFullHashInfoWithMetadata(bad_url, list_id, threat_pattern_type);
    v4_db_factory_->MarkPrefixAsBad(list_id, full_hash_info.full_hash);
    v4_get_hash_factory_->AddToFullHashCache(full_hash_info);
  }

  // Sets up the prefix database and the full hash cache to match one of the
  // prefixes for the given URL and metadata.
  void MarkUrlForMalwareUnexpired(
      const GURL& bad_url,
      ThreatPatternType threat_pattern_type = ThreatPatternType::NONE) {
    MarkUrlForListIdUnexpired(bad_url, GetUrlMalwareId(), threat_pattern_type);
  }

  // Sets up the prefix database and the full hash cache to match one of the
  // prefixes for the given URL in the UwS store.
  void MarkUrlForUwsUnexpired(const GURL& bad_url) {
    MarkUrlForListIdUnexpired(bad_url, GetUrlUwsId(), ThreatPatternType::NONE);
  }

  // Sets up the prefix database and the full hash cache to match one of the
  // prefixes for the given URL in the phishing store.
  void MarkUrlForPhishingUnexpired(const GURL& bad_url,
                                   ThreatPatternType threat_pattern_type) {
    MarkUrlForListIdUnexpired(bad_url, GetUrlSocEngId(), threat_pattern_type);
  }

  // Sets up the prefix database and the full hash cache to match one of the
  // prefixes for the given URL in the malware binary store.
  void MarkUrlForMalwareBinaryUnexpired(const GURL& bad_url) {
    MarkUrlForListIdUnexpired(bad_url, GetUrlMalBinId(),
                              ThreatPatternType::NONE);
  }

  // Sets up the prefix database and the full hash cache to match one of the
  // prefixes for the given URL in the client incident store.
  void MarkUrlForResourceUnexpired(const GURL& bad_url) {
    MarkUrlForListIdUnexpired(bad_url, GetChromeUrlClientIncidentId(),
                              ThreatPatternType::NONE);
  }

 private:
  // Owned by the V4Database.
  TestV4DatabaseFactory* v4_db_factory_;
  // Owned by the V4GetHashProtocolManager.
  TestV4GetHashProtocolManagerFactory* v4_get_hash_factory_;
  // Owned by the V4Database.
  TestV4StoreFactory* store_factory_;

  DISALLOW_COPY_AND_ASSIGN(V4SafeBrowsingServiceTest);
};

// Ensures that if an image is marked as UwS, the main page doesn't show an
// interstitial.
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, UnwantedImgIgnored) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);

  // Add the img url as coming from a site serving UwS and then load the parent
  // page.
  MarkUrlForUwsUnexpired(img_url);

  ui_test_utils::NavigateToURL(browser(), main_url);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
}

// Proceeding through an interstitial should cause it to get whitelisted for
// that user.
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, MalwareWithWhitelist) {
  GURL url = embedded_test_server()->GetURL(kEmptyPage);

  // After adding the URL to SafeBrowsing database and full hash cache, we
  // should see the interstitial page.
  MarkUrlForMalwareUnexpired(url);
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(1);

  ui_test_utils::NavigateToURL(browser(), url);
  Mock::VerifyAndClearExpectations(&observer_);
  // There should be an InterstitialPage.
  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  InterstitialPage* interstitial_page = contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  // Proceed through it.
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &contents->GetController()));
  interstitial_page->Proceed();
  load_stop_observer.Wait();
  EXPECT_FALSE(ShowingInterstitialPage());

  // Navigate to kEmptyPage again -- should hit the whitelist this time.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(0);
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(ShowingInterstitialPage());
}

// This test confirms that prefetches don't themselves get the interstitial
// treatment.
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, Prefetch) {
  GURL url = embedded_test_server()->GetURL(kPrefetchMalwarePage);
  GURL malware_url = embedded_test_server()->GetURL(kMalwarePage);

  // Even though we have added this URI to the SafeBrowsing database and
  // full hash result, we should not see the interstitial page since the
  // only malware was a prefetch target.
  MarkUrlForMalwareUnexpired(malware_url);

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // However, when we navigate to the malware page, we should still get
  // the interstitial.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(malware_url)))
      .Times(1);
  ui_test_utils::NavigateToURL(browser(), malware_url);
  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  Mock::VerifyAndClear(&observer_);
}

// Ensure that the referrer information is preserved in the hit report.
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, MainFrameHitWithReferrer) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL bad_url = embedded_test_server()->GetURL(kMalwarePage);

  MarkUrlForMalwareUnexpired(bad_url);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page, should show interstitial and have first page in
  // referrer.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  chrome::NavigateParams params(browser(), bad_url, ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(bad_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_FALSE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest,
                       SubresourceFilterEndToEndTest) {
  subresource_filter::testing::ScopedSubresourceFilterFeatureToggle
      scoped_feature_toggle(
          base::FeatureList::OVERRIDE_ENABLE_FEATURE,
          subresource_filter::kActivationLevelEnabled,
          subresource_filter::kActivationScopeActivationList,
          subresource_filter::kActivationListSocialEngineeringAdsInterstitial);

  subresource_filter::testing::TestRulesetCreator ruleset_creator;
  subresource_filter::testing::TestRulesetPair test_ruleset_pair;
  ruleset_creator.CreateRulesetToDisallowURLsWithPathSuffix(
      "included_script.js", &test_ruleset_pair);
  subresource_filter::testing::TestRulesetPublisher test_ruleset_publisher;
  ASSERT_NO_FATAL_FAILURE(
      test_ruleset_publisher.SetRuleset(test_ruleset_pair.unindexed));

  GURL phishing_url = embedded_test_server()->GetURL(
      "/subresource_filter/frame_with_included_script.html");
  MarkUrlForPhishingUnexpired(phishing_url,
                              ThreatPatternType::SOCIAL_ENGINEERING_ADS);

  // Navigation to a phishing page should trigger an interstitial. If the user
  // clicks through it, the page load should proceed, but with subresource
  // filtering activated. This is verified by probing whether `included_script`
  // that is disallowed above indeed fails to load.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(phishing_url)));
  ui_test_utils::NavigateToURL(browser(), phishing_url);
  ASSERT_TRUE(Mock::VerifyAndClearExpectations(&observer_));
  ASSERT_TRUE(got_hit_report());
  content::WaitForInterstitialAttach(web_contents);
  ASSERT_TRUE(ShowingInterstitialPage());

  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &web_contents->GetController()));
  InterstitialPage* interstitial_page = web_contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  interstitial_page->Proceed();
  load_stop_observer.Wait();
  ASSERT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(WasSubresourceFilterProbeScriptLoaded());

  // Navigate to a page that loads the same script, but is not a phishing page.
  // The load should be allowed.
  GURL safe_url = embedded_test_server()->GetURL(
      "/subresource_filter/frame_with_allowed_script.html");
  ui_test_utils::NavigateToURL(browser(), safe_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_TRUE(WasSubresourceFilterProbeScriptLoaded());

  // Navigate to the phishing page again -- should be no interstitial shown, but
  // subresource filtering should still be activated.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(phishing_url)))
      .Times(0);
  ui_test_utils::NavigateToURL(browser(), phishing_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(WasSubresourceFilterProbeScriptLoaded());
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameReferrer) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  MarkUrlForMalwareUnexpired(bad_url);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to page which has malware subresource, should show interstitial
  // and have first page in referrer.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameRendererInitiatedSlowLoad) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwareDelayedLoadsPage);
  GURL third_url = embedded_test_server()->GetURL(kNeverCompletesPath);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  MarkUrlForMalwareUnexpired(bad_url);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page. The malware subresources haven't loaded yet, so
  // no interstitial should show yet.
  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &contents->GetController()));
  // Run javascript function in the page which starts a timer to load the
  // malware image, and also starts a renderer-initiated top-level navigation to
  // a site that does not respond.  Should show interstitial and have first page
  // in referrer.
  contents->GetMainFrame()->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("navigateAndLoadMalwareImage()"));
  load_stop_observer.Wait();

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  // Report URLs should be for the current page, not the pending load.
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest,
                       SubResourceHitWithMainFrameBrowserInitiatedSlowLoad) {
  GURL first_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL second_url = embedded_test_server()->GetURL(kMalwareDelayedLoadsPage);
  GURL third_url = embedded_test_server()->GetURL(kNeverCompletesPath);
  GURL bad_url = embedded_test_server()->GetURL(kMalwareImg);

  MarkUrlForMalwareUnexpired(bad_url);

  // Navigate to first, safe page.
  ui_test_utils::NavigateToURL(browser(), first_url);
  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  // Navigate to malware page. The malware subresources haven't loaded yet, so
  // no interstitial should show yet.
  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  params.referrer.url = first_url;
  ui_test_utils::NavigateToURL(&params);

  EXPECT_FALSE(ShowingInterstitialPage());
  EXPECT_FALSE(got_hit_report());
  Mock::VerifyAndClear(&observer_);

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(bad_url)))
      .Times(1);

  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  content::RenderFrameHost* rfh = contents->GetMainFrame();
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &contents->GetController()));
  // Start a browser initiated top-level navigation to a site that does not
  // respond.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), third_url, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);

  // While the top-level navigation is pending, run javascript
  // function in the page which loads the malware image.
  rfh->ExecuteJavaScriptForTests(base::ASCIIToUTF16("loadMalwareImage()"));

  // Wait for interstitial to show.
  load_stop_observer.Wait();

  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  // Report URLs should be for the current page, not the pending load.
  EXPECT_EQ(bad_url, hit_report().malicious_url);
  EXPECT_EQ(second_url, hit_report().page_url);
  EXPECT_EQ(first_url, hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, SubResourceHitOnFreshTab) {
  // Allow popups.
  HostContentSettingsMapFactory::GetForProfile(browser()->profile())
      ->SetDefaultContentSetting(CONTENT_SETTINGS_TYPE_POPUPS,
                                 CONTENT_SETTING_ALLOW);

  // Add |kMalwareImg| to fake safebrowsing db.
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);
  MarkUrlForMalwareUnexpired(img_url);

  // Have the current tab open a new tab with window.open().
  WebContents* main_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::RenderFrameHost* main_rfh = main_contents->GetMainFrame();

  content::WebContentsAddedObserver web_contents_added_observer;
  main_rfh->ExecuteJavaScriptForTests(base::ASCIIToUTF16("w=window.open();"));
  WebContents* new_tab_contents = web_contents_added_observer.GetWebContents();
  content::RenderFrameHost* new_tab_rfh = new_tab_contents->GetMainFrame();
  // A fresh WebContents should not have any NavigationEntries yet. (See
  // https://crbug.com/524208.)
  EXPECT_EQ(nullptr, new_tab_contents->GetController().GetLastCommittedEntry());
  EXPECT_EQ(nullptr, new_tab_contents->GetController().GetPendingEntry());

  // Run javascript in the blank new tab to load the malware image.
  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(img_url)))
      .Times(1);
  new_tab_rfh->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("var img=new Image();"
                         "img.src=\"" +
                         img_url.spec() + "\";"
                                          "document.body.appendChild(img);"));

  // Wait for interstitial to show.
  content::WaitForInterstitialAttach(new_tab_contents);
  Mock::VerifyAndClearExpectations(&observer_);
  EXPECT_TRUE(ShowingInterstitialPage());
  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(img_url, hit_report().malicious_url);
  EXPECT_TRUE(hit_report().is_subresource);
  // Page report URLs should be empty, since there is no URL for this page.
  EXPECT_EQ(GURL(), hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);

  // Proceed through it.
  InterstitialPage* interstitial_page = new_tab_contents->GetInterstitialPage();
  ASSERT_TRUE(interstitial_page);
  interstitial_page->Proceed();

  content::WaitForInterstitialDetach(new_tab_contents);
  EXPECT_FALSE(ShowingInterstitialPage());
}

///////////////////////////////////////////////////////////////////////////////
// START: These tests use SafeBrowsingService::Client to directly interact with
// SafeBrowsingService.
///////////////////////////////////////////////////////////////////////////////
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, CheckDownloadUrl) {
  GURL badbin_url = embedded_test_server()->GetURL(kMalwareFile);
  std::vector<GURL> badbin_urls(1, badbin_url);

  scoped_refptr<TestSBClient> client(new TestSBClient);
  client->CheckDownloadUrl(badbin_urls);

  // Since badbin_url is not in database, it is considered to be safe.
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

  MarkUrlForMalwareBinaryUnexpired(badbin_url);

  client->CheckDownloadUrl(badbin_urls);

  // Now, the badbin_url is not safe since it is added to download database.
  EXPECT_EQ(SB_THREAT_TYPE_BINARY_MALWARE_URL, client->GetThreatType());
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, CheckUnwantedSoftwareUrl) {
  const GURL bad_url = embedded_test_server()->GetURL(kMalwareFile);
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    // Since bad_url is not in database, it is considered to be
    // safe.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

    MarkUrlForUwsUnexpired(bad_url);

    // Now, the bad_url is not safe since it is added to download
    // database.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_UNWANTED, client->GetThreatType());
  }

  // The unwantedness should survive across multiple clients.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_UNWANTED, client->GetThreatType());
  }

  // An unwanted URL also marked as malware should be flagged as malware.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    MarkUrlForMalwareUnexpired(bad_url);

    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, CheckBrowseUrl) {
  const GURL bad_url = embedded_test_server()->GetURL(kMalwareFile);
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    // Since bad_url is not in database, it is considered to be
    // safe.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

    MarkUrlForMalwareUnexpired(bad_url);

    // Now, the bad_url is not safe since it is added to download
    // database.
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }

  // The unwantedness should survive across multiple clients.
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);
    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }

  // Adding the unwanted state to an existing malware URL should have no impact
  // (i.e. a malware hit should still prevail).
  {
    scoped_refptr<TestSBClient> client(new TestSBClient);

    MarkUrlForUwsUnexpired(bad_url);

    client->CheckBrowseUrl(bad_url);
    EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, client->GetThreatType());
  }
}

IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, CheckDownloadUrlRedirects) {
  GURL original_url = embedded_test_server()->GetURL(kEmptyPage);
  GURL badbin_url = embedded_test_server()->GetURL(kMalwareFile);
  GURL final_url = embedded_test_server()->GetURL(kEmptyPage);
  std::vector<GURL> badbin_urls;
  badbin_urls.push_back(original_url);
  badbin_urls.push_back(badbin_url);
  badbin_urls.push_back(final_url);

  scoped_refptr<TestSBClient> client(new TestSBClient);
  client->CheckDownloadUrl(badbin_urls);

  // Since badbin_url is not in database, it is considered to be safe.
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());

  MarkUrlForMalwareBinaryUnexpired(badbin_url);

  client->CheckDownloadUrl(badbin_urls);

  // Now, the badbin_url is not safe since it is added to download database.
  EXPECT_EQ(SB_THREAT_TYPE_BINARY_MALWARE_URL, client->GetThreatType());
}

#if defined(GOOGLE_CHROME_BUILD)
// This test is only enabled when GOOGLE_CHROME_BUILD is true because the store
// that this test uses is only populated on GOOGLE_CHROME_BUILD builds.
IN_PROC_BROWSER_TEST_F(V4SafeBrowsingServiceTest, CheckResourceUrl) {
  GURL blacklist_url = embedded_test_server()->GetURL(kBlacklistResource);
  GURL malware_url = embedded_test_server()->GetURL(kMaliciousResource);
  std::string blacklist_url_hash, malware_url_hash;

  scoped_refptr<TestSBClient> client(new TestSBClient);
  {
    MarkUrlForResourceUnexpired(blacklist_url);
    blacklist_url_hash = GetFullHash(blacklist_url);

    client->CheckResourceUrl(blacklist_url);
    EXPECT_EQ(SB_THREAT_TYPE_BLACKLISTED_RESOURCE, client->GetThreatType());
    EXPECT_EQ(blacklist_url_hash, client->GetThreatHash());
  }
  {
    MarkUrlForMalwareUnexpired(malware_url);
    MarkUrlForResourceUnexpired(malware_url);
    malware_url_hash = GetFullHash(malware_url);

    // Since we're checking a resource url, we should receive result that it's
    // a blacklisted resource, not a malware.
    client = new TestSBClient;
    client->CheckResourceUrl(malware_url);
    EXPECT_EQ(SB_THREAT_TYPE_BLACKLISTED_RESOURCE, client->GetThreatType());
    EXPECT_EQ(malware_url_hash, client->GetThreatHash());
  }

  client->CheckResourceUrl(embedded_test_server()->GetURL(kEmptyPage));
  EXPECT_EQ(SB_THREAT_TYPE_SAFE, client->GetThreatType());
}
#endif
///////////////////////////////////////////////////////////////////////////////
// END: These tests use SafeBrowsingService::Client to directly interact with
// SafeBrowsingService.
///////////////////////////////////////////////////////////////////////////////

// TODO(vakh): Add test for UnwantedMainFrame.

class V4SafeBrowsingServiceMetadataTest
    : public V4SafeBrowsingServiceTest,
      public ::testing::WithParamInterface<ThreatPatternType> {
 public:
  V4SafeBrowsingServiceMetadataTest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(V4SafeBrowsingServiceMetadataTest);
};

// Irrespective of the threat_type classification, if the main frame URL is
// marked as Malware, an interstitial should be shown.
IN_PROC_BROWSER_TEST_P(V4SafeBrowsingServiceMetadataTest, MalwareMainFrame) {
  GURL url = embedded_test_server()->GetURL(kEmptyPage);
  MarkUrlForMalwareUnexpired(url, GetParam());

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(url))).Times(1);

  ui_test_utils::NavigateToURL(browser(), url);
  // All types should show the interstitial.
  EXPECT_TRUE(ShowingInterstitialPage());

  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(url, hit_report().malicious_url);
  EXPECT_EQ(url, hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);
  EXPECT_FALSE(hit_report().is_subresource);
}

// Irrespective of the threat_type classification, if the iframe URL is marked
// as Malware, an interstitial should be shown.
IN_PROC_BROWSER_TEST_P(V4SafeBrowsingServiceMetadataTest, MalwareIFrame) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL iframe_url = embedded_test_server()->GetURL(kMalwareIFrame);

  // Add the iframe url as malware and then load the parent page.
  MarkUrlForMalwareUnexpired(iframe_url, GetParam());

  EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(iframe_url)))
      .Times(1);

  ui_test_utils::NavigateToURL(browser(), main_url);
  // All types should show the interstitial.
  EXPECT_TRUE(ShowingInterstitialPage());

  EXPECT_TRUE(got_hit_report());
  EXPECT_EQ(iframe_url, hit_report().malicious_url);
  EXPECT_EQ(main_url, hit_report().page_url);
  EXPECT_EQ(GURL(), hit_report().referrer_url);
  EXPECT_TRUE(hit_report().is_subresource);
}

// Depending on the threat_type classification, if an embedded resource is
// marked as Malware, an interstitial may be shown.
IN_PROC_BROWSER_TEST_P(V4SafeBrowsingServiceMetadataTest, MalwareImg) {
  GURL main_url = embedded_test_server()->GetURL(kMalwarePage);
  GURL img_url = embedded_test_server()->GetURL(kMalwareImg);

  // Add the img url as malware and then load the parent page.
  MarkUrlForMalwareUnexpired(img_url, GetParam());

  switch (GetParam()) {
    case ThreatPatternType::NONE:  // Falls through.
    case ThreatPatternType::MALWARE_DISTRIBUTION:
      EXPECT_CALL(observer_, OnSafeBrowsingHit(IsUnsafeResourceFor(img_url)))
          .Times(1);
      break;
    case ThreatPatternType::MALWARE_LANDING:
      // No interstitial shown, so no notifications expected.
      break;
    default:
      break;
  }

  ui_test_utils::NavigateToURL(browser(), main_url);

  // Subresource which is tagged as a landing page should not show an
  // interstitial, the other types should.
  switch (GetParam()) {
    case ThreatPatternType::NONE:  // Falls through.
    case ThreatPatternType::MALWARE_DISTRIBUTION:
      EXPECT_TRUE(ShowingInterstitialPage());
      EXPECT_TRUE(got_hit_report());
      EXPECT_EQ(img_url, hit_report().malicious_url);
      EXPECT_EQ(main_url, hit_report().page_url);
      EXPECT_EQ(GURL(), hit_report().referrer_url);
      EXPECT_TRUE(hit_report().is_subresource);
      break;
    case ThreatPatternType::MALWARE_LANDING:
      EXPECT_FALSE(ShowingInterstitialPage());
      EXPECT_FALSE(got_hit_report());
      break;
    default:
      break;
  }
}

INSTANTIATE_TEST_CASE_P(
    MaybeSetMetadata,
    V4SafeBrowsingServiceMetadataTest,
    testing::Values(ThreatPatternType::NONE,
                    ThreatPatternType::MALWARE_LANDING,
                    ThreatPatternType::MALWARE_DISTRIBUTION));

}  // namespace safe_browsing

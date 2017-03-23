// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/user_policy_manager_factory_chromeos.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/policy/active_directory_policy_manager.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/user_cloud_external_data_manager.h"
#include "chrome/browser/chromeos/policy/user_cloud_policy_manager_chromeos.h"
#include "chrome/browser/chromeos/policy/user_cloud_policy_store_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/install_attributes.h"
#include "chrome/browser/policy/schema_registry_service.h"
#include "chrome/browser/policy/schema_registry_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_external_data_manager.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/configuration_policy_provider.h"
#include "components/policy/policy_constants.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "net/url_request/url_request_context_getter.h"

namespace policy {

namespace {

// Directory under the profile directory where policy-related resources are
// stored, see the following constants for details.
const base::FilePath::CharType kPolicy[] = FILE_PATH_LITERAL("Policy");

// Directory under kPolicy, in the user's profile dir, where policy for
// components is cached.
const base::FilePath::CharType kComponentsDir[] =
    FILE_PATH_LITERAL("Components");

// Directory in which to store external policy data. This is specified relative
// to kPolicy.
const base::FilePath::CharType kPolicyExternalDataDir[] =
    FILE_PATH_LITERAL("External Data");

// Timeout in seconds after which to abandon the initial policy fetch and start
// the session regardless.
const int kInitialPolicyFetchTimeoutSeconds = 10;

}  // namespace

// static
UserPolicyManagerFactoryChromeOS*
UserPolicyManagerFactoryChromeOS::GetInstance() {
  return base::Singleton<UserPolicyManagerFactoryChromeOS>::get();
}

// static
ConfigurationPolicyProvider* UserPolicyManagerFactoryChromeOS::GetForProfile(
    Profile* profile) {
  ConfigurationPolicyProvider* cloud_provider =
      GetInstance()->GetCloudPolicyManagerForProfile(profile);
  if (cloud_provider) {
    return cloud_provider;
  }
  return GetInstance()->GetActiveDirectoryPolicyManagerForProfile(profile);
}

// static
UserCloudPolicyManagerChromeOS*
UserPolicyManagerFactoryChromeOS::GetCloudPolicyManagerForProfile(
    Profile* profile) {
  return GetInstance()->GetCloudPolicyManager(profile);
}

// static
ActiveDirectoryPolicyManager*
UserPolicyManagerFactoryChromeOS::GetActiveDirectoryPolicyManagerForProfile(
    Profile* profile) {
  return GetInstance()->GetActiveDirectoryPolicyManager(profile);
}

// static
std::unique_ptr<ConfigurationPolicyProvider>
UserPolicyManagerFactoryChromeOS::CreateForProfile(
    Profile* profile,
    bool force_immediate_load,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner) {
  return GetInstance()->CreateManagerForProfile(profile, force_immediate_load,
                                                background_task_runner);
}

UserPolicyManagerFactoryChromeOS::UserPolicyManagerFactoryChromeOS()
    : BrowserContextKeyedBaseFactory(
          "UserCloudPolicyManagerChromeOS",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(SchemaRegistryServiceFactory::GetInstance());
}

UserPolicyManagerFactoryChromeOS::~UserPolicyManagerFactoryChromeOS() {}

UserCloudPolicyManagerChromeOS*
UserPolicyManagerFactoryChromeOS::GetCloudPolicyManager(Profile* profile) {
  // Get the manager for the original profile, since the PolicyService is
  // also shared between the incognito Profile and the original Profile.
  const auto it = cloud_managers_.find(profile->GetOriginalProfile());
  return it != cloud_managers_.end() ? it->second : nullptr;
}

ActiveDirectoryPolicyManager*
UserPolicyManagerFactoryChromeOS::GetActiveDirectoryPolicyManager(
    Profile* profile) {
  // Get the manager for the original profile, since the PolicyService is
  // also shared between the incognito Profile and the original Profile.
  const auto it =
      active_directory_managers_.find(profile->GetOriginalProfile());
  return it != active_directory_managers_.end() ? it->second : nullptr;
}

std::unique_ptr<ConfigurationPolicyProvider>
UserPolicyManagerFactoryChromeOS::CreateManagerForProfile(
    Profile* profile,
    bool force_immediate_load,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner) {
  DCHECK(cloud_managers_.find(profile) == cloud_managers_.end());
  DCHECK(active_directory_managers_.find(profile) ==
         active_directory_managers_.end());

  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  // Don't initialize cloud policy for the signin profile.
  if (chromeos::ProfileHelper::IsSigninProfile(profile))
    return {};

  // |user| should never be nullptr except for the signin profile. This object
  // is created as part of the Profile creation, which happens right after
  // sign-in. The just-signed-in User is the active user during that time.
  const user_manager::User* user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile);
  CHECK(user);

  // User policy exists for enterprise accounts only:
  // - For regular cloud-managed users (those who have a GAIA account), a
  //   |UserCloudPolicyManagerChromeOS| is created here.
  // - For Active Directory managed users, an |ActiveDirectoryPolicyManager|
  //   is created.
  // - For device-local accounts, policy is provided by
  //   |DeviceLocalAccountPolicyService|.
  // All other user types do not have user policy.
  const AccountId& account_id = user->GetAccountId();
  if (user->IsSupervised() ||
      BrowserPolicyConnector::IsNonEnterpriseUser(account_id.GetUserEmail())) {
    return {};
  }

  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  bool is_active_directory = false;
  switch (account_id.GetAccountType()) {
    case AccountType::UNKNOWN:
    case AccountType::GOOGLE:
      // TODO(tnagel): Return nullptr for unknown accounts once AccountId
      // migration is finished.
      if (!user->HasGaiaAccount()) {
        return {};
      }
      is_active_directory = false;
      break;
    case AccountType::ACTIVE_DIRECTORY:
      // Ensure install attributes are locked into Active Directory mode before
      // allowing Active Directory policy which is not signed.
      if (!connector->GetInstallAttributes()->IsActiveDirectoryManaged()) {
        return {};
      }
      is_active_directory = true;
      break;
  }

  const bool is_browser_restart =
      command_line->HasSwitch(chromeos::switches::kLoginUser);
  const user_manager::UserManager* const user_manager =
      user_manager::UserManager::Get();

  // We want to block for policy if the session has never been initialized
  // (generally true if the user is new, or if there was a crash before the
  // profile finished initializing). There is code in UserSelectionScreen to
  // force an online signin for uninitialized sessions to help ensure we are
  // able to load policy.
  const bool block_forever_for_policy =
      !user_manager->IsLoggedInAsStub() &&
      !user_manager->GetActiveUser()->profile_ever_initialized();

  const bool wait_for_policy_fetch =
      block_forever_for_policy || !is_browser_restart;

  base::TimeDelta initial_policy_fetch_timeout;
  if (block_forever_for_policy) {
    initial_policy_fetch_timeout = base::TimeDelta::Max();
  } else if (wait_for_policy_fetch) {
    initial_policy_fetch_timeout =
        base::TimeDelta::FromSeconds(kInitialPolicyFetchTimeoutSeconds);
  }

  DeviceManagementService* device_management_service =
      connector->device_management_service();
  if (wait_for_policy_fetch)
    device_management_service->ScheduleInitialization(0);

  base::FilePath profile_dir = profile->GetPath();
  const base::FilePath component_policy_cache_dir =
      profile_dir.Append(kPolicy).Append(kComponentsDir);
  const base::FilePath external_data_dir =
      profile_dir.Append(kPolicy).Append(kPolicyExternalDataDir);
  base::FilePath policy_key_dir;
  CHECK(PathService::Get(chromeos::DIR_USER_POLICY_KEYS, &policy_key_dir));

  std::unique_ptr<UserCloudPolicyStoreChromeOS> store =
      base::MakeUnique<UserCloudPolicyStoreChromeOS>(
          chromeos::DBusThreadManager::Get()->GetCryptohomeClient(),
          chromeos::DBusThreadManager::Get()->GetSessionManagerClient(),
          background_task_runner, account_id, policy_key_dir,
          is_active_directory);

  scoped_refptr<base::SequencedTaskRunner> backend_task_runner =
      content::BrowserThread::GetBlockingPool()->GetSequencedTaskRunner(
          content::BrowserThread::GetBlockingPool()->GetSequenceToken());
  scoped_refptr<base::SequencedTaskRunner> io_task_runner =
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::IO);
  std::unique_ptr<CloudExternalDataManager> external_data_manager(
      new UserCloudExternalDataManager(base::Bind(&GetChromePolicyDetails),
                                       backend_task_runner, io_task_runner,
                                       external_data_dir, store.get()));
  if (force_immediate_load)
    store->LoadImmediately();

  scoped_refptr<base::SequencedTaskRunner> file_task_runner =
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::FILE);

  if (is_active_directory) {
    std::unique_ptr<ActiveDirectoryPolicyManager> manager =
        ActiveDirectoryPolicyManager::CreateForUserPolicy(account_id,
                                                          std::move(store));
    manager->Init(
        SchemaRegistryServiceFactory::GetForContext(profile)->registry());

    active_directory_managers_[profile] = manager.get();
    return std::move(manager);
  } else {
    std::unique_ptr<UserCloudPolicyManagerChromeOS> manager =
        base::MakeUnique<UserCloudPolicyManagerChromeOS>(
            std::move(store), std::move(external_data_manager),
            component_policy_cache_dir, wait_for_policy_fetch,
            initial_policy_fetch_timeout, base::ThreadTaskRunnerHandle::Get(),
            file_task_runner, io_task_runner);

    // TODO(tnagel): Enable whitelist for Active Directory.
    bool wildcard_match = false;
    if (connector->IsEnterpriseManaged() &&
        chromeos::CrosSettings::IsWhitelisted(account_id.GetUserEmail(),
                                              &wildcard_match) &&
        wildcard_match &&
        !connector->IsNonEnterpriseUser(account_id.GetUserEmail())) {
      manager->EnableWildcardLoginCheck(account_id.GetUserEmail());
    }

    manager->Init(
        SchemaRegistryServiceFactory::GetForContext(profile)->registry());
    manager->Connect(g_browser_process->local_state(),
                     device_management_service,
                     g_browser_process->system_request_context());

    cloud_managers_[profile] = manager.get();
    return std::move(manager);
  }
}

void UserPolicyManagerFactoryChromeOS::BrowserContextShutdown(
    content::BrowserContext* context) {
  Profile* profile = static_cast<Profile*>(context);
  if (profile->IsOffTheRecord())
    return;
  UserCloudPolicyManagerChromeOS* cloud_manager =
      GetCloudPolicyManager(profile);
  if (cloud_manager)
    cloud_manager->Shutdown();
  ActiveDirectoryPolicyManager* active_directory_manager =
      GetActiveDirectoryPolicyManager(profile);
  if (active_directory_manager)
    active_directory_manager->Shutdown();
}

void UserPolicyManagerFactoryChromeOS::BrowserContextDestroyed(
    content::BrowserContext* context) {
  Profile* profile = static_cast<Profile*>(context);
  cloud_managers_.erase(profile);
  active_directory_managers_.erase(profile);
  BrowserContextKeyedBaseFactory::BrowserContextDestroyed(context);
}

void UserPolicyManagerFactoryChromeOS::SetEmptyTestingFactory(
    content::BrowserContext* context) {}

bool UserPolicyManagerFactoryChromeOS::HasTestingFactory(
    content::BrowserContext* context) {
  return false;
}

void UserPolicyManagerFactoryChromeOS::CreateServiceNow(
    content::BrowserContext* context) {}

}  // namespace policy

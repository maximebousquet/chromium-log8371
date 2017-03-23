// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/settings/session_manager_operation.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/stl_util.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/policy/proto/chrome_device_policy.pb.h"
#include "chrome/browser/net/nss_context.h"
#include "components/ownership/owner_key_util.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/rsa_private_key.h"
#include "crypto/signature_creator.h"

using ownership::OwnerKeyUtil;
using ownership::PublicKey;

namespace em = enterprise_management;

namespace chromeos {

SessionManagerOperation::SessionManagerOperation(const Callback& callback)
    : callback_(callback), weak_factory_(this) {}

SessionManagerOperation::~SessionManagerOperation() {}

void SessionManagerOperation::Start(
    SessionManagerClient* session_manager_client,
    scoped_refptr<OwnerKeyUtil> owner_key_util,
    scoped_refptr<PublicKey> public_key) {
  session_manager_client_ = session_manager_client;
  owner_key_util_ = owner_key_util;
  public_key_ = public_key;
  Run();
}

void SessionManagerOperation::RestartLoad(bool key_changed) {
  if (key_changed)
    public_key_ = nullptr;

  if (!is_loading_)
    return;

  // Abort previous load operations.
  weak_factory_.InvalidateWeakPtrs();
  // Mark as not loading to start loading again.
  is_loading_ = false;
  StartLoading();
}

void SessionManagerOperation::StartLoading() {
  if (is_loading_)
    return;
  is_loading_ = true;
  if (cloud_validations_) {
    EnsurePublicKey(base::Bind(&SessionManagerOperation::RetrieveDeviceSettings,
                               weak_factory_.GetWeakPtr()));
  } else {
    RetrieveDeviceSettings();
  }
}

void SessionManagerOperation::LoadImmediately() {
  StorePublicKey(
      base::Bind(&SessionManagerOperation::BlockingRetrieveDeviceSettings,
                 weak_factory_.GetWeakPtr()),
      LoadPublicKey(owner_key_util_, public_key_));
}

void SessionManagerOperation::ReportResult(
    DeviceSettingsService::Status status) {
  callback_.Run(this, status);
}

void SessionManagerOperation::EnsurePublicKey(const base::Closure& callback) {
  if (force_key_load_ || !public_key_ || !public_key_->is_loaded()) {
    scoped_refptr<base::TaskRunner> task_runner =
        content::BrowserThread::GetBlockingPool()
            ->GetTaskRunnerWithShutdownBehavior(
                base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
    base::PostTaskAndReplyWithResult(
        task_runner.get(), FROM_HERE,
        base::Bind(&SessionManagerOperation::LoadPublicKey, owner_key_util_,
                   force_key_load_ ? nullptr : public_key_),
        base::Bind(&SessionManagerOperation::StorePublicKey,
                   weak_factory_.GetWeakPtr(), callback));
  } else {
    callback.Run();
  }
}

// static
scoped_refptr<PublicKey> SessionManagerOperation::LoadPublicKey(
    scoped_refptr<OwnerKeyUtil> util,
    scoped_refptr<PublicKey> current_key) {
  scoped_refptr<PublicKey> public_key(new PublicKey());

  // Keep already-existing public key.
  if (current_key && current_key->is_loaded()) {
    public_key->data() = current_key->data();
  }
  if (!public_key->is_loaded() && util->IsPublicKeyPresent()) {
    if (!util->ImportPublicKey(&public_key->data()))
      LOG(ERROR) << "Failed to load public owner key.";
  }

  return public_key;
}

void SessionManagerOperation::StorePublicKey(const base::Closure& callback,
                                             scoped_refptr<PublicKey> new_key) {
  force_key_load_ = false;
  public_key_ = new_key;

  if (!public_key_ || !public_key_->is_loaded()) {
    ReportResult(DeviceSettingsService::STORE_KEY_UNAVAILABLE);
    return;
  }

  callback.Run();
}

void SessionManagerOperation::RetrieveDeviceSettings() {
  session_manager_client()->RetrieveDevicePolicy(
      base::Bind(&SessionManagerOperation::ValidateDeviceSettings,
                 weak_factory_.GetWeakPtr()));
}

void SessionManagerOperation::BlockingRetrieveDeviceSettings() {
  ValidateDeviceSettings(
      session_manager_client()->BlockingRetrieveDevicePolicy());
}

void SessionManagerOperation::ValidateDeviceSettings(
    const std::string& policy_blob) {
  std::unique_ptr<em::PolicyFetchResponse> policy(
      new em::PolicyFetchResponse());
  if (policy_blob.empty()) {
    ReportResult(DeviceSettingsService::STORE_NO_POLICY);
    return;
  }

  if (!policy->ParseFromString(policy_blob) ||
      !policy->IsInitialized()) {
    ReportResult(DeviceSettingsService::STORE_INVALID_POLICY);
    return;
  }

  base::SequencedWorkerPool* pool =
      content::BrowserThread::GetBlockingPool();
  scoped_refptr<base::SequencedTaskRunner> background_task_runner =
      pool->GetSequencedTaskRunnerWithShutdownBehavior(
          pool->GetSequenceToken(),
          base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);

  std::unique_ptr<policy::DeviceCloudPolicyValidator> validator =
      base::WrapUnique<policy::DeviceCloudPolicyValidator>(
          policy::DeviceCloudPolicyValidator::Create(std::move(policy),
                                                     background_task_runner));

  if (cloud_validations_) {
    // Policy auto-generated by session manager doesn't include a timestamp, so
    // the timestamp shouldn't be verified in that case.
    //
    // Additionally, offline devices can get their clock set backwards in time
    // under some hardware conditions; checking the timestamp now could likely
    // find a value in the future, and prevent the user from signing-in or
    // starting guest mode. Tlsdate will eventually fix the clock when the
    // device is back online, but the network configuration may come from device
    // ONC.
    //
    // To prevent all of these issues the timestamp is just not verified when
    // loading the device policy from session manager. Note that the timestamp
    // is still verified during enrollment and when a new policy is fetched from
    // the server.
    //
    // The two *_NOT_REQUIRED options are necessary because both the DM token
    // and the device id are empty for a user logging in on an actual Chrome OS
    // device that is not enterprise-managed. Note for devs: The strings are not
    // empty when you test Chrome with target_os = "chromeos" on Linux!
    validator->ValidateAgainstCurrentPolicy(
        policy_data_.get(),
        policy::CloudPolicyValidatorBase::TIMESTAMP_NOT_VALIDATED,
        policy::CloudPolicyValidatorBase::DM_TOKEN_NOT_REQUIRED,
        policy::CloudPolicyValidatorBase::DEVICE_ID_NOT_REQUIRED);

    // We don't check the DMServer verification key below, because the signing
    // key is validated when it is installed.
    validator->ValidateSignature(public_key_->as_string());
  }

  validator->ValidatePolicyType(policy::dm_protocol::kChromeDevicePolicyType);
  validator->ValidatePayload();
  if (force_immediate_load_) {
    validator->RunValidation();
    ReportValidatorStatus(validator.get());
  } else {
    // The Validator will delete itself once validation is complete.
    validator.release()->StartValidation(
        base::Bind(&SessionManagerOperation::ReportValidatorStatus,
                   weak_factory_.GetWeakPtr()));
  }
}

void SessionManagerOperation::ReportValidatorStatus(
    policy::DeviceCloudPolicyValidator* validator) {
  DeviceSettingsService::Status status =
      DeviceSettingsService::STORE_VALIDATION_ERROR;
  if (validator->success()) {
    status = DeviceSettingsService::STORE_SUCCESS;
    policy_data_ = std::move(validator->policy_data());
    device_settings_ = std::move(validator->payload());
  } else {
    LOG(ERROR) << "Policy validation failed: " << validator->status();

    // Those are mostly caused by RTC loss and are recoverable.
    if (validator->status() ==
        policy::DeviceCloudPolicyValidator::VALIDATION_BAD_TIMESTAMP) {
      status = DeviceSettingsService::STORE_TEMP_VALIDATION_ERROR;
    }
  }

  ReportResult(status);
}

LoadSettingsOperation::LoadSettingsOperation(bool force_key_load,
                                             bool cloud_validations,
                                             bool force_immediate_load,
                                             const Callback& callback)
    : SessionManagerOperation(callback) {
  force_key_load_ = force_key_load;
  cloud_validations_ = cloud_validations;
  force_immediate_load_ = force_immediate_load;
}

LoadSettingsOperation::~LoadSettingsOperation() {}

void LoadSettingsOperation::Run() {
  if (force_immediate_load_)
    LoadImmediately();
  else
    StartLoading();
}

StoreSettingsOperation::StoreSettingsOperation(
    const Callback& callback,
    std::unique_ptr<em::PolicyFetchResponse> policy)
    : SessionManagerOperation(callback),
      policy_(std::move(policy)),
      weak_factory_(this) {
  if (policy_->has_new_public_key())
    force_key_load_ = true;
}

StoreSettingsOperation::~StoreSettingsOperation() {}

void StoreSettingsOperation::Run() {
  session_manager_client()->StoreDevicePolicy(
      policy_->SerializeAsString(),
      base::Bind(&StoreSettingsOperation::HandleStoreResult,
                 weak_factory_.GetWeakPtr()));
}

void StoreSettingsOperation::HandleStoreResult(bool success) {
  if (!success)
    ReportResult(DeviceSettingsService::STORE_OPERATION_FAILED);
  else
    StartLoading();
}

}  // namespace chromeos

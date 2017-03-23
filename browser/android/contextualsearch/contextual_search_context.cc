// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrome/browser/android/contextualsearch/contextual_search_context.h>

#include "base/android/jni_string.h"
#include "content/public/browser/browser_thread.h"

#include "jni/ContextualSearchContext_jni.h"

ContextualSearchContext::ContextualSearchContext(JNIEnv* env, jobject obj)
    : can_resolve(false),
      can_send_base_page_url(false),
      selected_text(std::string()),
      home_country(std::string()),
      base_page_url(GURL()),
      surrounding_text(base::string16()),
      start_offset(0),
      end_offset(0) {
  java_object_.Reset(env, obj);
}

ContextualSearchContext::ContextualSearchContext(
    const std::string& selected_text,
    const std::string& home_country,
    const GURL& page_url,
    const std::string& encoding)
    : selected_text(selected_text),
      home_country(home_country),
      base_page_url(page_url),
      base_page_encoding(encoding) {
  java_object_ = nullptr;
}

ContextualSearchContext::~ContextualSearchContext() {
}

// static
ContextualSearchContext*
ContextualSearchContext::FromJavaContextualSearchContext(
    const base::android::JavaRef<jobject>& j_contextual_search_context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (j_contextual_search_context.is_null())
    return NULL;

  ContextualSearchContext* contextual_search_context =
      reinterpret_cast<ContextualSearchContext*>(
          Java_ContextualSearchContext_getNativePointer(
              base::android::AttachCurrentThread(),
              j_contextual_search_context));
  return contextual_search_context;
}

void ContextualSearchContext::SetResolveProperties(
    JNIEnv* env,
    jobject obj,
    const base::android::JavaParamRef<jstring>& j_selection,
    const base::android::JavaParamRef<jstring>& j_home_country,
    jboolean j_may_send_base_page_url) {
  can_resolve = true;
  selected_text = base::android::ConvertJavaStringToUTF8(env, j_selection);
  home_country = base::android::ConvertJavaStringToUTF8(env, j_home_country);
  can_send_base_page_url = j_may_send_base_page_url;
}

// Accessors

bool ContextualSearchContext::CanResolve() const {
  return can_resolve;
}

bool ContextualSearchContext::CanSendBasePageUrl() const {
  return can_send_base_page_url;
}

const GURL ContextualSearchContext::GetBasePageUrl() const {
  return base_page_url;
}

void ContextualSearchContext::SetBasePageUrl(const GURL& base_page_url) {
  this->base_page_url = base_page_url;
}

const std::string ContextualSearchContext::GetBasePageEncoding() const {
  return base_page_encoding;
}

void ContextualSearchContext::SetBasePageEncoding(
    const std::string& base_page_encoding) {
  this->base_page_encoding = base_page_encoding;
}

const std::string ContextualSearchContext::GetHomeCountry() const {
  return home_country;
}

void ContextualSearchContext::SetSelectionSurroundings(
    int start_offset,
    int end_offset,
    const base::string16& surrounding_text) {
  this->start_offset = start_offset;
  this->end_offset = end_offset;
  this->surrounding_text = surrounding_text;
}

const std::string ContextualSearchContext::GetOriginalSelectedText() const {
  return selected_text;
}

const base::string16 ContextualSearchContext::GetSurroundingText() const {
  return surrounding_text;
}

int ContextualSearchContext::GetStartOffset() const {
  return start_offset;
}

int ContextualSearchContext::GetEndOffset() const {
  return end_offset;
}

// Java wrapper boilerplate

void ContextualSearchContext::Destroy(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  delete this;
}

bool RegisterContextualSearchContext(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

jlong Init(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj) {
  ContextualSearchContext* context = new ContextualSearchContext(env, obj);
  return reinterpret_cast<intptr_t>(context);
}

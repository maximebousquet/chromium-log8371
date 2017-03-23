// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_ROUTER_DISCOVERY_MEDIA_SINK_INTERNAL_H_
#define CHROME_BROWSER_MEDIA_ROUTER_DISCOVERY_MEDIA_SINK_INTERNAL_H_

#include <utility>

#include "base/memory/manual_constructor.h"
#include "chrome/browser/media/router/media_sink.h"
#include "net/base/ip_address.h"
#include "url/gurl.h"

namespace media_router {

// Extra data for DIAL media sink.
struct DialSinkExtraData {
  net::IPAddress ip_address;

  // Model name of the sink.
  std::string model_name;

  // The base URL used for DIAL operations.
  GURL app_url;

  DialSinkExtraData();
  DialSinkExtraData(const DialSinkExtraData& other);
  ~DialSinkExtraData();

  bool operator==(const DialSinkExtraData& other) const;
};

// Extra data for Cast media sink.
struct CastSinkExtraData {
  net::IPAddress ip_address;

  // Model name of the sink.
  std::string model_name;

  // A bit vector representing the capabilities of the sink. The values are
  // defined in media_router.mojom.
  uint8_t capabilities = 0;

  // ID of Cast channel opened for the sink. The caller must set this value to a
  // valid cast_channel_id. The cast_channel_id may change over time as the
  // browser reconnects to a device.
  int cast_channel_id = 0;

  CastSinkExtraData();
  CastSinkExtraData(const CastSinkExtraData& other);
  ~CastSinkExtraData();

  bool operator==(const CastSinkExtraData& other) const;
};

// Represents a media sink discovered by MediaSinkService. It is used by
// MediaSinkService to push MediaSinks with extra data to the
// MediaRouteProvider, and it is not exposed to users of MediaRouter.
class MediaSinkInternal {
 public:
  // Used by mojo.
  MediaSinkInternal();

  // Used by MediaSinkService to create media sinks.
  MediaSinkInternal(const MediaSink& sink, const DialSinkExtraData& dial_data);
  MediaSinkInternal(const MediaSink& sink, const CastSinkExtraData& cast_data);

  // Used to push instance of this class into vector.
  MediaSinkInternal(const MediaSinkInternal& other);

  ~MediaSinkInternal();

  MediaSinkInternal& operator=(const MediaSinkInternal& other);
  bool operator==(const MediaSinkInternal& other) const;

  // Used by mojo.
  void set_sink_id(const MediaSink::Id& sink_id) { sink_.set_sink_id(sink_id); }
  void set_name(const std::string& name) { sink_.set_name(name); }
  void set_description(const std::string& description) {
    sink_.set_description(description);
  }
  void set_domain(const std::string& domain) { sink_.set_domain(domain); }
  void set_icon_type(MediaSink::IconType icon_type) {
    sink_.set_icon_type(icon_type);
  }

  void set_sink(const MediaSink& sink);
  const MediaSink& sink() const { return sink_; }

  void set_dial_data(const DialSinkExtraData& dial_data);

  // Must only be called if the sink is a DIAL sink.
  const DialSinkExtraData& dial_data() const;

  void set_cast_data(const CastSinkExtraData& cast_data);

  // Must only be called if the sink is a Cast sink.
  const CastSinkExtraData& cast_data() const;

  bool is_dial_sink() const { return sink_type_ == SinkType::DIAL; }
  bool is_cast_sink() const { return sink_type_ == SinkType::CAST; }

  static bool IsValidSinkId(const std::string& sink_id);

 private:
  void InternalCopyAssignFrom(const MediaSinkInternal& other);
  void InternalCopyConstructFrom(const MediaSinkInternal& other);
  void InternalCleanup();

  enum class SinkType { GENERIC, DIAL, CAST };

  MediaSink sink_;

  SinkType sink_type_;

  union {
    // Set if sink is DIAL sink.
    base::ManualConstructor<DialSinkExtraData> dial_data_;

    // Set if sink is Cast sink.
    base::ManualConstructor<CastSinkExtraData> cast_data_;
  };
};

}  // namespace media_router

#endif  // CHROME_BROWSER_MEDIA_ROUTER_DISCOVERY_MEDIA_SINK_INTERNAL_H_

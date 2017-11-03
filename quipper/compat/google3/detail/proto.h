#ifndef QUIPPER_COMPAT_GOOGLE3_DETAIL_PROTO_H_
#define QUIPPER_COMPAT_GOOGLE3_DETAIL_PROTO_H_

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "perf_data.pb.h"
#include "perf_stat.pb.h"

namespace quipper {

using ::google::protobuf::io::ArrayInputStream;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::TextFormat;
using ::google::protobuf::Arena;
using ::google::protobuf::Message;
using ::google::protobuf::util::MessageDifferencer;
}  // namespace quipper

#endif  // QUIPPER_COMPAT_GOOGLE3_DETAIL_PROTO_H_

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/v8-tracing.h"

#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace platform {
namespace tracing {

void TraceObject::Initialize(char phase, std::string name,
                             std::string category_group, uint64_t id,
                             uint64_t bind_id, int num_args, int flags) {
  pid_ = base::OS::GetCurrentProcessId();
  tid_ = base::OS::GetCurrentThreadId();
  phase_ = phase;
  name_ = name;
  category_group_ = category_group;
  id_ = id;
  bind_id_ = bind_id;
  num_args_ = num_args;
  flags_ = flags;
  ts_ = base::TimeTicks::HighResolutionNow().ToInternalValue();
  tts_ = base::ThreadTicks::Now().ToInternalValue();
  duration_ = 0;
  cpu_duration_ = 0;
}

void TraceObject::UpdateDuration() {
  duration_ = base::TimeTicks::HighResolutionNow().ToInternalValue() - ts_;
  cpu_duration_ = base::ThreadTicks::Now().ToInternalValue() - tts_;
}

void TraceObject::InitializeForTesting(char phase, std::string name,
                                       std::string category_group, uint64_t id,
                                       uint64_t bind_id, int num_args,
                                       int flags, int pid, int tid, int64_t ts,
                                       int64_t tts, uint64_t duration,
                                       uint64_t cpu_duration) {
  pid_ = pid;
  tid_ = tid;
  phase_ = phase;
  name_ = name;
  category_group_ = category_group;
  id_ = id;
  bind_id_ = bind_id;
  num_args_ = num_args;
  flags_ = flags;
  ts_ = ts;
  tts_ = tts;
  duration_ = duration;
  cpu_duration_ = cpu_duration;
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8

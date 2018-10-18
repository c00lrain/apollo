/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include <sched.h>
#include <chrono>

#include "cybertron/common/global_data.h"
#include "cybertron/common/log.h"
#include "cybertron/croutine/croutine.h"
#include "cybertron/croutine/routine_context.h"
#include "cybertron/scheduler/processor.h"
#include "cybertron/scheduler/processor_context.h"
#include "cybertron/scheduler/scheduler.h"
#include "cybertron/time/time.h"

namespace apollo {
namespace cybertron {
namespace scheduler {

Processor::Processor() { routine_context_.reset(new RoutineContext()); }

Processor::~Processor() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Processor::Start() {
  thread_ = std::thread(&Processor::Run, this);
  uint32_t core_num = std::thread::hardware_concurrency();
  if (core_num != 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(id_, &set);
    pthread_setaffinity_np(thread_.native_handle(), sizeof(set), &set);
  }
}

void Processor::Run() {
  CRoutine::SetMainContext(routine_context_);

  std::shared_ptr<CRoutine> cr = nullptr;

  while (running_) {
    if (context_) {
      cr = context_->NextRoutine();
      if (cr) {
        cr->Resume();
      } else {
        std::unique_lock<std::mutex> lk_rq(mtx_rq_);
        if (Scheduler::Instance()->IsClassic()) {
          cv_.wait_for(lk_rq, std::chrono::milliseconds(1),
                       [this] { return !this->context_->RqEmpty(); });
        } else {
          cv_.wait_for(lk_rq, std::chrono::milliseconds(1));
        }
      }
    } else {
      std::unique_lock<std::mutex> lk_rq(mtx_rq_);
      cv_.wait(lk_rq,
               [this] { return this->context_ && !this->context_->RqEmpty(); });
    }
  }
}

}  // namespace scheduler
}  // namespace cybertron
}  // namespace apollo
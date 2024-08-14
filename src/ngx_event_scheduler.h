#pragma once

#include <datadog/event_scheduler.h>

#include <memory>
#include <unordered_set>

#include "dd.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_event.h>
}

namespace datadog {
namespace nginx {

class NgxEventScheduler : public dd::EventScheduler {
 public:
  struct Event {
    std::chrono::steady_clock::duration interval;
    std::function<void()> callback;
    ngx_event_t event;

    Event(std::function<void()> callback,
          std::chrono::steady_clock::duration interval);

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
  };

 private:
  std::unordered_set<Event*> events_;

 public:
  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override;

  std::string config() const override;

  ~NgxEventScheduler();
};

}  // namespace nginx
}  // namespace datadog

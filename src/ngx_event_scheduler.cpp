#include "ngx_event_scheduler.h"

#include <chrono>

namespace datadog {
namespace nginx {
namespace {

ngx_msec_t to_milliseconds(std::chrono::steady_clock::duration interval) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(interval)
      .count();
}

extern "C" void handle_event(ngx_event_t *ev) {
  auto *event = static_cast<NgxEventScheduler::Event *>(ev->data);
  // Schedule the next round.
  ngx_add_timer(ev, to_milliseconds(event->interval));

  event->callback();
}

}  // namespace

NgxEventScheduler::Event::Event(std::function<void()> callback,
                                std::chrono::steady_clock::duration interval)
    : interval(interval), callback(callback), event() {
  event.data = this;
  event.log = ngx_cycle->log;
  event.handler = &handle_event;
  event.cancelable = true;  // otherwise a pending event will prevent shutdown
}

dd::EventScheduler::Cancel NgxEventScheduler::schedule_recurring_event(
    std::chrono::steady_clock::duration interval,
    std::function<void()> callback) {
  auto event = std::make_unique<Event>(std::move(callback), interval);
  events_.insert(event.get());
  ngx_add_timer(&event->event, to_milliseconds(event->interval));

  // Return a cancellation function.
  return [this, event = event.release()]() {
    ngx_event_del_timer(&event->event);
    events_.erase(event);
    delete event;
  };
}

NgxEventScheduler::~NgxEventScheduler() {
  for (auto *event : events_) {
    ngx_event_del_timer(&event->event);
    delete event;
  }
}

std::string NgxEventScheduler::config() const {
  return R"({"type": "datadog::nginx::NgxEventScheduler")";
}

}  // namespace nginx
}  // namespace datadog

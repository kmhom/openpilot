#include <chrono>
#include <thread>

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <cassert>
#include "messaging.hpp"
#include "common/timing.h"

// Apple doesn't have timerfd
#ifndef __APPLE__
#include <sys/timerfd.h>
#endif

#ifdef QCOM
namespace {
  int64_t arm_cntpct() {
    int64_t v;
    asm volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
  }
}
#endif

int main() {
  setpriority(PRIO_PROCESS, 0, -13);

  PubMaster pm({"clocks"});

#ifndef __APPLE__
  int timerfd = timerfd_create(CLOCK_BOOTTIME, 0);
  assert(timerfd >= 0);

  struct itimerspec spec = {0};
  spec.it_interval.tv_sec = 1;
  spec.it_interval.tv_nsec = 0;
  spec.it_value.tv_sec = 1;
  spec.it_value.tv_nsec = 0;

  int err = timerfd_settime(timerfd, 0, &spec, 0);
  assert(err == 0);

  uint64_t expirations = 0;
  while ((err = read(timerfd, &expirations, sizeof(expirations)))) {
    if (err < 0) break;
#else
  // Just run at 1Hz on apple
  while (true){
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif

    uint64_t boottime = nanos_since_boot();
    uint64_t monotonic = nanos_monotonic();
    uint64_t monotonic_raw = nanos_monotonic_raw();
    uint64_t wall_time = nanos_since_epoch();

#ifdef QCOM
    uint64_t modem_uptime_v = arm_cntpct() / 19200ULL; // 19.2 mhz clock
#endif

    capnp::MallocMessageBuilder msg;
    cereal::Event::Builder event = msg.initRoot<cereal::Event>();
    event.setLogMonoTime(boottime);
    auto clocks = event.initClocks();

    clocks.setBootTimeNanos(boottime);
    clocks.setMonotonicNanos(monotonic);
    clocks.setMonotonicRawNanos(monotonic_raw);
    clocks.setWallTimeNanos(wall_time);
#ifdef QCOM
    clocks.setModemUptimeMillis(modem_uptime_v);
#endif

    pm.send("clocks", msg);
  }

#ifndef __APPLE__
  close(timerfd);
#endif
  return 0;
}

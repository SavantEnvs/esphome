// mayhem: environment shim linked into every fuzz binary. Drivers pace their device I/O with
// esphome::delay()/delayMicroseconds(), which on the host platform (components/host/hal.cpp)
// end in libc nanosleep(). Against an in-memory bus there is nothing to wait for, so this libc
// call returns immediately (the elapsed-time view, millis(), still reads the real clock).
#include <ctime>

extern "C" int nanosleep(const struct timespec *, struct timespec *rem) {
  if (rem) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

#include <new>
#include "esphome/core/application.h"

// The host platform's main() (renamed in the fuzz build) would call these; libFuzzer runs instead.
void setup() {}
void loop() {}

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  // Construct App once, as generated code and upstream's test main (tests/components/main.cpp) do.
  new (&esphome::App) esphome::Application;
  return 0;
}

#pragma once

// Some ESP-IDF builds don't enable FreeRTOS trace hooks; provide no-op
// definitions so TinyUSB compiles cleanly on those configurations.
#ifndef traceISR_EXIT_TO_SCHEDULER
#define traceISR_EXIT_TO_SCHEDULER()
#endif

#ifndef traceISR_ENTER
#define traceISR_ENTER(x)
#endif

#ifndef traceISR_EXIT
#define traceISR_EXIT(x)
#endif

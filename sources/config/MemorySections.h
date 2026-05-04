#ifndef _MEMORY_SECTIONS_H_
#define _MEMORY_SECTIONS_H_

#define PICOTRACKER_FAST_DATA __attribute__((section(".DTCMRAM")))
#define PICOTRACKER_FAST_AUDIO_BUFFER \
  PICOTRACKER_FAST_DATA __attribute__((aligned(32)))

#endif

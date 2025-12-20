/**
 * DisplayPresent.h
 *
 * Why this file exists:
 * Arduino's .ino build pipeline auto-generates function prototypes. That step
 * frequently breaks C++ templates (especially SFINAE helpers) and can produce
 * errors like:
 *   "variable or field 'tryPresent' declared void"
 *
 * By moving the template helpers into a normal header, we avoid Arduino's
 * prototype generation and keep the code standards-compliant.
 */
#pragma once

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "config.h"

namespace DisplayPresentDetail {
  // Different versions of ESP32-HUB75-MatrixPanel-I2S-DMA expose different
  // "present" APIs. These overloads attempt the common variants.
  template <typename T>
  static auto tryPresent(T* d, int) -> decltype(d->flipDMABuffer(true), void()) { d->flipDMABuffer(true); }
  template <typename T>
  static auto tryPresent(T* d, long) -> decltype(d->flipDMABuffer(), void()) { d->flipDMABuffer(); }
  template <typename T>
  static auto tryPresent(T* d, char) -> decltype(d->showDMABuffer(true), void()) { d->showDMABuffer(true); }
  template <typename T>
  static auto tryPresent(T* d, unsigned char) -> decltype(d->showDMABuffer(), void()) { d->showDMABuffer(); }
  template <typename T>
  static void tryPresent(T*, ...) {}
}

/**
 * Present the back buffer if double buffering is enabled.
 * If the linked library doesn't support a present call, this becomes a no-op.
 */
static inline void presentFrame(MatrixPanel_I2S_DMA* d) {
#if ENABLE_DOUBLE_BUFFER
  DisplayPresentDetail::tryPresent(d, 0);
#else
  (void)d;
#endif
}



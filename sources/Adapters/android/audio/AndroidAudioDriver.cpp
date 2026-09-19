/* SPDX-License-Identifier: BSD-3-Clause */
#include "AndroidAudioDriver.h"
#include "Services/Audio/MonoPcmCapture.h"
#include <android/log.h>
#include <algorithm>
#include <cstdio>

namespace {
void CloseStream(AAudioStream *&stream) {
  if (!stream) return;
  AAudioStream_requestStop(stream);
  // close waits for data callbacks before their destination storage is reused.
  AAudioStream_close(stream);
  stream = nullptr;
}
bool Open(AAudioStream **stream, aaudio_direction_t direction,
          AAudioStream_dataCallback callback, AAudioStream_errorCallback error,
          void *owner) {
  AAudioStreamBuilder *builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setDirection(builder, direction);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setSampleRate(builder, 44100);
  AAudioStreamBuilder_setChannelCount(builder, direction == AAUDIO_DIRECTION_OUTPUT ? 2 : 1);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(builder, callback, owner);
  AAudioStreamBuilder_setErrorCallback(builder, error, owner);
  const auto result = AAudioStreamBuilder_openStream(builder, stream);
  AAudioStreamBuilder_delete(builder);
  if (result != AAUDIO_OK) return false;
  // Never silently play/record at the wrong pitch if the platform negotiates
  // a different format. Shared AAudio provides conversion on supported devices.
  if (AAudioStream_getSampleRate(*stream) != 44100 ||
      AAudioStream_getFormat(*stream) != AAUDIO_FORMAT_PCM_FLOAT ||
      AAudioStream_getChannelCount(*stream) != (direction == AAUDIO_DIRECTION_OUTPUT ? 2 : 1)) {
    CloseStream(*stream);
    return false;
  }
  return true;
}
}
bool AndroidAudioDriver::OpenOutput() {
  return output_ || Open(&output_, AAUDIO_DIRECTION_OUTPUT, Output, OutputError, this);
}
bool AndroidAudioDriver::InitDriver() { return OpenOutput(); }
void AndroidAudioDriver::CloseDriver() { StopDriver(); EndInputCapture(); CloseStream(output_); }
bool AndroidAudioDriver::StartDriver() {
  if (started_) return true;
  if (!OpenOutput()) return false;
  ring_.Reset(); consumed_.store(0); started_ = true;
  if (!suspended_ && AAudioStream_requestStart(output_) != AAUDIO_OK) {
    started_ = false; return false;
  }
  return true;
}
void AndroidAudioDriver::StopDriver() { started_ = false; CloseStream(output_); }
void AndroidAudioDriver::SetSuspended(bool value) {
  if (suspended_ == value) return;
  suspended_ = value;
  if (value) { EndInputCapture(); CloseStream(output_); }
  else if (started_) {
    ring_.Reset();
    if (OpenOutput() && AAudioStream_requestStart(output_) != AAUDIO_OK)
      CloseStream(output_);
  }
}
int AndroidAudioDriver::GetPlayedBufferPercentage() { return ring_.FillFrames() * 100 / 16384; }
double AndroidAudioDriver::GetStreamTime() { return consumed_.load() / 44100.0; }
void AndroidAudioDriver::AddBuffer(short *data, int frames) {
  if (started_ && data && frames > 0)
    (void)ring_.WriteInterleaved({data, static_cast<std::size_t>(frames) * 2});
}
void AndroidAudioDriver::PumpProducer() noexcept {
  if (!started_ || suspended_) return;
  if (outputFailed_.exchange(false)) {
    CloseStream(output_); ring_.Reset();
    nextOutputAttempt_ = {};
  }
  if (!output_ && std::chrono::steady_clock::now() >= nextOutputAttempt_) {
    nextOutputAttempt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    if (OpenOutput() && AAudioStream_requestStart(output_) != AAUDIO_OK) CloseStream(output_);
  }
  for (int i = 0; i < 3 && ring_.FillFrames() < 4096; ++i) {
    onAudioBufferTick(); OnNewBufferNeeded();
  }
}
aaudio_data_callback_result_t AndroidAudioDriver::Output(AAudioStream *, void *ctx, void *data, int32_t frames) {
  auto &self = *static_cast<AndroidAudioDriver *>(ctx);
  auto *out = static_cast<float *>(data);
  std::array<StereoF32, 256> scratch{};
  for (int32_t offset = 0; offset < frames;) {
    const auto count = std::min<int32_t>(256, frames - offset);
    (void)self.ring_.Read(std::span(scratch).first(count));
    for (int i = 0; i < count; ++i) {
      out[(offset+i)*2] = scratch[i].left;
      out[(offset+i)*2+1] = scratch[i].right;
    }
    offset += count;
  }
  self.consumed_.fetch_add(frames);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void AndroidAudioDriver::OutputError(AAudioStream *, void *ctx, aaudio_result_t) {
  static_cast<AndroidAudioDriver *>(ctx)->outputFailed_.store(true);
}
void AndroidAudioDriver::InputError(AAudioStream *, void *ctx, aaudio_result_t) {
  static_cast<AndroidAudioDriver *>(ctx)->capturing_.store(false);
}
bool AndroidAudioDriver::BeginInputCapture(std::span<std::int16_t> destination) noexcept {
  EndInputCapture();
  if (destination.empty() || suspended_) return false;
  destination_ = destination; captured_.store(0); peak_.store(0);
  if (!Open(&input_, AAUDIO_DIRECTION_INPUT, Input, InputError, this)) return false;
  capturing_.store(true);
  if (AAudioStream_requestStart(input_) != AAUDIO_OK) { EndInputCapture(); return false; }
  return true;
}
void AndroidAudioDriver::EndInputCapture() noexcept { capturing_.store(false); CloseStream(input_); }
aaudio_data_callback_result_t AndroidAudioDriver::Input(AAudioStream *, void *ctx, void *data, int32_t frames) {
  auto &self = *static_cast<AndroidAudioDriver *>(ctx);
  if (!self.capturing_.load()) return AAUDIO_CALLBACK_RESULT_STOP;
  const auto offset = self.captured_.load();
  const auto stats = CopyMonoPcmCapture({static_cast<const float *>(data), static_cast<std::size_t>(frames)}, self.destination_.subspan(offset));
  self.peak_.store(stats.peak);
  self.captured_.store(offset + stats.frames);
  if (offset + stats.frames == self.destination_.size()) {
    self.capturing_.store(false); return AAUDIO_CALLBACK_RESULT_STOP;
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void AndroidAudioDriver::FormatInputCaptureStats(std::span<char> out) const {
  std::snprintf(out.data(), out.size(), "AAudio capture: frames=%zu peak=%u\n", captured_.load(), peak_.load());
}
void AndroidAudioDriver::LogInputCaptureStats() const {
  __android_log_print(ANDROID_LOG_INFO, "NullPerator", "Recorded %zu frames", captured_.load());
}

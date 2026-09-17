/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSAudioDriver.h"
#include "Services/Audio/MonoPcmCapture.h"

#include <TargetConditionals.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <os/log.h>
#include <span>
#include <thread>

IOSAudioDriver::IOSAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {}

IOSAudioDriver::~IOSAudioDriver() { CloseDriver(); }

bool IOSAudioDriver::InitDriver() {
#if TARGET_OS_SIMULATOR
  // RemoteIO can block simulator launch while CoreAudio's aggregate device is
  // still being created. The simulator still runs the mixer/sequencer for UI
  // tests; physical iOS devices use the real-time RemoteIO path below.
  return true;
#else
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_RemoteIO;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent component = AudioComponentFindNext(nullptr, &description);
  if (component == nullptr ||
      AudioComponentInstanceNew(component, &unit_) != noErr) {
    unit_ = nullptr;
    return false;
  }

  UInt32 enabled = 0U;
  if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                           kAudioUnitScope_Input, 1, &enabled,
                           sizeof(enabled)) != noErr) {
    CloseDriver();
    return false;
  }

  AURenderCallbackStruct callback{&IOSAudioDriver::Render, this};
  if (AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                           kAudioUnitScope_Input, 0, &callback,
                           sizeof(callback)) != noErr) {
    CloseDriver();
    return false;
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = 44100.0;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                        kAudioFormatFlagIsNonInterleaved |
                        kAudioFormatFlagsNativeEndian;
  format.mFramesPerPacket = 1;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 32;
  format.mBytesPerFrame = sizeof(float);
  format.mBytesPerPacket = sizeof(float);
  if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Input, 0, &format,
                           sizeof(format)) != noErr) {
    CloseDriver();
    return false;
  }

  if (AudioUnitInitialize(unit_) != noErr) {
    CloseDriver();
    return false;
  }
  inputAvailable_.store(true, std::memory_order_release);
  return true;
#endif
}

void IOSAudioDriver::CloseDriver() {
  StopDriver();
  inputAvailable_.store(false, std::memory_order_release);
  EndInputCapture();
  inputMonitoring_.store(false, std::memory_order_release);
  if (unit_ == nullptr)
    return;
  AudioUnitUninitialize(unit_);
  AudioComponentInstanceDispose(unit_);
  unit_ = nullptr;
}

bool IOSAudioDriver::StartDriver() {
#if TARGET_OS_SIMULATOR
  ring_.Reset();
  consumedFrames_.store(0U, std::memory_order_release);
  started_.store(true, std::memory_order_release);
  return true;
#else
  if (unit_ == nullptr)
    return false;
  ring_.Reset();
  consumedFrames_.store(0U, std::memory_order_release);
  started_.store(true, std::memory_order_release);
  if (AudioOutputUnitStart(unit_) != noErr) {
    started_.store(false, std::memory_order_release);
    return false;
  }
  return true;
#endif
}

void IOSAudioDriver::StopDriver() {
  started_.store(false, std::memory_order_release);
#if !TARGET_OS_SIMULATOR
  if (unit_ != nullptr)
    AudioOutputUnitStop(unit_);
#endif
}

bool IOSAudioDriver::Interlaced() { return true; }

int IOSAudioDriver::GetPlayedBufferPercentage() {
  return static_cast<int>((ring_.FillFrames() * 100U) / RingCapacityFrames);
}

double IOSAudioDriver::GetStreamTime() {
  return static_cast<double>(consumedFrames_.load(std::memory_order_acquire)) /
         44100.0;
}

void IOSAudioDriver::AddBuffer(short *buffer, int samplecount) {
  if (buffer == nullptr || samplecount <= 0 ||
      !started_.load(std::memory_order_acquire))
    return;
  (void)ring_.WriteInterleaved(std::span<const short>(
      buffer, static_cast<std::size_t>(samplecount) * 2U));
}

void IOSAudioDriver::OnAudioActive(bool active) {
  active_.store(active, std::memory_order_release);
}

void IOSAudioDriver::PumpProducer() noexcept {
  if (!started_.load(std::memory_order_acquire))
    return;
#if TARGET_OS_SIMULATOR
  std::array<StereoF32, 1470> simulatedCallback{};
  (void)ring_.Read(simulatedCallback);
  consumedFrames_.fetch_add(simulatedCallback.size(),
                            std::memory_order_relaxed);
#endif
  for (int request = 0; request < 3 && ring_.FillFrames() < TargetFillFrames;
       ++request) {
    onAudioBufferTick();
    OnNewBufferNeeded();
  }
}

bool IOSAudioDriver::InputAvailable() const noexcept {
  return inputAvailable_.load(std::memory_order_acquire);
}

void IOSAudioDriver::SetInputMonitoring(bool enabled) noexcept {
  inputMonitoring_.store(false, std::memory_order_release);
  if (!enabled && !inputCaptureGate_.IsRunning())
    inputPeak_.store(0U, std::memory_order_release);
}

bool IOSAudioDriver::IsInputMonitoring() const noexcept {
  return inputMonitoring_.load(std::memory_order_acquire);
}

bool IOSAudioDriver::BeginInputCapture(
    std::span<std::int16_t> destination) noexcept {
  if (!InputAvailable() || destination.empty() || inputCaptureGate_.IsRunning())
    return false;
  EndInputCapture(); // Also drain the callback that filled the previous take.
  inputCapacityFrames_ = destination.size();
  inputCapturedFrames_.store(0U, std::memory_order_release);
  inputDestination_.store(destination.data(), std::memory_order_release);
  inputLargestBlock_.store(0);
  inputRenderErrors_.store(0);
  inputMissingFrames_.store(0);
  inputShortBlocks_.store(0);
  inputShortFrames_.store(0);
  inputInvalidFloats_.store(0);
  inputClippedSamples_.store(0);
  inputTimelineDiscontinuities_.store(0);
  inputLastError_.store(noErr);
  inputHasSampleTime_ = false;
  if (!ConfigureInput(true))
    return false;
  inputCaptureGate_.Start();
  return true;
}

void IOSAudioDriver::EndInputCapture() noexcept {
  inputCaptureGate_.Stop();
  while (!inputCaptureGate_.IsIdle())
    std::this_thread::yield();
  (void)ConfigureInput(false);
}

bool IOSAudioDriver::IsInputCapturing() const noexcept {
  return inputCaptureGate_.IsRunning();
}

std::size_t IOSAudioDriver::CapturedInputFrames() const noexcept {
  return inputCapturedFrames_.load(std::memory_order_acquire);
}

std::uint16_t IOSAudioDriver::InputPeak() const noexcept {
  return inputPeak_.load(std::memory_order_acquire);
}

void IOSAudioDriver::LogInputCaptureStats() const {
  // Called after the capture callback drains, never from the audio thread.
  std::array<char, 384> text{};
  FormatInputCaptureStats(text);
  os_log(OS_LOG_DEFAULT, "NullPerator capture: %{public}s", text.data());
}

void IOSAudioDriver::FormatInputCaptureStats(std::span<char> output) const {
  if (output.empty())
    return;
  std::snprintf(
      output.data(), output.size(),
      "frames=%zu max_block=%u errors=%u last_error=%d missing=%u "
      "short_blocks=%u short_frames=%u invalid_floats=%u "
      "clipped=%u discontinuities=%u\n",
      CapturedInputFrames(), inputLargestBlock_.load(),
      inputRenderErrors_.load(), static_cast<int>(inputLastError_.load()),
      inputMissingFrames_.load(), inputShortBlocks_.load(),
      inputShortFrames_.load(), inputInvalidFloats_.load(),
      inputClippedSamples_.load(), inputTimelineDiscontinuities_.load());
}

OSStatus IOSAudioDriver::Render(void *context,
                                AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *timestamp, UInt32,
                                UInt32 frames, AudioBufferList *buffers) {
  return static_cast<IOSAudioDriver *>(context)->Render(flags, timestamp,
                                                        frames, buffers);
}

OSStatus IOSAudioDriver::Render(AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *timestamp, UInt32 frames,
                                AudioBufferList *buffers) noexcept {
  PullInput(flags, timestamp, frames);
  if (buffers == nullptr || buffers->mNumberBuffers < 2 ||
      buffers->mBuffers[0].mData == nullptr ||
      buffers->mBuffers[1].mData == nullptr)
    return noErr;

  auto *left = static_cast<float *>(buffers->mBuffers[0].mData);
  auto *right = static_cast<float *>(buffers->mBuffers[1].mData);
  std::array<StereoF32, 512> scratch{};
  std::size_t offset = 0;
  while (offset < frames) {
    const std::size_t count =
        std::min<std::size_t>(scratch.size(), frames - offset);
    (void)ring_.Read(std::span<StereoF32>(scratch.data(), count));
    for (std::size_t index = 0; index < count; ++index) {
      left[offset + index] = scratch[index].left;
      right[offset + index] = scratch[index].right;
    }
    offset += count;
  }
  consumedFrames_.fetch_add(frames, std::memory_order_relaxed);
  return noErr;
}

void IOSAudioDriver::PullInput(AudioUnitRenderActionFlags *flags,
                               const AudioTimeStamp *timestamp,
                               UInt32 frames) noexcept {
#if TARGET_OS_SIMULATOR
  (void)flags;
  (void)timestamp;
  (void)frames;
#else
  (void)flags;
  // Acquire before AudioUnitRender: an old callback cannot copy microphone
  // data into a new take after Stop/Begin changes the destination.
  WorkerGate<1>::Guard capture(inputCaptureGate_, 0);
  const bool capturing = static_cast<bool>(capture);
  if (!capturing || unit_ == nullptr || timestamp == nullptr || frames == 0U) {
    if (!capturing)
      inputPeak_.store(0U, std::memory_order_release);
    return;
  }
  inputLargestBlock_.store(std::max(inputLargestBlock_.load(), frames));
  if ((timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0) {
    if (inputHasSampleTime_ &&
        std::abs(timestamp->mSampleTime - inputExpectedSampleTime_) > 0.5)
      inputTimelineDiscontinuities_.fetch_add(1);
    inputExpectedSampleTime_ = timestamp->mSampleTime + frames;
    inputHasSampleTime_ = true;
  }

  AudioBufferList input{};
  input.mNumberBuffers = 1U;
  input.mBuffers[0].mNumberChannels = 1U;
  input.mBuffers[0].mDataByteSize = frames * sizeof(float);
  // RemoteIO owns buffers sized for its current route and sample conversion.
  // Its maximum callback size may change; a fixed 4096-frame buffer dropped
  // larger callbacks and spliced nonadjacent input blocks together.
  input.mBuffers[0].mData = nullptr;
  AudioUnitRenderActionFlags inputFlags = 0;
  const OSStatus status =
      AudioUnitRender(unit_, &inputFlags, timestamp, 1U, frames, &input);
  const bool silent =
      status == noErr &&
      (inputFlags & kAudioUnitRenderAction_OutputIsSilence) != 0;
  std::span<const float> source;
  if (status == noErr && !silent && input.mBuffers[0].mData != nullptr)
    source = {static_cast<const float *>(input.mBuffers[0].mData),
              std::min<std::size_t>(frames, input.mBuffers[0].mDataByteSize /
                                                sizeof(float))};
  // With rate conversion, a successful RemoteIO render can return fewer
  // valid frames than requested. Concatenate those frames; padding the tail
  // introduced periodic one-sample zeros (clicks) into the saved recording.
  const bool failed = status != noErr || (!silent && source.empty());
  if (failed) {
    inputRenderErrors_.fetch_add(1);
    inputLastError_.store(status == noErr ? kAudio_ParamError : status);
    inputMissingFrames_.fetch_add(frames);
  } else if (!silent && source.size() < frames) {
    inputShortBlocks_.fetch_add(1);
    inputShortFrames_.fetch_add(frames - source.size());
  }
  std::int16_t *destination = inputDestination_.load(std::memory_order_acquire);
  const std::size_t offset =
      inputCapturedFrames_.load(std::memory_order_relaxed);
  const std::size_t capacity =
      std::min<std::size_t>(frames, inputCapacityFrames_ - offset);
  MonoPcmCaptureStats stats;
  if (destination != nullptr) {
    if (failed || silent) {
      // Actual render failures retain their elapsed duration, with a logged
      // error. Successful partial input never follows this silence path.
      std::fill_n(destination + offset, capacity, std::int16_t{0});
      stats.frames = capacity;
    } else {
      stats = CopyMonoPcmCapture(source, {destination + offset, capacity});
    }
  }
  inputPeak_.store(stats.peak, std::memory_order_release);
  inputClippedSamples_.fetch_add(stats.clipped);
  inputInvalidFloats_.fetch_add(stats.invalid);
  inputCapturedFrames_.store(offset + stats.frames, std::memory_order_release);
  if (offset + stats.frames >= inputCapacityFrames_)
    inputCaptureGate_.Stop();
#endif
}

extern "C" bool NullPeratorIOSSetRecordingSession(bool recording);

bool IOSAudioDriver::ConfigureInput(bool enabled) noexcept {
#if TARGET_OS_SIMULATOR
  (void)enabled;
  return false;
#else
  if (inputEnabled_ == enabled)
    return true;
  if (unit_ == nullptr)
    return false;
  const bool restart = started_.load(std::memory_order_acquire);
  AudioOutputUnitStop(unit_);
  AudioUnitUninitialize(unit_);
  bool success = NullPeratorIOSSetRecordingSession(enabled);
  UInt32 value = enabled ? 1U : 0U;
  success = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Input, 1, &value,
                                 sizeof(value)) == noErr &&
            success;
  if (enabled && success) {
    AudioStreamBasicDescription format{};
    format.mSampleRate = 44100.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved |
                          kAudioFormatFlagsNativeEndian;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = sizeof(float);
    format.mBytesPerPacket = sizeof(float);
    success = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                   kAudioUnitScope_Output, 1, &format,
                                   sizeof(format)) == noErr;
    UInt32 allocate = 1U;
    success =
        AudioUnitSetProperty(unit_, kAudioUnitProperty_ShouldAllocateBuffer,
                             kAudioUnitScope_Output, 1, &allocate,
                             sizeof(allocate)) == noErr &&
        success;
  }
  success = AudioUnitInitialize(unit_) == noErr && success;
  if (restart)
    success = AudioOutputUnitStart(unit_) == noErr && success;
  inputEnabled_ = enabled;
  if (!success && enabled)
    (void)ConfigureInput(false);
  if (!success && !enabled) {
    // Never leave an input unit running after an unsuccessful teardown.
    AudioOutputUnitStop(unit_);
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
    started_.store(false, std::memory_order_release);
    inputAvailable_.store(false, std::memory_order_release);
  }
  return success;
#endif
}

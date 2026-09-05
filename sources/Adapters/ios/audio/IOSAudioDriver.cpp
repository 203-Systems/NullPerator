/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSAudioDriver.h"

#include <TargetConditionals.h>
#include <algorithm>
#include <array>
#include <cmath>
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

  UInt32 enabled = 1U;
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

  AudioStreamBasicDescription inputFormat = format;
  inputFormat.mChannelsPerFrame = 1;
  if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Output, 1, &inputFormat,
                           sizeof(inputFormat)) != noErr ||
      AudioUnitInitialize(unit_) != noErr) {
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
  inputMonitoring_.store(enabled && InputAvailable(),
                         std::memory_order_release);
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
  inputCaptureGate_.Start();
  return true;
}

void IOSAudioDriver::EndInputCapture() noexcept {
  inputCaptureGate_.Stop();
  while (!inputCaptureGate_.IsIdle())
    std::this_thread::yield();
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
  const bool monitoring = inputMonitoring_.load(std::memory_order_acquire);
  // Acquire before AudioUnitRender: an old callback cannot copy microphone
  // data into a new take after Stop/Begin changes the destination.
  WorkerGate<1>::Guard capture(inputCaptureGate_, 0);
  const bool capturing = static_cast<bool>(capture);
  if ((!monitoring && !capturing) || unit_ == nullptr || timestamp == nullptr ||
      frames == 0U || frames > inputScratch_.size()) {
    if (!monitoring && !capturing)
      inputPeak_.store(0U, std::memory_order_release);
    return;
  }

  AudioBufferList input{};
  input.mNumberBuffers = 1U;
  input.mBuffers[0].mNumberChannels = 1U;
  input.mBuffers[0].mDataByteSize = frames * sizeof(float);
  input.mBuffers[0].mData = inputScratch_.data();
  if (AudioUnitRender(unit_, flags, timestamp, 1U, frames, &input) != noErr) {
    inputPeak_.store(0U, std::memory_order_release);
    return;
  }

  std::uint16_t peak = 0U;
  for (UInt32 index = 0U; index < frames; ++index) {
    const float clamped = std::clamp(inputScratch_[index], -1.0F, 1.0F);
    const auto sample = static_cast<std::int16_t>(
        std::lrint(clamped * (clamped < 0.0F ? 32768.0F : 32767.0F)));
    inputPcmScratch_[index] = sample;
    const std::uint16_t magnitude = static_cast<std::uint16_t>(
        sample == INT16_MIN ? INT16_MAX : std::abs(sample));
    peak = std::max(peak, magnitude);
  }
  inputPeak_.store(peak, std::memory_order_release);

  if (!capturing)
    return;
  std::int16_t *destination = inputDestination_.load(std::memory_order_acquire);
  const std::size_t offset =
      inputCapturedFrames_.load(std::memory_order_relaxed);
  const std::size_t count =
      std::min<std::size_t>(frames, inputCapacityFrames_ - offset);
  if (destination != nullptr) {
    std::copy_n(inputPcmScratch_.data(), count, destination + offset);
  }
  inputCapturedFrames_.store(offset + count, std::memory_order_release);
  if (offset + count >= inputCapacityFrames_)
    inputCaptureGate_.Stop();
#endif
}

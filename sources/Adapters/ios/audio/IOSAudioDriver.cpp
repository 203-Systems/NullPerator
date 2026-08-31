/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSAudioDriver.h"

#include <TargetConditionals.h>
#include <algorithm>
#include <array>
#include <span>

IOSAudioDriver::IOSAudioDriver(AudioSettings &settings) : AudioDriver(settings) {}

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
  format.mFormatFlags = kAudioFormatFlagIsFloat |
                        kAudioFormatFlagIsPacked |
                        kAudioFormatFlagIsNonInterleaved |
                        kAudioFormatFlagsNativeEndian;
  format.mFramesPerPacket = 1;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 32;
  format.mBytesPerFrame = sizeof(float);
  format.mBytesPerPacket = sizeof(float);
  if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Input, 0, &format,
                           sizeof(format)) != noErr ||
      AudioUnitInitialize(unit_) != noErr) {
    CloseDriver();
    return false;
  }
  return true;
#endif
}

void IOSAudioDriver::CloseDriver() {
  StopDriver();
  if (unit_ == nullptr) return;
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
  if (unit_ == nullptr) return false;
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
  if (unit_ != nullptr) AudioOutputUnitStop(unit_);
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
      !started_.load(std::memory_order_acquire)) return;
  (void)ring_.WriteInterleaved(std::span<const short>(
      buffer, static_cast<std::size_t>(samplecount) * 2U));
}

void IOSAudioDriver::OnAudioActive(bool active) {
  active_.store(active, std::memory_order_release);
}

void IOSAudioDriver::PumpProducer() noexcept {
  if (!started_.load(std::memory_order_acquire)) return;
#if TARGET_OS_SIMULATOR
  std::array<StereoF32, 1470> simulatedCallback{};
  (void)ring_.Read(simulatedCallback);
  consumedFrames_.fetch_add(simulatedCallback.size(),
                            std::memory_order_relaxed);
#endif
  for (int request = 0;
       request < 3 && ring_.FillFrames() < TargetFillFrames; ++request) {
    onAudioBufferTick();
    OnNewBufferNeeded();
  }
}

OSStatus IOSAudioDriver::Render(void *context, AudioUnitRenderActionFlags *,
                                const AudioTimeStamp *, UInt32, UInt32 frames,
                                AudioBufferList *buffers) {
  return static_cast<IOSAudioDriver *>(context)->Render(frames, buffers);
}

OSStatus IOSAudioDriver::Render(UInt32 frames,
                                AudioBufferList *buffers) noexcept {
  if (buffers == nullptr || buffers->mNumberBuffers < 2 ||
      buffers->mBuffers[0].mData == nullptr ||
      buffers->mBuffers[1].mData == nullptr) return noErr;

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

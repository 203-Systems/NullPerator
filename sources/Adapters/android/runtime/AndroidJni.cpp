/* SPDX-License-Identifier: BSD-3-Clause */
#include "AndroidNativeRuntime.h"
#include "Adapters/android/gui/AndroidUiPresenter.h"
#include "Adapters/android/audio/AndroidAudio.h"
#include "Adapters/android/audio/AndroidAudioDriver.h"
#include "System/System/System.h"
#include "Application/Audio/RecordingPlatform.h"
#include <jni.h>
#include "ProductVersion.h"
#include <cstring>
#include <string>
#include <vector>

// All JNI methods run on one HandlerThread. AAudio callbacks never enter JNI.
namespace {
std::unique_ptr<AndroidNativeRuntime> runtime;
std::string importProject;
bool importRequested = false, importPending = false, permissionRequested = false;
SampleImportResult importResult;
int permission = -2;
std::string String(JNIEnv *env, jstring value) {
  if (!value) return {};
  // JNI's GetStringUTFChars uses modified UTF-8, which breaks filesystem names
  // containing supplementary characters. Use Java's standard UTF-8 codec.
  jclass type = env->FindClass("java/lang/String");
  jmethodID method = env->GetMethodID(type, "getBytes", "(Ljava/lang/String;)[B");
  jstring encoding = env->NewStringUTF("UTF-8");
  auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(value, method, encoding));
  std::string result;
  if (bytes) {
    result.resize(env->GetArrayLength(bytes));
    env->GetByteArrayRegion(bytes, 0, result.size(), reinterpret_cast<jbyte *>(result.data()));
    env->DeleteLocalRef(bytes);
  }
  env->DeleteLocalRef(encoding); env->DeleteLocalRef(type);
  return result;
}
jstring JavaString(JNIEnv *env, const std::string &value) {
  jclass type = env->FindClass("java/lang/String");
  jmethodID constructor = env->GetMethodID(type, "<init>", "([BLjava/lang/String;)V");
  jbyteArray bytes = env->NewByteArray(value.size());
  env->SetByteArrayRegion(bytes, 0, value.size(), reinterpret_cast<const jbyte *>(value.data()));
  jstring encoding = env->NewStringUTF("UTF-8");
  auto result = static_cast<jstring>(env->NewObject(type, constructor, bytes, encoding));
  env->DeleteLocalRef(bytes); env->DeleteLocalRef(encoding); env->DeleteLocalRef(type);
  return result;
}
}
extern "C" bool NullPeratorAndroidRequestSampleImport(const char *project) {
  if (importPending) return false;
  importPending = importRequested = true;
  importProject = project ? project : ""; importResult = {}; return true;
}
SampleImportResult NullPeratorAndroidPollSampleImport() {
  const auto result = importResult;
  if (result.status != SampleImportStatus::Pending) { importPending = false; importResult = {}; }
  return result;
}
extern "C" int NullPeratorAndroidRecordPermission() {
  if (permission == -2) { permissionRequested = true; permission = 0; }
  return permission;
}
#define JNI_METHOD(name) extern "C" JNIEXPORT
JNI_METHOD(init) jboolean JNICALL Java_org_nullperator_app_NativeCore_init(JNIEnv *env, jclass, jstring path) {
  if (runtime) return true;
  runtime = std::make_unique<AndroidNativeRuntime>(String(env, path));
  if (!runtime->Init()) { runtime.reset(); return false; } return true;
}
JNI_METHOD(tick) void JNICALL Java_org_nullperator_app_NativeCore_tick(JNIEnv *, jclass) { if (runtime) runtime->Tick(); }
JNI_METHOD(action) void JNICALL Java_org_nullperator_app_NativeCore_action(JNIEnv *, jclass, jint action, jboolean down, jboolean repeat) {
  if (runtime && action >= 0 && action < 16) runtime->SetAction(action, down, repeat);
}
JNI_METHOD(release) void JNICALL Java_org_nullperator_app_NativeCore_release(JNIEnv *, jclass) { if (runtime) runtime->ReleaseAllActions(); }
JNI_METHOD(suspend) void JNICALL Java_org_nullperator_app_NativeCore_suspend(JNIEnv *, jclass, jboolean suspended) {
  if (runtime) runtime->Suspend(suspended);
}
JNI_METHOD(shutdown) void JNICALL Java_org_nullperator_app_NativeCore_shutdown(JNIEnv *, jclass) { runtime.reset(); }
JNI_METHOD(permission) void JNICALL Java_org_nullperator_app_NativeCore_permission(JNIEnv *, jclass, jint value) { permission = value; }
JNI_METHOD(needsPermission) jboolean JNICALL Java_org_nullperator_app_NativeCore_needsPermission(JNIEnv *, jclass) {
  bool value = permissionRequested; permissionRequested = false; return value;
}
JNI_METHOD(takeImport) jstring JNICALL Java_org_nullperator_app_NativeCore_takeImport(JNIEnv *env, jclass) {
  if (!importRequested) return nullptr;
  importRequested = false; return JavaString(env, importProject);
}
JNI_METHOD(importResult) void JNICALL Java_org_nullperator_app_NativeCore_importResult(JNIEnv *env, jclass, jint status, jstring path) {
  if (!importPending) return;
  importResult.status = static_cast<SampleImportStatus>(status);
  const auto text = String(env, path); std::snprintf(importResult.path, sizeof(importResult.path), "%s", text.c_str());
}
JNI_METHOD(battery) void JNICALL Java_org_nullperator_app_NativeCore_battery(JNIEnv *, jclass, jint percent, jboolean charging) {
  if (runtime) runtime->SetBattery(percent, charging, true);
}
JNI_METHOD(buildHash) jstring JNICALL Java_org_nullperator_app_NativeCore_buildHash(JNIEnv *env, jclass) { return env->NewStringUTF(AndroidNativeRuntime::BuildHash()); }
JNI_METHOD(buildTime) jstring JNICALL Java_org_nullperator_app_NativeCore_buildTime(JNIEnv *env, jclass) { return env->NewStringUTF(AndroidNativeRuntime::BuildTime()); }
JNI_METHOD(frame) jbyteArray JNICALL Java_org_nullperator_app_NativeCore_frame(JNIEnv *env, jclass, jint after) {
  AndroidUiFramePacket frame;
  if (!runtime || !runtime->DrainFrame(after, frame)) return nullptr;
  std::vector<std::uint8_t> bytes;
  auto put = [&](std::uint32_t value) { for (int i=0;i<4;++i) bytes.push_back(value >> (i*8)); };
  put(frame.sequence); put(frame.palette.size());
  bytes.insert(bytes.end(), frame.palette.begin(), frame.palette.end());
  put(frame.regions.size());
  for (const auto &region : frame.regions) {
    put(region.bounds.x); put(region.bounds.y); put(region.bounds.width); put(region.bounds.height);
    put(region.indices.size()); bytes.insert(bytes.end(), region.indices.begin(), region.indices.end());
  }
  auto result = env->NewByteArray(bytes.size());
  env->SetByteArrayRegion(result, 0, bytes.size(), reinterpret_cast<const jbyte *>(bytes.data())); return result;
}

JNI_METHOD(productVersion) jstring JNICALL Java_org_nullperator_app_NativeCore_productVersion(JNIEnv *env, jclass) {
  return env->NewStringUTF(nullperator_product::Version);
}

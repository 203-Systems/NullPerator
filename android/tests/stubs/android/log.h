#pragma once
#define ANDROID_LOG_INFO 4
inline int __android_log_print(int, const char *, const char *, ...) { return 0; }

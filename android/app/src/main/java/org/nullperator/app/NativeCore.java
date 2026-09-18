package org.nullperator.app;
final class NativeCore {
    static { System.loadLibrary("nullperator_android_core"); }
    static native boolean init(String path);
    static native void tick();
    static native void action(int action, boolean down, boolean repeat);
    static native void release();
    static native void suspend(boolean suspended);
    static native void shutdown();
    static native void permission(int value);
    static native boolean needsPermission();
    static native String takeImport();
    static native void importResult(int status, String path);
    static native void battery(int percentage, boolean charging);
    static native byte[] frame(int after);
    static native String productVersion();
    static native String buildHash();
    static native String buildTime();
}

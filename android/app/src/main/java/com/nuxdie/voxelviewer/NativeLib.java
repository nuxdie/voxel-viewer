package com.nuxdie.voxelviewer;

/** JNI entry points into the shared C++ viewer. Call from the GL thread unless noted. */
final class NativeLib {
    static {
        System.loadLibrary("voxelviewer");
    }

    private NativeLib() {}

    static native void nativeCreate();
    static native boolean nativeSurfaceCreated();
    static native void nativeResize(int width, int height);
    static native void nativeDraw();
    static native void nativeLoad(byte[] data, String name);
    static native void nativeOrbit(float dx, float dy);
    static native void nativePan(float dx, float dy);
    static native void nativeZoom(float factor);
    static native void nativeFrame();
    static native int nativeCycleMode();
    static native String nativeModeName();
    static native void nativeSlice(int delta);
    static native void nativeClearSlice();
    static native void nativeToggleDecorations();
    static native void nativeToggleNight();
    /** Thread-safe; may be called from the UI thread. */
    static native String nativeStatus();
}

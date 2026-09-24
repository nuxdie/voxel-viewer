package com.nuxdie.voxelviewer;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;

/**
 * OpenGL ES 3 surface running the native renderer.
 * One finger orbits, two fingers pinch-zoom and pan, double tap frames the model.
 */
final class ViewerSurface extends GLSurfaceView {
    private final ScaleGestureDetector scaleDetector;
    private final GestureDetector gestureDetector;
    private final float touchScale;  // converts pixels to "desktop mouse pixels"
    private float lastX, lastY;
    private int lastPointerCount;

    ViewerSurface(Context context) {
        super(context);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(new ConfigChooser());
        setPreserveEGLContextOnPause(true);
        setRenderer(new Renderer());
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        touchScale = 1.6f / getResources().getDisplayMetrics().density;

        scaleDetector = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override
            public boolean onScale(ScaleGestureDetector d) {
                final float f = d.getScaleFactor();
                queueEvent(() -> NativeLib.nativeZoom(f));
                return true;
            }
        });
        gestureDetector = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
            @Override
            public boolean onDoubleTap(MotionEvent e) {
                queueEvent(NativeLib::nativeFrame);
                return true;
            }
        });
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        scaleDetector.onTouchEvent(e);
        gestureDetector.onTouchEvent(e);

        // Track the centroid of all fingers.
        int n = e.getPointerCount();
        float x = 0, y = 0;
        for (int i = 0; i < n; i++) {
            if (e.getActionMasked() == MotionEvent.ACTION_POINTER_UP && i == e.getActionIndex()) continue;
            x += e.getX(i);
            y += e.getY(i);
        }
        int counted = e.getActionMasked() == MotionEvent.ACTION_POINTER_UP ? n - 1 : n;
        if (counted <= 0) return true;
        x /= counted;
        y /= counted;

        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_POINTER_UP:
                // Finger count changed: restart the drag to avoid jumps.
                lastX = x;
                lastY = y;
                lastPointerCount = counted;
                break;
            case MotionEvent.ACTION_MOVE: {
                final float dx = (x - lastX) * touchScale, dy = (y - lastY) * touchScale;
                lastX = x;
                lastY = y;
                if (lastPointerCount == 1 && !scaleDetector.isInProgress()) {
                    queueEvent(() -> NativeLib.nativeOrbit(dx, dy));
                } else if (lastPointerCount >= 2) {
                    final float px = dx / touchScale, py = dy / touchScale;
                    queueEvent(() -> NativeLib.nativePan(px, py));
                }
                break;
            }
            default:
                break;
        }
        return true;
    }

    private static final class Renderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            NativeLib.nativeSurfaceCreated();
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int width, int height) {
            NativeLib.nativeResize(width, height);
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            NativeLib.nativeDraw();
        }
    }

    /** Prefers 4x MSAA with a 24-bit depth buffer, falling back to what the device offers. */
    private static final class ConfigChooser implements GLSurfaceView.EGLConfigChooser {
        private static final int EGL_OPENGL_ES3_BIT = 0x40;

        @Override
        public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
            int[][] attempts = {
                {EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 24,
                 EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL10.EGL_SAMPLE_BUFFERS, 1, EGL10.EGL_SAMPLES, 4,
                 EGL10.EGL_NONE},
                {EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 24,
                 EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL10.EGL_NONE},
                {EGL10.EGL_DEPTH_SIZE, 16, EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL10.EGL_NONE},
            };
            for (int[] attribs : attempts) {
                EGLConfig[] configs = new EGLConfig[1];
                int[] count = new int[1];
                if (egl.eglChooseConfig(display, attribs, configs, 1, count) && count[0] > 0) return configs[0];
            }
            throw new IllegalStateException("No OpenGL ES 3 configuration available");
        }
    }
}

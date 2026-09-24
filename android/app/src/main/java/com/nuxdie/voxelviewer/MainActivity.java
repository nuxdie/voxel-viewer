package com.nuxdie.voxelviewer;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1;
    private static final String[] SAMPLES = {"forest.schem", "house.schem"};

    private ViewerSurface surface;
    private TextView status;
    private Button modeButton;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable statusPoller = new Runnable() {
        @Override
        public void run() {
            status.setText(NativeLib.nativeStatus());
            handler.postDelayed(this, 400);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        NativeLib.nativeCreate();

        FrameLayout root = new FrameLayout(this);
        surface = new ViewerSurface(this);
        root.addView(surface, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(dp(8), dp(8), dp(8), dp(8));
        bar.addView(button("Open", v -> showOpenMenu(v)));
        modeButton = button("Blocks", v -> surface.queueEvent(() -> {
            NativeLib.nativeCycleMode();
            final String name = NativeLib.nativeModeName();
            runOnUiThread(() -> modeButton.setText(name));
        }));
        bar.addView(modeButton);
        bar.addView(button("Slice −", v -> surface.queueEvent(() -> NativeLib.nativeSlice(-1))));
        bar.addView(button("Slice +", v -> surface.queueEvent(() -> NativeLib.nativeSlice(1))));
        bar.addView(button("All", v -> surface.queueEvent(NativeLib::nativeClearSlice)));
        bar.addView(button("Decor", v -> surface.queueEvent(NativeLib::nativeToggleDecorations)));
        bar.addView(button("Reset", v -> surface.queueEvent(NativeLib::nativeFrame)));

        HorizontalScrollView scroller = new HorizontalScrollView(this);
        scroller.setHorizontalScrollBarEnabled(false);
        scroller.addView(bar);
        root.addView(scroller, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START));

        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        status.setShadowLayer(4, 0, 1, Color.BLACK);
        status.setPadding(dp(12), dp(8), dp(12), dp(12));
        root.addView(status, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));

        // Keep the controls clear of the status bar, notch and gesture area.
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            scroller.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), 0);
            status.setPadding(dp(12) + insets.getSystemWindowInsetLeft(), dp(8),
                    dp(12) + insets.getSystemWindowInsetRight(), dp(12) + insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(root);

        Intent intent = getIntent();
        if (savedInstanceState == null && intent != null && Intent.ACTION_VIEW.equals(intent.getAction())
                && intent.getData() != null) {
            loadUri(intent.getData());
        } else if (savedInstanceState == null) {
            loadAsset(SAMPLES[0]);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (Intent.ACTION_VIEW.equals(intent.getAction()) && intent.getData() != null) loadUri(intent.getData());
    }

    @Override
    protected void onResume() {
        super.onResume();
        surface.onResume();
        handler.post(statusPoller);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(statusPoller);
        surface.onPause();
        super.onPause();
    }

    private void showOpenMenu(View anchor) {
        PopupMenu menu = new PopupMenu(this, anchor);
        menu.getMenu().add(0, 0, 0, "Open file…");
        for (int i = 0; i < SAMPLES.length; i++) menu.getMenu().add(0, i + 1, i + 1, "Sample: " + SAMPLES[i]);
        menu.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == 0) {
                Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.setType("*/*");
                startActivityForResult(pick, REQUEST_OPEN);
            } else {
                loadAsset(SAMPLES[item.getItemId() - 1]);
            }
            return true;
        });
        menu.show();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OPEN && resultCode == RESULT_OK && data != null && data.getData() != null) {
            loadUri(data.getData());
        }
    }

    private void loadAsset(String name) {
        new Thread(() -> {
            try (InputStream in = getAssets().open(name)) {
                deliver(readAll(in), name);
            } catch (IOException e) {
                toast("Cannot open sample " + name + ": " + e.getMessage());
            }
        }).start();
    }

    private void loadUri(Uri uri) {
        new Thread(() -> {
            String name = displayName(uri);
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) throw new IOException("no data");
                deliver(readAll(in), name);
            } catch (IOException | SecurityException e) {
                toast("Cannot open " + name + ": " + e.getMessage());
            }
        }).start();
    }

    private void deliver(byte[] data, String name) {
        surface.queueEvent(() -> NativeLib.nativeLoad(data, name));
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
        } catch (RuntimeException ignored) {
            // Fall back to the URI's last segment.
        }
        String last = uri.getLastPathSegment();
        return last != null ? last : "file";
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }

    private void toast(String msg) {
        runOnUiThread(() -> Toast.makeText(this, msg, Toast.LENGTH_LONG).show());
    }

    private Button button(String text, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setBackgroundColor(0x99202530);
        b.setPadding(dp(14), dp(6), dp(14), dp(6));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42));
        lp.setMarginEnd(dp(6));
        b.setLayoutParams(lp);
        return b;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}

package com.jumatt.jsrfunleashed;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

/**
 * The first screen. If the game is in the app's folder (game/, with
 * default.xbe and Media/ in it), it starts it straight away. If not, it says
 * how to put it there: pick the disc image (.iso), which it unpacks
 * (port/ios/xiso.c, through port/android/jsrf_android.c), or copy the
 * extracted folder in from a computer over USB.
 */
public class SetupActivity extends Activity {
    static {
        System.loadLibrary("SDL2");
        System.loadLibrary("main");
    }

    static native String nativeVersion();
    static native boolean nativeProbe(String path);
    static native String nativeCheckImage(String path);
    static native String nativeCheckGame(String dir);
    native String nativeUnpack(String iso, String dest);

    private static final int PICK_ISO = 1;

    private TextView mStatus;
    private ProgressBar mBar;
    private Button mPick, mCheck;
    private volatile boolean mCancel;
    private boolean mBusy;

    private File gameDir() {
        return new File(getExternalFilesDir(null), "game");
    }

    static boolean isGame(File d) {
        return new File(d, "default.xbe").isFile() && new File(d, "Media").isDirectory();
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File g = gameDir();
        boolean found = isGame(g);
        Log.i("JSRF", "game folder " + g + ": " + (found ? "the game is there"
              : "no game (folder " + (g.isDirectory() ? "exists" : "missing")
                + ", default.xbe " + (new File(g, "default.xbe").isFile() ? "yes" : "no")
                + ", Media " + (new File(g, "Media").isDirectory() ? "yes" : "no") + ")"));
        if (found) {
            startGame();
            return;
        }
        showSetup();
    }

    private void startGame() {
        // Another build of the game (European, Japanese, a Sega GT 2002 combo
        // disc) starts and then crashes a few seconds in: the engine is the
        // standalone US disc's code.
        String other = nativeCheckGame(gameDir().getAbsolutePath());
        if (other != null) {
            if (mStatus == null) showSetup();
            status("These files won't work with JSRF Unleashed.\n\n" + other
                   + "\n\nReplace the files in " + gameDir().getAbsolutePath() + " with a standalone US disc's.");
            return;
        }
        startActivity(new Intent(this, JsrfActivity.class));
        finish();
    }

    private void showSetup() {
        int pad = dp(24);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("JSRF Unleashed");
        title.setTextSize(28);
        col.addView(title);

        TextView body = new TextView(this);
        body.setTextSize(16);
        body.setPadding(0, dp(12), 0, dp(12));
        body.setText("This plays your own copy of Jet Set Radio Future: the standalone US disc. "
                + "Nothing of the game comes with the app.\n\n"
                + "Choose the disc image (.iso) of your copy: it is unpacked once into the app, "
                + "which needs about 3 GB of free space.\n\n"
                + "Or copy the extracted game folder (the one with default.xbe and Media in it) "
                + "from a computer over USB, named \"game\", into:\n"
                + "Android/data/" + getPackageName() + "/files/\n\n"
                + "Your saves are kept in that folder too, so uninstalling the app deletes them: "
                + "copy game/UDATA somewhere to keep them.");
        col.addView(body);

        mPick = new Button(this);
        mPick.setText("Choose the disc image (.iso)");
        mPick.setOnClickListener(v -> pickIso());
        col.addView(mPick);

        mCheck = new Button(this);
        mCheck.setText("I copied the folder: start");
        mCheck.setOnClickListener(v -> {
            if (isGame(gameDir())) startGame();
            else status("No game there yet: " + gameDir().getAbsolutePath()
                        + " needs default.xbe and Media in it.");
        });
        col.addView(mCheck);

        mBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        mBar.setMax(1000);
        mBar.setVisibility(View.GONE);
        col.addView(mBar);

        mStatus = new TextView(this);
        mStatus.setTextSize(16);
        mStatus.setPadding(0, dp(12), 0, 0);
        col.addView(mStatus);

        TextView ver = new TextView(this);
        ver.setTextSize(12);
        ver.setPadding(0, dp(24), 0, 0);
        ver.setText("Version " + nativeVersion()
                + ". An unofficial fan project, not affiliated with or endorsed by SEGA.");
        col.addView(ver);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(col);
        /* Android 15 and later draw an app under the status and navigation
         * bars: keep the text out from under them (and the camera's notch). */
        scroll.setOnApplyWindowInsetsListener((v, in) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets b = in.getInsets(WindowInsets.Type.systemBars()
                                                         | WindowInsets.Type.displayCutout());
                v.setPadding(b.left, b.top, b.right, b.bottom);
            } else {
                v.setPadding(in.getSystemWindowInsetLeft(), in.getSystemWindowInsetTop(),
                             in.getSystemWindowInsetRight(), in.getSystemWindowInsetBottom());
            }
            return in;
        });
        setContentView(scroll);
    }

    private void pickIso() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, PICK_ISO);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ISO || result != RESULT_OK || data == null || data.getData() == null)
            return;
        unpack(data.getData());
    }

    private void unpack(Uri uri) {
        final ParcelFileDescriptor pfd;
        try {
            pfd = getContentResolver().openFileDescriptor(uri, "r");
        } catch (Exception e) {
            status("Could not open that file: " + e.getMessage());
            return;
        }
        if (pfd == null) {
            status("Could not open that file.");
            return;
        }
        /* The unpacker takes the picked file as "fd:N": a picker hands out a
         * descriptor, not a path. */
        final String iso = "fd:" + pfd.getFd();
        String wrong = nativeCheckImage(iso);
        if (wrong != null) {
            close(pfd);
            status(wrong);
            return;
        }
        long need = pfd.getStatSize() + 200L * 1024 * 1024;
        long free = getExternalFilesDir(null).getUsableSpace();
        if (pfd.getStatSize() > 0 && free < need) {
            close(pfd);
            status(String.format("Not enough free space: unpacking needs about %.1f GB, "
                                 + "and there is %.1f GB.", need / 1e9, free / 1e9));
            return;
        }
        final File dest = gameDir();
        final File tmp = new File(dest.getParentFile(), "game.unpacking");
        deleteTree(tmp);
        mBusy = true;
        mCancel = false;
        mPick.setEnabled(false);
        mCheck.setEnabled(false);
        mBar.setProgress(0);
        mBar.setVisibility(View.VISIBLE);
        status("Unpacking... (a few minutes; keep the app open)");
        new Thread(() -> {
            String err = nativeUnpack(iso, tmp.getAbsolutePath());
            close(pfd);
            if (err == null && !isGame(tmp))
                err = "The image was unpacked, but it has no default.xbe and Media in it: "
                    + "it is not this game.";
            if (err == null) {
                deleteTree(dest);
                if (!tmp.renameTo(dest)) err = "Could not move the unpacked game into place.";
            }
            if (err != null) deleteTree(tmp);
            final String message = err;
            runOnUiThread(() -> {
                mBusy = false;
                mBar.setVisibility(View.GONE);
                mPick.setEnabled(true);
                mCheck.setEnabled(true);
                if (message == null) startGame();
                else status(message);
            });
        }, "jsrf-unpack").start();
    }

    /** Called by nativeUnpack, on the unpacking thread. false stops it. */
    boolean onUnpackProgress(long done, long total) {
        final int p = total > 0 ? (int) (done * 1000 / total) : 0;
        runOnUiThread(() -> {
            mBar.setProgress(p);
            status(String.format("Unpacking... %d%%", p / 10));
        });
        return !mCancel;
    }

    @Override
    public void onBackPressed() {
        if (mBusy) {
            mCancel = true;
            status("Stopping...");
            return;
        }
        super.onBackPressed();
    }

    private void status(String s) {
        mStatus.setText(s);
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private static void close(ParcelFileDescriptor p) {
        try {
            p.close();
        } catch (Exception e) {
            /* nothing to do */
        }
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null)
            for (File k : kids) deleteTree(k);
        f.delete();
    }
}

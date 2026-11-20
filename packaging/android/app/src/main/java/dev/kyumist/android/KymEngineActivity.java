package dev.kyumist.android;

import android.app.NativeActivity;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

public class KymEngineActivity extends NativeActivity {
    static {
        System.loadLibrary("kymengine_android");
    }

    private static native void nativeSetAndroidContext(
            String gameDataDir,
            Class<?> nativeBridgeClass,
            Class<?> scriptHostClass
    );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        File gameDir = new File(getFilesDir(), "kym_game");
        extractAssetsIfNeeded(gameDir);

        nativeSetAndroidContext(
                gameDir.getAbsolutePath(),
                dev.kyumist.NativeBridge.class,
                dev.kyumist.ScriptHost.class
        );

        super.onCreate(savedInstanceState);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                );
            }
        } else {
            View decor = getWindow().getDecorView();
            if (decor != null) {
                decor.setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_FULLSCREEN
                );
            }
        }
    }

    private void extractAssetsIfNeeded(File gameDir) {
        String currentStamp = computeBuildStamp();
        File stampFile = new File(gameDir, ".kym_extract_stamp");
        File shadersDir = new File(gameDir, "shaders");

        if (shadersDir.exists() && stampFile.exists()) {
            String savedStamp = readSmallFile(stampFile);
            if (currentStamp.equals(savedStamp)) {
                return;
            }
        }

        if (!gameDir.exists()) {
            gameDir.mkdirs();
        }
        copyAssetsToDir("", gameDir);
        writeSmallFile(stampFile, currentStamp);
    }

    @SuppressWarnings("deprecation")
    private String computeBuildStamp() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            long versionCode = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                    ? pi.getLongVersionCode()
                    : pi.versionCode;
            return versionCode + ":" + pi.lastUpdateTime + ":" + pi.firstInstallTime;
        } catch (Throwable ignored) {
            return "1";
        }
    }

    private static String readSmallFile(File f) {
        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line = br.readLine();
            return line != null ? line.trim() : "";
        } catch (IOException ignored) {
            return "";
        }
    }

    private static void writeSmallFile(File f, String content) {
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(content.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }

    private void copyAssetsToDir(String assetPath, File outDir) {
        AssetManager am = getAssets();
        try {
            String[] list = am.list(assetPath);
            if (list == null || list.length == 0) {
                if (assetPath.isEmpty()) {
                    return;
                }
                File parent = outDir.getParentFile();
                if (parent != null && !parent.exists()) {
                    parent.mkdirs();
                }
                try (InputStream in = am.open(assetPath);
                     FileOutputStream out = new FileOutputStream(outDir)) {
                    byte[] buf = new byte[16384];
                    int len;
                    while ((len = in.read(buf)) > 0) {
                        out.write(buf, 0, len);
                    }
                }
            } else {
                if (!outDir.exists()) {
                    outDir.mkdirs();
                }
                for (String entry : list) {
                    String nextAsset = assetPath.isEmpty() ? entry : (assetPath + "/" + entry);
                    copyAssetsToDir(nextAsset, new File(outDir, entry));
                }
            }
        } catch (IOException ignored) {
        }
    }
}

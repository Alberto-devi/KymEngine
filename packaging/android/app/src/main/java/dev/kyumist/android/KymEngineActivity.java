package dev.kyumist.android;

import android.app.NativeActivity;
import android.content.res.AssetManager;
import android.os.Bundle;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class KymEngineActivity extends NativeActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        copyAssetsToDir("", new File(getFilesDir(), "kym_game"));
        super.onCreate(savedInstanceState);
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
                    byte[] buf = new byte[8192];
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


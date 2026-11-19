package dev.kyumist;

public final class Audio {
    private Audio() {}

    public static void playSound(String path) {
        NativeBridge.playSound(path, 1.0f);
    }

    public static void playSound(String path, float volume) {
        NativeBridge.playSound(path, volume);
    }
}


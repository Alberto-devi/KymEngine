package dev.kyumist;

public final class UI {
    private UI() {}

    public static boolean loadDocument(String relativePath) {
        return NativeBridge.uiLoadDocument(relativePath);
    }

    public static void unloadDocument(String relativePath) {
        NativeBridge.uiUnloadDocument(relativePath);
    }

    public static boolean setText(String elementId, String text) {
        return NativeBridge.uiSetText(elementId, text);
    }

    public static boolean setVisible(String elementId, boolean visible) {
        return NativeBridge.uiSetVisible(elementId, visible);
    }

    public static boolean setClass(String elementId, String className, boolean active) {
        return NativeBridge.uiSetClass(elementId, className, active);
    }
}


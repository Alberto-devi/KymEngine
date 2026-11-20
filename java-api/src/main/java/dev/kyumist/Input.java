package dev.kyumist;

public final class Input {
    private static final float[] mouseBuf = new float[2];

    private Input() {}

    public static final int KEY_W = 87;
    public static final int KEY_A = 65;
    public static final int KEY_S = 83;
    public static final int KEY_D = 68;
    public static final int KEY_Q = 81;
    public static final int KEY_E = 69;
    public static final int KEY_R = 82;
    public static final int KEY_F = 70;
    public static final int KEY_SPACE = 32;
    public static final int KEY_ESCAPE = 256;
    public static final int KEY_ENTER = 257;
    public static final int KEY_LEFT_SHIFT = 340;
    public static final int KEY_LEFT_CTRL = 341;
    public static final int KEY_RIGHT = 262;
    public static final int KEY_LEFT = 263;
    public static final int KEY_DOWN = 264;
    public static final int KEY_UP = 265;

    public static final int MOUSE_LEFT = 0;
    public static final int MOUSE_RIGHT = 1;
    public static final int MOUSE_MIDDLE = 2;

    public static boolean isKeyDown(int keyCode) {
        return NativeBridge.isKeyDown(keyCode);
    }

    public static boolean isMouseButtonDown(int button) {
        return NativeBridge.isMouseButtonDown(button);
    }

    public static float getMouseX() {
        NativeBridge.getMousePosition(mouseBuf);
        return mouseBuf[0];
    }

    public static float getMouseY() {
        NativeBridge.getMousePosition(mouseBuf);
        return mouseBuf[1];
    }

    public static boolean isActionPressed(String actionName) {
        return NativeBridge.isActionPressed(actionName);
    }

    public static boolean isActionJustPressed(String actionName) {
        return NativeBridge.isActionJustPressed(actionName);
    }

    public static boolean isActionJustReleased(String actionName) {
        return NativeBridge.isActionJustReleased(actionName);
    }

    public static float getAxis(String negativeAction, String positiveAction) {
        return NativeBridge.getAxis(negativeAction, positiveAction);
    }

    public static float getTouchAxisX() {
        return NativeBridge.getTouchAxisX();
    }

    public static float getTouchAxisY() {
        return NativeBridge.getTouchAxisY();
    }

    public static void setMouseCaptured(boolean captured) {
        NativeBridge.setMouseCaptured(captured);
    }
}

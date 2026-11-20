package dev.kyumist;

public final class NativeBridge {
    private NativeBridge() {}

    public static native void logInfo(String message);
    public static native void logWarn(String message);
    public static native void logError(String message);

    public static native String getName(long entityId);
    public static native void setName(long entityId, String name);
    public static native long findEntityByName(String name);
    public static native long spawnCube(String name, float x, float y, float z);
    public static native long spawnPrefab(String relativePrefabPath, float x, float y, float z);
    public static native void destroyEntity(long entityId);
    public static native void loadScene(String relativeScenePath);

    public static native void getPosition(long entityId, float[] outXYZ);
    public static native void setPosition(long entityId, float x, float y, float z);

    public static native void getRotation(long entityId, float[] outXYZ);
    public static native void setRotation(long entityId, float x, float y, float z);

    public static native void getScale(long entityId, float[] outXYZ);
    public static native void setScale(long entityId, float x, float y, float z);

    public static native void setColor(long entityId, float r, float g, float b, float a);
    public static native void setUIText(long entityId, String text);

    public static native boolean isKeyDown(int keyCode);
    public static native boolean isMouseButtonDown(int button);
    public static native void getMousePosition(float[] outXY);
    public static native boolean isActionPressed(String actionName);
    public static native boolean isActionJustPressed(String actionName);
    public static native boolean isActionJustReleased(String actionName);
    public static native float getAxis(String negativeAction, String positiveAction);
    public static native float getTouchAxisX();
    public static native float getTouchAxisY();
    public static native void setMouseCaptured(boolean captured);

    public static native boolean uiLoadDocument(String relativePath);
    public static native void uiUnloadDocument(String relativePath);
    public static native boolean uiSetText(String elementId, String text);
    public static native boolean uiSetVisible(String elementId, boolean visible);
    public static native boolean uiSetClass(String elementId, String className, boolean active);

    public static native void getVelocity(long entityId, float[] outXYZ);
    public static native void setVelocity(long entityId, float vx, float vy, float vz);
    public static native void getAngularVelocity(long entityId, float[] outXYZ);
    public static native void setAngularVelocity(long entityId, float wx, float wy, float wz);
    public static native void applyImpulse(long entityId, float ix, float iy, float iz);
    public static native long raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance);

    public static native void playSound(String relativePath, float volume);

    public static native void playAnimation(long entityId, String animationName, boolean loop);
    public static native void stopAnimation(long entityId);
    public static native void setAnimationSpeed(long entityId, float speed);
    public static native boolean isAnimationPlaying(long entityId);
}

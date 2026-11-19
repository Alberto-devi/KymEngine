package dev.kyumist;

public final class Entity {
    private final long id;
    private final float[] buffer = new float[3];

    public Entity(long id) {
        this.id = id;
    }

    public long getId() {
        return id;
    }

    public String getName() {
        return NativeBridge.getName(id);
    }

    public void setName(String name) {
        NativeBridge.setName(id, name);
    }

    public void getPosition(float[] outXYZ) {
        NativeBridge.getPosition(id, outXYZ);
    }

    public float getX() {
        NativeBridge.getPosition(id, buffer);
        return buffer[0];
    }

    public float getY() {
        NativeBridge.getPosition(id, buffer);
        return buffer[1];
    }

    public float getZ() {
        NativeBridge.getPosition(id, buffer);
        return buffer[2];
    }

    public void setPosition(float x, float y, float z) {
        NativeBridge.setPosition(id, x, y, z);
    }

    public void translate(float dx, float dy, float dz) {
        NativeBridge.getPosition(id, buffer);
        NativeBridge.setPosition(id, buffer[0] + dx, buffer[1] + dy, buffer[2] + dz);
    }

    public void getRotation(float[] outXYZ) {
        NativeBridge.getRotation(id, outXYZ);
    }

    public void setRotation(float rx, float ry, float rz) {
        NativeBridge.setRotation(id, rx, ry, rz);
    }

    public void rotate(float drx, float dry, float drz) {
        NativeBridge.getRotation(id, buffer);
        NativeBridge.setRotation(id, buffer[0] + drx, buffer[1] + dry, buffer[2] + drz);
    }

    public void getScale(float[] outXYZ) {
        NativeBridge.getScale(id, outXYZ);
    }

    public void setScale(float sx, float sy, float sz) {
        NativeBridge.setScale(id, sx, sy, sz);
    }

    public void setColor(float r, float g, float b, float a) {
        NativeBridge.setColor(id, r, g, b, a);
    }

    public void setUIText(String text) {
        NativeBridge.setUIText(id, text);
    }

    public void getVelocity(float[] outXYZ) {
        NativeBridge.getVelocity(id, outXYZ);
    }

    public void setVelocity(float vx, float vy, float vz) {
        NativeBridge.setVelocity(id, vx, vy, vz);
    }

    public void getAngularVelocity(float[] outXYZ) {
        NativeBridge.getAngularVelocity(id, outXYZ);
    }

    public void setAngularVelocity(float wx, float wy, float wz) {
        NativeBridge.setAngularVelocity(id, wx, wy, wz);
    }

    public void applyImpulse(float ix, float iy, float iz) {
        NativeBridge.applyImpulse(id, ix, iy, iz);
    }

    public void destroy() {
        NativeBridge.destroyEntity(id);
    }
}

package dev.kyumist;

public abstract class KyumistScript {
    protected Entity entity;

    public final void internalAttach(long entityId) {
        this.entity = new Entity(entityId);
    }

    public void onCreate() {}

    public void onUpdate(float dt) {}

    public void onCollisionEnter(Entity other) {}

    public void onUIEvent(String elementId, String eventType) {}

    public void onDestroy() {}

    protected void loadScene(String relativeScenePath) {
        NativeBridge.loadScene(relativeScenePath);
    }

    protected Entity spawnPrefab(String relativePrefabPath, float x, float y, float z) {
        long id = NativeBridge.spawnPrefab(relativePrefabPath, x, y, z);
        return (id >= 0) ? new Entity(id) : null;
    }

    protected Entity findEntity(String name) {
        long id = NativeBridge.findEntityByName(name);
        return (id >= 0) ? new Entity(id) : null;
    }

    protected Entity spawnCube(String name, float x, float y, float z) {
        long id = NativeBridge.spawnCube(name, x, y, z);
        return (id >= 0) ? new Entity(id) : null;
    }

    protected Entity raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance) {
        long id = NativeBridge.raycast(ox, oy, oz, dx, dy, dz, maxDistance);
        return (id >= 0) ? new Entity(id) : null;
    }

    protected void logInfo(String message) {
        NativeBridge.logInfo(message);
    }

    protected void logWarn(String message) {
        NativeBridge.logWarn(message);
    }

    protected void logError(String message) {
        NativeBridge.logError(message);
    }
}

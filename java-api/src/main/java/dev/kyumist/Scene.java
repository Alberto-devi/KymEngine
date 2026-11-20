package dev.kyumist;

public final class Scene {
    private Scene() {}

    public static void load(String relativeScenePath) {
        NativeBridge.loadScene(relativeScenePath);
    }

    public static Entity spawnPrefab(String relativePrefabPath, float x, float y, float z) {
        long id = NativeBridge.spawnPrefab(relativePrefabPath, x, y, z);
        return (id >= 0) ? new Entity(id) : null;
    }
}


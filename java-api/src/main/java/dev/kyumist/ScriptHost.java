package dev.kyumist;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;

public final class ScriptHost {
    private static URLClassLoader classLoader;
    private static final Map<Long, KyumistScript> instances = new HashMap<>();

    private ScriptHost() {}

    public static boolean reloadClassLoader(String classesDirPath) {
        destroyAll();
        try {
            if (classLoader != null) {
                classLoader.close();
                classLoader = null;
            }
            Path dir = Paths.get(classesDirPath);
            if (!Files.exists(dir)) {
                NativeBridge.logWarn("Scripts build directory does not exist: " + classesDirPath);
                return false;
            }
            URL url = dir.toUri().toURL();
            classLoader = new URLClassLoader(new URL[]{url}, ScriptHost.class.getClassLoader());
            return true;
        } catch (Throwable t) {
            NativeBridge.logError("Failed to reload Java URLClassLoader: " + t.getMessage());
            return false;
        }
    }

    public static boolean createInstance(long entityId, String className) {
        if (classLoader == null || className == null || className.isEmpty()) {
            return false;
        }
        try {
            Class<?> clazz = Class.forName(className, true, classLoader);
            Object obj = clazz.getDeclaredConstructor().newInstance();
            if (!(obj instanceof KyumistScript script)) {
                NativeBridge.logError("Class '" + className + "' does not extend dev.kyumist.KyumistScript");
                return false;
            }
            script.internalAttach(entityId);
            instances.put(entityId, script);
            script.onCreate();
            return true;
        } catch (Throwable t) {
            NativeBridge.logError("Error instantiating script '" + className + "': " + t.getMessage());
            return false;
        }
    }

    public static void updateInstance(long entityId, float dt) {
        KyumistScript script = instances.get(entityId);
        if (script == null) {
            return;
        }
        try {
            script.onUpdate(dt);
        } catch (Throwable t) {
            NativeBridge.logError("Exception in onUpdate for entity " + entityId + ": " + t.getMessage());
        }
    }

    public static void dispatchCollision(long entityId, long otherEntityId) {
        KyumistScript script = instances.get(entityId);
        if (script == null || otherEntityId < 0) {
            return;
        }
        try {
            script.onCollisionEnter(new Entity(otherEntityId));
        } catch (Throwable t) {
            NativeBridge.logError("Exception in onCollisionEnter for entity " + entityId + ": " + t.getMessage());
        }
    }

    public static void destroyInstance(long entityId) {
        KyumistScript script = instances.remove(entityId);
        if (script == null) {
            return;
        }
        try {
            script.onDestroy();
        } catch (Throwable t) {
            NativeBridge.logError("Exception in onDestroy for entity " + entityId + ": " + t.getMessage());
        }
    }

    public static void destroyAll() {
        for (Map.Entry<Long, KyumistScript> entry : instances.entrySet()) {
            try {
                entry.getValue().onDestroy();
            } catch (Throwable t) {
                NativeBridge.logError("Exception in onDestroy for entity " + entry.getKey() + ": " + t.getMessage());
            }
        }
        instances.clear();
    }
}

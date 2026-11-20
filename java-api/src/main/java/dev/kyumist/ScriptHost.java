package dev.kyumist;

import java.io.Closeable;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;

public final class ScriptHost {
    private static ClassLoader classLoader;
    private static final Map<Long, KyumistScript> instances = new HashMap<>();

    private ScriptHost() {}

    private static void closeCurrentClassLoader() {
        if (classLoader instanceof Closeable) {
            try {
                ((Closeable) classLoader).close();
            } catch (Throwable ignored) {
            }
        }
        classLoader = null;
    }

    public static boolean reloadClassLoader(String classesDirPath) {
        destroyAll();
        try {
            closeCurrentClassLoader();
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

    public static boolean reloadDexClassLoader(String dexFilePath) {
        destroyAll();
        try {
            closeCurrentClassLoader();
            if (dexFilePath == null || dexFilePath.isEmpty()) {
                NativeBridge.logWarn("Empty DEX path provided to reloadDexClassLoader.");
                return false;
            }
            Path dexPath = Paths.get(dexFilePath);
            if (!Files.exists(dexPath)) {
                NativeBridge.logWarn("Scripts DEX file does not exist: " + dexFilePath);
                return false;
            }
            byte[] bytes = Files.readAllBytes(dexPath);
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            Class<?> dexLoaderCls = Class.forName("dalvik.system.InMemoryDexClassLoader");
            classLoader = (ClassLoader) dexLoaderCls
                    .getConstructor(ByteBuffer.class, ClassLoader.class)
                    .newInstance(buffer, ScriptHost.class.getClassLoader());
            NativeBridge.logInfo("Loaded Android DEX scripts via InMemoryDexClassLoader (" + bytes.length + " bytes).");
            return true;
        } catch (Throwable t) {
            NativeBridge.logError("Failed to reload Android DEX ClassLoader: " + t.getMessage());
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
            if (!(obj instanceof KyumistScript)) {
                NativeBridge.logError("Class '" + className + "' does not extend dev.kyumist.KyumistScript");
                return false;
            }
            KyumistScript script = (KyumistScript) obj;
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

    public static void dispatchUIEvent(String elementId, String eventType) {
        for (Map.Entry<Long, KyumistScript> entry : instances.entrySet()) {
            try {
                entry.getValue().onUIEvent(elementId, eventType);
            } catch (Throwable t) {
                NativeBridge.logError("Exception in onUIEvent for entity " + entry.getKey() + ": " + t.getMessage());
            }
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

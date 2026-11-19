# Kyumist Engine — Especificaciones Técnicas y Plan de Arquitectura

> **Versión:** 0.1 (Borrador Inicial)  
> **Lenguaje Principal:** C++20  
> **Scripting:** Java (JDK 21 LTS+ embebido vía JNI)  
> **API Gráfica:** Vulkan 1.3  
> **Alcance:** Motor híbrido 2D y 3D con Editor visual integrado  

---

## 1. Visión General

**Kyumist Engine** es un motor gráfico y de videojuegos multiplataforma (Linux / Windows) diseñado en torno a tres pilares:
1. **Núcleo en C++20:** Alto rendimiento, control explícito de memoria y arquitectura orientada a datos (ECS).
2. **Renderizador Vulkan 1.3:** Uso de *Dynamic Rendering* (`VK_KHR_dynamic_rendering`) y *Synchronization2* para un pipeline moderno y limpio, capaz de renderizar tanto **2D** (sprites, tilemaps, texto, luces 2D por lotes) como **3D** (mallas PBR, iluminación, sombras y post-procesado).
3. **Scripting en Java:** Lógica de gameplay escrita en Java con compilación incremental y *Hot-Reload* desde el editor sin reiniciar el motor.

---

## 2. Stack Tecnológico Seleccionado

| Módulo | Herramienta / Librería | Propósito |
| :--- | :--- | :--- |
| **Compilación y Paquetes** | **CMake 3.24+** + **Ninja** + **vcpkg** | Build multiplataforma y gestión automática de dependencias (`vcpkg.json`). |
| **Ventana e Input** | **GLFW 3.4** | Creación de ventana, eventos de teclado/ratón/gamepad y superficie Vulkan. |
| **Backend Vulkan** | **Vulkan SDK 1.3+**, **volk**, **vk-bootstrap**, **VMA** | Carga dinámica de funciones (`volk`), inicialización limpia (`vk-bootstrap`) y gestión de memoria GPU (`Vulkan Memory Allocator`). |
| **Shaders** | **GLSL / HLSL** + **shaderc** + **SPIRV-Cross** | Compilación de shaders a SPIR-V en tiempo de ejecución/build y reflexión automática de *uniforms* para el editor. |
| **Matemáticas** | **GLM** | Vectores, matrices y cuaterniones (`GLM_FORCE_DEPTH_ZERO_TO_ONE` para Vulkan). |
| **UI del Editor** | **Dear ImGui (`docking`)** + **ImGuizmo** + **NFD-Extended** | Ventanas acoplables, inspector, jerarquía, consola, gizmos 3D/2D y diálogos nativos de archivos. |
| **Escena / ECS** | **EnTT** | Entity Component System de alto rendimiento para entidades 2D y 3D. |
| **Serialización** | **nlohmann/json** | Guardado/carga de proyectos (`.kmproj`), escenas (`.kmscene`) y materiales (`.kmmat`). |
| **Assets 3D y Texturas** | **fastgltf**, **stb_image**, **meshoptimizer** | Carga rápida de modelos glTF 2.0, imágenes (PNG/JPG/HDR) y optimización de mallas. |
| **Físicas** | **Jolt Physics** (3D) + **Box2D v3** (2D) | Simulación física dedicada y optimizada según la dimensión. |
| **Audio** | **miniaudio** | Reproducción de audio 2D y espacial 3D en un solo header sin dependencias externas. |
| **Scripting** | **JDK 21 LTS+** (JNI Invocation API + ZGC) | Ejecución de scripts de usuario en Java con *Hot-Reload* mediante `URLClassLoader`. |
| **Logging y Profiling** | **spdlog**, **Tracy Profiler**, **RenderDoc** | Logs en consola/editor, medición de tiempos CPU/GPU y depuración de frames Vulkan. |
| **Tests** | **Catch2** | Pruebas unitarias para matemáticas, serialización y sistemas del núcleo. |

---

## 3. Arquitectura del Proyecto y Estructura de Carpetas

El proyecto se divide en módulos desacoplados para separar el **Runtime** (el juego final exportado) del **Editor** (las herramientas visuales):

```text
Kyumist Engine/
├── CMakeLists.txt              # Configuración raíz de CMake
├── Documentacion/              # Documentación técnica y hoja de ruta
│   ├── SPECIFICATIONS.md       # Este documento
│   └── ROADMAP.md              # Estado actual y tareas pendientes por fase
├── assets/                     # Shaders internos, iconos y fuentes del editor
│   └── shaders/
├── engine/                     # Librería estática/compartida del motor (Runtime)
│   ├── core/                   # App loop, Timestep, Logging, Eventos, VFS/Archivos
│   ├── renderer/               # Abstracción Vulkan (Device, Swapchain, Pipelines, 2D/3D)
│   ├── scene/                  # ECS (EnTT), Componentes 2D/3D, Cámaras, Serialización
│   ├── physics/                # Wrappers para Jolt (3D) y Box2D (2D)
│   ├── audio/                  # Wrapper de miniaudio
│   └── scripting/              # Puente C++ <-> JVM (JNI, manejo de clases, hot-reload)
├── editor/                     # Ejecutable del Editor visual (enlaza con engine/)
│   ├── panels/                 # Viewport, Hierarchy, Inspector, ContentBrowser, Console
│   ├── gizmos/                 # ImGuizmo y selección de entidades (Mouse Picking)
│   └── java_compiler/          # Invocación de javac/Gradle y recarga en caliente
├── java-api/                   # Proyecto Java con el SDK para los scripts del usuario
│   └── src/main/java/dev/kyumist/
│       ├── KyumistScript.java  # Clase base (onCreate, onUpdate, onDestroy)
│       ├── Entity.java         # Handle numérico (long id) + acceso a componentes
│       ├── Input.java
│       └── Math/
├── runtime/                    # Ejecutable ligero para juegos exportados (sin editor)
└── tests/                      # Tests unitarios (Catch2)
```

---

## 4. Diseño del Renderizador (Vulkan 1.3 — 2D y 3D)

### Principios Clave
* **Dynamic Rendering:** Se evita el boilerplate clásico de `VkRenderPass` / `VkFramebuffer` usando Vulkan 1.3 (`vkCmdBeginRendering`).
* **Renderizado Offscreen para el Editor:** El motor renderiza la escena a una textura/imagen Vulkan (`VkImage`) que luego se muestra dentro de una ventana de **Dear ImGui** (Viewport). Al compilar con `runtime/`, se dibuja directo a la *Swapchain*.
* **Frames in Flight:** Doble buffering de *command buffers* y sincronización (2 frames en vuelo) para mantener la GPU ocupada sin bloquear la CPU.

### Pipeline Híbrido 2D / 3D
1. **Renderer 3D:**
   * Carga de mallas glTF 2.0, materiales **PBR** (Albedo, Normal, Metallic/Roughness, Emissive).
   * Luces direccionales (con *Shadow Mapping* básico) y puntuales.
   * Cámara perspectiva con controles de editor (Flycam / Orbit).
2. **Renderer 2D (Batch Renderer):**
   * *Batching* dinámico de quads/sprites en un único *draw call* (reduciendo cambios de estado en Vulkan) usando arreglos de texturas (*bindless* o *descriptor indexing*).
   * Soporte de capas (*Z-order / sorting layer*), cámara ortográfica y texto (MSDF o bitmap atlas).

---

## 5. Diseño del Puente de Scripting (C++ ↔ Java)

Para evitar cuellos de botella entre C++ y la JVM:

1. **JVM Embebida:** `ScriptEngine` inicializa la JVM al arrancar el editor o runtime usando `JNI_CreateJavaVM` con **ZGC** (`-XX:+UseZGC -XX:+ZGenerational`) para minimizar pausas del Garbage Collector.
2. **Handles Numéricos en lugar de Objetos Pesados:** Cada entidad en Java referencia su ID de `EnTT` como un `long`. No se duplican grafos de escena en el heap de Java.
3. **Minimizar cruces JNI por frame:**
   * Evitar crear objetos `Vector3` temporales en `onUpdate(float dt)`; preferir métodos con primitivos (`transform.setPosition(x, y, z)`) o buffers directos compartidos (`ByteBuffer.allocateDirect`).
4. **Hot-Reload en el Editor:**
   * Los scripts compilados del proyecto del usuario se cargan mediante un `URLClassLoader` aislado.
   * Al pulsar **Recompile** (o detectar cambios en `.java`), se ejecuta `javac`, se serializa el estado de los scripts activos, se descarta el `ClassLoader` anterior y se instancia el nuevo sin cerrar el editor.

---

## 6. Hoja de Ruta (Roadmap por Fases)

### Fase 0 — Cimientos y Build System
* Configurar `CMakeLists.txt` raíz + `vcpkg.json` + estructura de carpetas.
* Crear ventana con **GLFW** + sistema de logging (**spdlog**) + bucle principal con *delta time*.

### Fase 1 — Núcleo Vulkan
* Inicializar Instance, Physical/Logical Device y Swapchain con **vk-bootstrap**, **volk** y **VMA**.
* Compilar shaders GLSL a SPIR-V con **shaderc**.
* Renderizar el primer triángulo y un quad texturizado en pantalla usando *Dynamic Rendering*.

### Fase 2 — Editor Base (Dear ImGui)
* Integrar **Dear ImGui** (con *docking*) sobre Vulkan.
* Renderizar la escena a una textura *offscreen* y mostrarla en el panel **Viewport**.
* Añadir paneles básicos: **Console** (conectado a `spdlog`), **Stats** (FPS / frame time) y **Viewport**.

### Fase 3 — Escena ECS, 2D y 3D
* Integrar **EnTT** y serialización JSON (`.kmscene`).
* Implementar el **Batch Renderer 2D** (sprites, cámara ortográfica).
* Implementar carga de modelos 3D (**fastgltf**) + iluminación PBR básica + **ImGuizmo** en el Viewport.
* Añadir paneles **Hierarchy**, **Inspector** y **Content Browser**.

### Fase 4 — Scripting en Java
* Integrar la **JVM** vía JNI en `engine/scripting/`.
* Crear el SDK base en `java-api/` (`KyumistScript`, `Entity`, `Input`).
* Implementar compilación automática y **Hot-Reload** con `URLClassLoader` desde el Editor.

### Fase 5 — Físicas, Audio y Runtime Independiente
* Integrar **Jolt Physics** (3D), **Box2D v3** (2D) y **miniaudio**.
* Exponer APIs de físicas y audio a Java.
* Crear el ejecutable `runtime/` y un flujo básico de exportación de proyecto (empaquetando assets + JRE mínimo con `jlink`).


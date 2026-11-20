# Kyumist Engine — Hoja de Ruta y Progreso

> **Última actualización:** 2026-11-18  
> **Estado actual:** Todas las fases base completadas (Fases 0 a 5 implementadas y verificadas)

Este documento resume lo que ya está implementado y verificado en **Kyumist Engine**, junto con posibles ampliaciones futuras.

---

## Resumen de Estado

| Fase | Nombre | Estado |
| :--- | :--- | :--- |
| **Fase 0** | Cimientos, Build System, Logging y Ventana Base | ✅ Completada |
| **Fase 1** | Núcleo Vulkan 1.3, VMA, Swapchain y Primer Triángulo | ✅ Completada |
| **Fase 2** | Editor Base (Dear ImGui + Viewport Offscreen) | ✅ Completada |
| **Fase 3** | Escena ECS, Renderer 2D/3D e ImGuizmo | ✅ Completada |
| **Fase 4** | Scripting en Java (JNI + Hot-Reload) | ✅ Completada |
| **Fase 5** | Físicas 3D/2D, Audio (`miniaudio`), `Runtime` y Tests | ✅ Completada |
| **Mejoras IDE** | **Project Manager**, **Script Editor Integrado** y **FileSystem (`res://`)** | ✅ Completada |

---

## Lo que ya hemos hecho

### ✅ Fase 0 — Cimientos y Build System
- [x] Estructura base de carpetas (`assets/shaders/`, `engine/*`, `editor/`, `java-api/`, `runtime/`, `tests/`, `Documentacion/`).
- [x] Configuración de **CMake 3.24+ (C++20) + Ninja** con `FetchContent` para descargar `spdlog` (`v1.14.1`), `glfw` (`3.4`) y `glm` (`1.0.1`).
- [x] Sistema de logging (`engine/core/Log.hpp` / `Log.cpp`) con loggers separados `KYUMIST` y `EDITOR`.
- [x] Wrapper de tiempo delta (`engine/core/Timestep.hpp`).
- [x] Ventana GLFW preparada para Vulkan (`engine/core/Window.hpp` / `Window.cpp` con `GLFW_NO_API`, detección de soporte Vulkan y callback de *resize*).
- [x] Bucle principal (`engine/core/Application.hpp` / `Application.cpp`) y ejecutable `editor/KyumistEditor` con soporte para `--headless` y `--max-frames`.

### ✅ Fase 1 — Núcleo Vulkan 1.3 y Primer Triángulo
- [x] Integración de `vk-bootstrap` (`v1.3.280`) y `VulkanMemoryAllocator (VMA)` (`v3.1.0`) en CMake.
- [x] Compilación automática de shaders GLSL (`assets/shaders/triangle.vert` y `triangle.frag`) a SPIR-V con `glslangValidator`.
- [x] Inicialización de Vulkan 1.3 (`engine/renderer/VulkanRenderer.hpp` / `VulkanRenderer.cpp`) con **Validation Layers** (`VK_LAYER_KHRONOS_validation`) conectadas a `spdlog`.
- [x] Selección de GPU Vulkan 1.3 con `dynamicRendering` y `synchronization2` activos.
- [x] Creación y recreación automática de la **Swapchain** al redimensionar la ventana (y target *offscreen* dedicado en modo `--headless`).
- [x] Pipeline gráfico usando **Dynamic Rendering** (`VkPipelineRenderingCreateInfo`, sin `VkRenderPass` ni `VkFramebuffer`).
- [x] Vertex Buffer en GPU gestionado con VMA y renderizado de triángulo RGB con doble buffering (`MAX_FRAMES_IN_FLIGHT = 2`).

### ✅ Fase 2 — Editor Base (Dear ImGui)
- [x] Integración de **Dear ImGui** (`v1.91.5-docking`) con backends `imgui_impl_glfw` + `imgui_impl_vulkan` (*Dynamic Rendering*).
- [x] Renderizado en dos pasadas: escena a textura *offscreen* (`VkImage` + `VkSampler` + descriptor ImGui) e interfaz del editor sobre la *Swapchain*.
- [x] Redimensionado dinámico del *Viewport* sin reiniciar el motor.
- [x] *DockSpace* preconfigurado con paneles **Viewport**, **Stats & Renderer** (GPU, FPS, *frame time*, selector de color de fondo) y **Console** (conectado a `EditorConsoleSink` en `spdlog` con colores y filtro).

### ✅ Fase 3 — Escena ECS, Renderer 2D/3D e ImGuizmo
- [x] Integración de **EnTT** (`v3.13.2`) en `engine/scene/` con componentes (`TagComponent`, `TransformComponent`, `MeshRendererComponent`, `SpriteRendererComponent`, `DirectionalLightComponent`, `CameraComponent`) y escena por defecto.
- [x] Serialización de escenas JSON (`.kmscene`) con **nlohmann/json** (`v3.11.3`) y escena inicial `assets/scenes/default.kmscene`.
- [x] **Depth Buffer (`VK_FORMAT_D32_SFLOAT`)**, *Push Constants* (128 bytes) y shaders híbridos `scene.vert` / `scene.frag`.
- [x] **Renderer 3D** con primitivas integradas (`Cube`, `Sphere`, `Plane`), iluminación direccional y cargador de mallas `.obj` (ejemplo `assets/models/pyramid.obj`).
- [x] **Batch Renderer 2D** para `SpriteRendererComponent` ordenado por `sortingOrder` con patrón sólido/checker.
- [x] Cámara interactiva **EditorCamera** (3D Perspectiva Flycam + 2D Ortográfica Pan/Zoom).
- [x] Paneles acoplables **Hierarchy**, **Inspector**, **Content Browser** y manipulación visual en el **Viewport** con **ImGuizmo** (Mover / Rotar / Escalar en Local/Global).

### ✅ Fase 4 — Scripting en Java
- [x] Integración de la **JVM** (OpenJDK 25) vía **JNI Invocation API** en `engine/scripting/ScriptEngine` con **ZGC** y autodetección de `JAVA_HOME` en CMake.
- [x] SDK en `java-api/` compilado en `kyumist-api.jar` (`NativeBridge`, `Entity` con handle `long`, `Input`, `KyumistScript`, `ScriptHost`).
- [x] Compilación incremental de `assets/scripts/*.java` con `javac` y **Hot-Reload** mediante `URLClassLoader` aislado.
- [x] Componente `JavaScriptComponent` en ECS + serialización JSON, botones **Play / Stop** (con snapshot/restauración de escena) y **Hot-Reload Java** en el Editor, más scripts de ejemplo (`RotatorScript`, `Oscillator2DScript`).

### ✅ Fase 5 — Físicas 3D/2D, Audio (`miniaudio`), Runtime y Tests (+ Obsidian UI)
- [x] Rediseño visual **Kyumist Obsidian UI** con fuentes `.ttf` (`Roboto-Medium`, `Cousine-Regular`), toolbar unificada, HUD en Viewport, `DrawVec3Control` (`X/Y/Z` coloreados), Content Browser en cuadrícula y Console monoespaciada.
- [x] Motor de físicas 3D/2D (`engine/physics/PhysicsSystem`) con `RigidBodyComponent` (`Static`, `Dynamic`, `Kinematic`; `Box3D`, `Sphere3D`, `Box2D`), gravedad, rebote y fricción.
- [x] Sistema de audio con **miniaudio** (`0.11.21`) en `engine/audio/AudioEngine`, `AudioSourceComponent` y generación automática de `assets/audio/bounce.wav`.
- [x] Expansión del SDK Java (`Audio.java`, `getVelocity`, `setVelocity`, `applyImpulse`, `playSound`) y paneles de Física/Audio en el Inspector y Content Browser.
- [x] Ejecutable independiente `runtime/KyumistRuntime` (carga `.kmscene` y ejecuta JVM + Físicas + Audio sin el editor) y suite de tests `tests/KyumistTests` registrada en `ctest`.

### ✅ Mejoras IDE — Project Manager, Script Editor Integrado y FileSystem (`res://`)
- [x] **Sistema de Proyectos (`engine/project/ProjectManager`)**: pantalla inicial **Kyumist Project Manager** (al estilo Unity Hub / Godot), lista persistente de proyectos recientes (`build/kyumist_projects.json`), selector visual de directorios integrado y creación de proyectos completos desde plantilla (`Híbrido 3D+2D`, `3D Básico`, `2D Básico`) generando `project.kmproj`, `scenes/main.kmscene`, `scripts/*.java`, `models/pyramid.obj`, `models/crystal_totem.gltf`, `materials/*.kmmat`, `audio/bounce.wav` y `.vscode/settings.json`.
- [x] **Editor de Código Java Integrado (`editor/panels/ScriptEditorPanel`)**: acoplado en el *DockSpace* junto al **Viewport** con pestañas de múltiples `.java`, barra lateral de scripts, creación rápida de scripts (`+ Nuevo Script`), inserción de *snippets* (`for`, `Rotar`, `WASD`, `Impulso`, `Audio`), fuente monoespaciada `Cousine-Regular`, **vista única editable y coloreada en tiempo real**, *gutter* con números de línea y errores de `javac`, **auto-tabulación inteligente** en bloques `{}` y **autocompletado contextual** (API `entity.*`/`Input.*`/`Audio.*`/`Math.*`, Java y símbolos locales) + **Guardar y Hot-Reload (Ctrl+S)**.
- [x] **Modelos glTF 2.0, Materiales PBR, Texturas PNG y Spritesheets (`engine/renderer/ModelLoader` y `VulkanRenderer`)**: carga nativa de modelos **glTF 2.0 (`.gltf` / `.glb`)** y **Wavefront `.obj` + `.mtl`**, materiales **`.kmmat`**, decodificador/codificador **`.png` (`zlib`)**, *descriptor sets* en Vulkan (`SceneLightsUBO` + `sampler2D uAlbedoTex`), muestras `textures/checker_albedo.png` y `textures/coin_spritesheet.png` y spritesheets animados (`hFrames`, `vFrames`, `animFps`).
- [x] **Iluminación Avanzada, Cámara de Juego, Jerarquía y UI In-Game**: **Skybox** procedural (`SkyboxComponent`), hasta **8 Luces Puntuales** (`PointLightComponent`), **sombras planas proyectadas** sobre el suelo (`castShadows`), jerarquía padre-hijo (`HierarchyComponent` + `GetWorldTransform`), **Prefabs (`.kmprefab`)**, **cámara activa en Play / `KyumistRuntime`** (`CameraComponent`), texto HUD (`UITextComponent`), físicas con **`isTrigger`**, **`angularVelocity`**, **`PhysicsSystem::Raycast`** y callbacks Java **`onCollisionEnter`**, **`spawnCube`**, **`findEntity`**, **`raycast`**, ratón en **`Input`** y **Exportador Standalone (`run_game.sh`)**.
- [x] **FileSystem (`res://`) estilo Godot (`EditorPanels`)**: árbol jerárquico de carpetas a la izquierda, barra de ruta *breadcrumb*, menú `+ Crear` (Escena, Material PBR, Script Java, Carpeta), botón **`+ Importar Asset...`**, menú contextual (clic derecho) e iconos vectoriales personalizados dibujados con `ImDrawList` para carpetas, `.kmscene`, `.java`, `.gltf`/`.glb`, `.kmmat`/`.mtl`, `.obj`, `.png`, `.kmprefab`, `.wav`, shaders y `.kmproj`.
- [x] Soporte CLI `--project <ruta>` en `editor/KyumistEditor` y opciones **Archivo -> Exportar Juego (Standalone)...** y **Archivo -> Cerrar Proyecto (Project Manager)**.
- [x] Configuración `.vscode/settings.json` con rutas de `java-api/src/main/java` y `build/java/kyumist-api.jar` para autocompletado externo en VS Code.
- [x] **Usabilidad del Editor y Viewport**: iluminación hemisférica y direccional más luminosa en `scene.frag` y escenas por defecto, **selección directa de entidades por clic (Ray Picking 3D)** en el Viewport y sistema **Undo / Redo (`Ctrl+Z` / `Ctrl+Shift+Z`)** independiente tanto en el Script Editor como en la Escena/Viewport/Inspector.
- [x] **Sección de Ejemplos de Proyectos (`ProjectManager`)**: eliminado el antiguo `Kyumist Demo Project` y añadida la sección **Ejemplos de Proyectos** con **SkyHop 2D** (Plataformas 2D), **Vortex Arena 3D** (Shooter 3D con *raycast*, colisiones físicas y sistema de vidas/daño contra drones) y **Aether Guardian** (Santuario Híbrido 3D+2D con Tótem glTF sólido, 3 orbes guardianes que quitan vida al chocar y runas 2D animadas), con botón de restauración en 1 clic.
- [x] **Ventana de Juego Independiente al pulsar `Play`**: al pulsar **Play** en el editor, se abre automáticamente una ventana separada de `KyumistRuntime` conectada al proyecto y a la escena actual **sin herramientas de edición** (cerrable con `Stop` o `ESC`), mientras que `PhysicsSystem` resuelve también colisiones entre cuerpos `Kinematic` y `Static`/`Kinematic`.
- [x] **Compatibilidad Multiplataforma 100 % (Linux y Windows)**: `ZLIB` automático por `FetchContent` si falta en el sistema, detección de `glslangValidator` desde `VULKAN_SDK`, lanzamiento/cierre nativo del proceso de juego (`CreateProcessA` en Windows y `fork`/`execl` en Linux), compilación Java vía `_popen`/`popen`, soporte `USERPROFILE`/`HOME` y exportación standalone con `run_game.bat` + `run_game.sh`.

### Guía rápida de compilación (Linux y Windows)
- **Linux**: `cmake -B build && cmake --build build -j` (requiere Vulkan SDK, OpenJDK, `zlib1g-dev` y cabeceras X11/Wayland para GLFW).
- **Windows (PowerShell / VS 2022)**: requiere **LunarG Vulkan SDK** (`VULKAN_SDK`), **OpenJDK 17+** (`JAVA_HOME`) y **CMake + Visual Studio C++**:
  ```powershell
  cmake -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -j
  $env:PATH = "$env:JAVA_HOME\bin\server;$env:JAVA_HOME\bin;$env:PATH"
  .\build\editor\Release\KyumistEditor.exe
  ```

---

## Fase 7 — v0.3.0 / v0.3.1: Input Map, UI HTML/CSS, Animación Esquelética glTF 2.0 y Exportación
- [x] **Sistema de `InputMap` (`input_map.json`)**: acciones configurables desde el editor y consultables en Java (`Input.isActionPressed`, `isActionJustPressed`, `isActionJustReleased`, `getAxis`).
- [x] **UI HTML/CSS con RmlUi**: carga de documentos `.html` + `.css` (`UI.loadDocument`, `setText`, `setVisible`, `setClass`) con eventos `onUIEvent(elementId, eventType)` en `KyumistScript`.
- [x] **Animación Esquelética glTF 2.0 (*Skinning*)**: parseo de `nodes`, `skins` (`JOINTS_0`, `WEIGHTS_0`, `inverseBindMatrices`), `animations` (`LINEAR`, `STEP`, `CUBICSPLINE`) y mallas emparentadas a huesos (`attachedNode`), componente `AnimatorComponent`, *vertex buffers* dinámicos por entidad en Vulkan y API Java (`Entity.playAnimation`, `stopAnimation`, `setAnimationSpeed`, `isAnimationPlaying`).
- [x] **Gestión de Escenas y Prefabs en Runtime**: `loadScene(...)` y `spawnPrefab(...)` con instanciación y activación inmediata de `JavaScriptComponent`.
- [x] **Controles Táctiles para Móvil (`TouchControlsOverlay`)**: joystick virtual + botones A/B configurables en `project.kmproj` (con modo `simulateInEditor` para probar en PC).
- [x] **Exportación Multiplataforma y Empaquetado**:
  - Paquetes `.deb`, **AppImage** (`packaging/build_appimage.sh`) e instaladores/bundles para **Linux x64** y **Windows x64**.
  - Exportación a **Android APK** (`packaging/android/`) con plantilla `NativeActivity`, inyección de `assets/` y firma automática con `zipalign` + `apksigner` (esquemas v2/v3).

---

## 🔜 Siguiente Paso Prioritario (v0.4.0) — Runtime Completo en Android NDK
- [ ] **Portar `kyumist_engine` a Android NDK**: reemplazar GLFW por `ANativeWindow` / `AInputEvent` y `VK_KHR_android_surface` en `Window` y `VulkanRenderer`, compilando las dependencias nativas (`VMA`, `RmlUi`, `Freetype`, `miniaudio`, `ImGui`) para `arm64-v8a` y `armeabi-v7a`.
- [ ] **Scripting Java sobre Android ART/DEX**: compilar los scripts `.java` del juego a `.dex` durante la exportación APK y cargarlos en runtime mediante `DexClassLoader`.

---

## Posibles Ampliaciones Futuras (Opcionales)
- [ ] Reemplazar el solver físico integrado por **Jolt Physics** (3D complejo/mallas) y **Box2D v3** (2D avanzado) si se requieren articulaciones o *raycasts* masivos.
- [ ] Integrar **Tracy Profiler** para instrumentación de frames CPU/GPU y empaquetado de distribución final con `jlink`.
- [ ] Añadir *Shadow Maps* en profundidad para sombras entre objetos 3D arbitrarios y mapas normales/metálicos por textura.


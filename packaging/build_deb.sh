#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-0.1.0}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
BUILD_DIR="$ROOT_DIR/build"
PKG_DIR="$BUILD_DIR/pkg_deb"
OUT_DIR="$BUILD_DIR/dist"
DEB_FILE="$OUT_DIR/kymengine_${VERSION}_${ARCH}.deb"

if [[ ! -x "$BUILD_DIR/editor/KyumistEditor" || ! -x "$BUILD_DIR/runtime/KyumistRuntime" ]]; then
  cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j
fi

rm -rf "$PKG_DIR"
mkdir -p \
  "$PKG_DIR/DEBIAN" \
  "$PKG_DIR/opt/kymengine/java" \
  "$PKG_DIR/usr/bin" \
  "$PKG_DIR/usr/share/applications" \
  "$PKG_DIR/usr/share/icons/hicolor/scalable/apps" \
  "$OUT_DIR"

cp "$BUILD_DIR/editor/KyumistEditor" "$PKG_DIR/opt/kymengine/"
cp "$BUILD_DIR/runtime/KyumistRuntime" "$PKG_DIR/opt/kymengine/"
cp "$BUILD_DIR/java/kyumist-api.jar" "$PKG_DIR/opt/kymengine/java/"
cp -r "$ROOT_DIR/assets" "$PKG_DIR/opt/kymengine/"
cp "$ROOT_DIR/LICENSE" "$PKG_DIR/opt/kymengine/"
chmod 0755 "$PKG_DIR/opt/kymengine/KyumistEditor" "$PKG_DIR/opt/kymengine/KyumistRuntime"

cat <<'EOF' > "$PKG_DIR/usr/bin/kymengine"
#!/usr/bin/env bash
if [[ -z "${JAVA_HOME:-}" ]]; then
  JAVAC_BIN="$(command -v javac || true)"
  if [[ -n "$JAVAC_BIN" ]]; then
    JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$JAVAC_BIN")")")"
    export JAVA_HOME
  fi
fi
if [[ -n "${JAVA_HOME:-}" && -d "$JAVA_HOME/lib/server" ]]; then
  export LD_LIBRARY_PATH="$JAVA_HOME/lib/server:${LD_LIBRARY_PATH:-}"
fi
exec /opt/kymengine/KyumistEditor "$@"
EOF
chmod 0755 "$PKG_DIR/usr/bin/kymengine"

cp "$ROOT_DIR/packaging/icons/kymengine.svg" "$PKG_DIR/usr/share/icons/hicolor/scalable/apps/kymengine.svg"

cat <<EOF > "$PKG_DIR/usr/share/applications/kymengine.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=KymEngine
Comment=Motor de juegos 3D/2D KymEngine (C++20, Vulkan 1.3 y Java)
Exec=/usr/bin/kymengine %f
Icon=kymengine
Terminal=false
Categories=Development;IDE;Game;
StartupNotify=true
EOF

INSTALLED_SIZE_KB="$(du -sk "$PKG_DIR/opt" "$PKG_DIR/usr" | awk '{sum+=$1} END {print sum}')"

cat <<EOF > "$PKG_DIR/DEBIAN/control"
Package: kymengine
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE_KB}
Depends: libvulkan1, libx11-6, zlib1g, libstdc++6, openjdk-25-jdk | openjdk-21-jdk | default-jdk
Maintainer: Alberto-devi <6ike.450@gmail.com>
Homepage: https://github.com/Alberto-devi/KymEngine
Description: KymEngine 3D/2D Game Engine (C++20, Vulkan 1.3 & Java)
 Motor de videojuegos multiplataforma 3D/2D con editor visual Vulkan,
 scripting en Java con Hot-Reload, fisicas 3D/2D y audio integrado.
EOF

chmod 0755 "$PKG_DIR/DEBIAN"
dpkg-deb --build --root-owner-group "$PKG_DIR" "$DEB_FILE"
echo "Paquete .deb generado en: $DEB_FILE"


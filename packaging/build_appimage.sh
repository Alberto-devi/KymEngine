#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-0.1.0}"
ARCH="x86_64"
BUILD_DIR="$ROOT_DIR/build"
APPDIR="$BUILD_DIR/KymEngine.AppDir"
OUT_DIR="$BUILD_DIR/dist"
APPIMAGE_FILE="$OUT_DIR/KymEngine-${VERSION}-${ARCH}.AppImage"

if [[ ! -x "$BUILD_DIR/editor/KyumistEditor" || ! -x "$BUILD_DIR/runtime/KyumistRuntime" ]]; then
  cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin/java" "$APPDIR/usr/lib" "$OUT_DIR"

cp "$BUILD_DIR/editor/KyumistEditor" "$APPDIR/usr/bin/"
cp "$BUILD_DIR/runtime/KyumistRuntime" "$APPDIR/usr/bin/"
cp "$BUILD_DIR/java/kyumist-api.jar" "$APPDIR/usr/bin/java/"
cp -r "$ROOT_DIR/assets" "$APPDIR/usr/bin/"
cp "$ROOT_DIR/LICENSE" "$APPDIR/usr/bin/"
chmod 0755 "$APPDIR/usr/bin/KyumistEditor" "$APPDIR/usr/bin/KyumistRuntime"

cp "$ROOT_DIR/packaging/icons/kymengine.svg" "$APPDIR/kymengine.svg"
cp "$ROOT_DIR/packaging/icons/kymengine.svg" "$APPDIR/.DirIcon"

cat <<'EOF' > "$APPDIR/kymengine.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=KymEngine
Comment=Motor de juegos 3D/2D KymEngine
Exec=KyumistEditor
Icon=kymengine
Terminal=false
Categories=Development;IDE;Game;
EOF

cat <<'EOF' > "$APPDIR/AppRun"
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:${PATH:-}"
if [[ -z "${JAVA_HOME:-}" ]]; then
  JAVAC_BIN="$(command -v javac || true)"
  if [[ -n "$JAVAC_BIN" ]]; then
    JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$JAVAC_BIN")")")"
    export JAVA_HOME
  fi
fi
export LD_LIBRARY_PATH="$HERE/usr/lib:${JAVA_HOME:+$JAVA_HOME/lib/server:}${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/KyumistEditor" "$@"
EOF
chmod 0755 "$APPDIR/AppRun"

APPIMAGETOOL="$(command -v appimagetool || true)"
if [[ -z "$APPIMAGETOOL" && -x /tmp/appimagetool-x86_64.AppImage ]]; then
  APPIMAGETOOL="/tmp/appimagetool-x86_64.AppImage"
fi

if [[ -n "$APPIMAGETOOL" ]]; then
  ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$APPIMAGE_FILE"
  echo "AppImage generado en: $APPIMAGE_FILE"
else
  echo "AppDir preparado correctamente en: $APPDIR"
  echo "Para generar el .AppImage final, instala o descarga appimagetool y ejecuta:"
  echo "  ARCH=x86_64 appimagetool \"$APPDIR\" \"$APPIMAGE_FILE\""
fi


#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-appimage"
TOOLS_DIR="$SCRIPT_DIR/tools"
APPDIR="$BUILD_DIR/AppDir"

ARCH="$(uname -m)"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-$ARCH.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-$ARCH.AppImage"

echo "=== Paint.nux AppImage Builder ==="

# Download tools if not cached
mkdir -p "$TOOLS_DIR"
if [ ! -x "$APPIMAGETOOL" ]; then
    echo "Downloading appimagetool..."
    curl -fSL -o "$APPIMAGETOOL" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
    chmod +x "$APPIMAGETOOL"
fi

# Build release
echo "Building Paint.nux (Release)..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Install into AppDir
echo "Installing to AppDir..."
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

# Generate PNG icons from SVG
echo "Generating PNG icons..."
SVG_ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/paintnux.svg"
for SIZE in 16 32 48 64 128 256; do
    ICON_DIR="$APPDIR/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps"
    mkdir -p "$ICON_DIR"
    rsvg-convert -w "$SIZE" -h "$SIZE" "$SVG_ICON" -o "$ICON_DIR/paintnux.png" 2>/dev/null \
        || convert -background none "$SVG_ICON" -resize "${SIZE}x${SIZE}" "$ICON_DIR/paintnux.png" 2>/dev/null \
        || echo "WARNING: Could not generate ${SIZE}x${SIZE} icon (install librsvg2-bin or imagemagick)"
done

# Bundle Qt libraries
echo "Bundling Qt libraries..."
QT_PLUGIN_PATH="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qmake -query QT_INSTALL_PLUGINS)"
QT_LIB_PATH="$(qmake6 -query QT_INSTALL_LIBS 2>/dev/null || qmake -query QT_INSTALL_LIBS)"

# Copy Qt libs that our binary links against
mkdir -p "$APPDIR/usr/lib"
for lib in $(ldd "$APPDIR/usr/bin/paintnux" | grep -oP '/\S+libQt6\S+\.so\S*'); do
    cp -nL "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
done

# Copy essential Qt plugins
for plugin_dir in platforms platformthemes imageformats platforminputcontexts xcbglintegrations; do
    if [ -d "$QT_PLUGIN_PATH/$plugin_dir" ]; then
        mkdir -p "$APPDIR/usr/plugins/$plugin_dir"
        cp -rL "$QT_PLUGIN_PATH/$plugin_dir/"*.so "$APPDIR/usr/plugins/$plugin_dir/" 2>/dev/null || true
    fi
done

# Copy indirect dependencies of bundled libs
for lib in "$APPDIR"/usr/lib/libQt6*.so* "$APPDIR"/usr/plugins/*/*.so; do
    [ -f "$lib" ] || continue
    for dep in $(ldd "$lib" 2>/dev/null | grep -oP '/\S+\.so\S*' || true); do
        base="$(basename "$dep")"
        [ -f "$APPDIR/usr/lib/$base" ] && continue
        # Only bundle libs not in base system
        case "$base" in
            libQt6*|libicu*|libpcre2*|libdouble*|libxcb*|libxkb*|libmd4c*)
                cp -nL "$dep" "$APPDIR/usr/lib/" 2>/dev/null || true
                ;;
        esac
    done
done

# Create qt.conf so Qt finds plugins
cat > "$APPDIR/usr/bin/qt.conf" <<QTCONF
[Paths]
Prefix = ../
Plugins = plugins
QTCONF

# Create AppRun script
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/usr/bin/env bash
SELF_DIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$SELF_DIR/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$SELF_DIR/usr/bin/paintnux" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# Symlink desktop file and icon to AppDir root (required by AppImage spec)
ln -sf usr/share/applications/paintnux.desktop "$APPDIR/paintnux.desktop"
ln -sf usr/share/icons/hicolor/256x256/apps/paintnux.png "$APPDIR/paintnux.png"
ln -sf paintnux.png "$APPDIR/.DirIcon"

# Build AppImage
echo "Creating AppImage..."
OUTPUT="$PROJECT_DIR/Paint.nux-$ARCH.AppImage"
ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"

echo "=== Done: $OUTPUT ==="
ls -lh "$OUTPUT"

#!/bin/bash
# Сборка "Косынки" так, чтобы бинарник запускался на Linux Mint (и любом
# дистрибутиве с glibc >= 2.31): исходники компилируются против СКАЧАННОЙ
# glibc 2.31 (Debian 11 bullseye) из .build/sysroot, поэтому системная glibc
# больше не нужна версии 2.4x — достаточно 2.30+ (Mint 20/21/22).
#
# Этапы:
#   1. качает пакеты glibc 2.31 + X11 в .build/sysroot (fetch/fix_sysroot.py)
#   2. собирает raylib (static, GLFW/X11) этими же старыми заголовками
#   3. компилирует игру -> dist/solitaire-linux-x86_64/solitaire
#   4. кладёт в пакет X11-библиотеки, ресурсы и run.sh
#   5. проверяет максимальную требуемую версию GLIBC_* и делает тестовый запуск
#
# Внешним остаётся только libGL.so.1 (драйвер Mesa): его нельзя влинковать,
# GLFW грузит его через dlopen; на Mint он есть по умолчанию.
#
# Использование: ./build_portable_mint.sh    (SKIP_TEST=1 — без теста)
set -euo pipefail

SRC_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$SRC_DIR/.build"
SYS="$BUILD/sysroot"
RAYLIB_SRC="$BUILD/raylib-sys"
CC_OLD="$BUILD/cc-old"
PKG="solitaire-linux-x86_64"
DIST="$SRC_DIR/dist/$PKG"
MAX_GLIBC="2.31"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Нет утилиты '$1'" >&2; exit 1; }; }
need gcc; need make; need python3; need dpkg-deb; need tar

# --- 1. Sysroot с glibc 2.31 ----------------------------------------------
if [ ! -f "$SYS/usr/include/X11/Xlib.h" ] || [ ! -f "$SYS/lib/x86_64-linux-gnu/libc.so.6" ]; then
    echo "==> Качаю glibc 2.31 + X11 из Debian bullseye"
    python3 "$BUILD/fetch_sysroot.py"
fi
python3 "$BUILD/fix_sysroot.py" >/dev/null
chmod +x "$CC_OLD"

# --- 2. raylib: static, GLFW/X11, cтарые заголовки -------------------------
if [ ! -f "$RAYLIB_SRC/src/libraylib.a" ]; then
    echo "==> Собираю raylib против glibc 2.31"
    if [ ! -d "$BUILD/raylib-6.2" ]; then
        curl -fsSL -o "$BUILD/raylib.tgz" \
            "https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/6.2"
        tar xzf "$BUILD/raylib.tgz" -C "$BUILD"
    fi
    rm -rf "$RAYLIB_SRC"
    cp -a "$BUILD/raylib-6.2" "$RAYLIB_SRC"
    make -C "$RAYLIB_SRC/src" -j"$(nproc)" PLATFORM=PLATFORM_DESKTOP \
        RAYLIB_LIBTYPE=STATIC GLFW_LINUX_ENABLE_WAYLAND=FALSE \
        GLFW_LINUX_ENABLE_X11=TRUE CC="$CC_OLD" >/dev/null
fi

# --- 3. Игра --------------------------------------------------------------
echo "==> Компилирую игру против glibc 2.31"
GCCDIR="$SYS/usr/lib/gcc/x86_64-linux-gnu/10"
rm -rf "$DIST"; mkdir -p "$DIST/lib" "$DIST/assets/cards" "$DIST/assets/fonts" \
    "$DIST/assets/jpg" "$DIST/assets/ogg" "$DIST/game"
"$CC_OLD" -O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wno-unused-parameter \
    -I "$RAYLIB_SRC/src" \
    -o "$DIST/solitaire" \
    "$SRC_DIR/main.c" "$SRC_DIR"/game/*.c \
    "$RAYLIB_SRC/src/libraylib.a" \
    -L "$GCCDIR" -Wl,-z,noexecstack -Wl,--as-needed \
    -lm -ldl -lpthread -lrt \
    -lX11 -lXrandr -lXinerama -lXcursor -lXi -lXext
strip --strip-unneeded "$DIST/solitaire"

# --- 4. Runtime-библиотеки (glibc НЕ кладём: системный libGL от Mesa собран
#        под glibc самого дистрибутива и загружается через dlopen) ---------
echo "==> Укладываю X11-библиотеки"
for f in libX11.so.6 libxcb.so.1 libXau.so.6 libXdmcp.so.6 libXext.so.6 \
         libXrandr.so.2 libXinerama.so.1 libXcursor.so.1 libXfixes.so.3 \
         libXrender.so.1 libXi.so.6 libbsd.so.0 libmd.so.0; do
    for d in "$SYS/usr/lib/x86_64-linux-gnu" "$SYS/lib/x86_64-linux-gnu"; do
        if [ -e "$d/$f" ]; then cp -fL "$d/$f" "$DIST/lib/$f"; break; fi
    done
done

# --- 5. Ресурсы -----------------------------------------------------------
echo "==> Копирую ассеты"
find "$SRC_DIR/assets/cards" -maxdepth 1 -type f -name '*.png' \
    -exec cp -f {} "$DIST/assets/cards/" \;
cp -f "$SRC_DIR/assets/fonts/main.ttf" "$DIST/assets/fonts/main.ttf"
cp -f "$SRC_DIR/assets/jpg/felt.png"   "$DIST/assets/jpg/felt.png"
cp -f "$SRC_DIR/assets/jpg/feltx2.jpg" "$DIST/assets/jpg/feltx2.jpg"
for s in sfx_shared_liftoff sfx_shared_movecardsetdown sfx_hearts_cardsclear \
         sfx_shared_cardsparkle sfx_shared_hintshown sfx_shared_illegalmove \
         sfx_shared_undo; do
    cp -f "$SRC_DIR/assets/ogg/$s.wma.ogg" "$DIST/assets/ogg/$s.wma.ogg"
done
cp -f "$SRC_DIR/game/lighting.vs" "$DIST/game/lighting.vs"
cp -f "$SRC_DIR/game/lighting.fs" "$DIST/game/lighting.fs"

# --- 5b. Конфиг камеры (необязательный, правится пользователем) -------------
if [ -f "$SRC_DIR/solitaire.conf" ]; then
    cp -f "$SRC_DIR/solitaire.conf" "$DIST/solitaire.conf"
fi

# --- 6. Лаунчер -----------------------------------------------------------
cat > "$DIST/run.sh" <<'LAUNCHER'
#!/bin/sh
# Нужна системная glibc >= 2.30 (Mint 20+) и libGL (Mesa). X11 тащим из lib/.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$here" || exit 1

have_gl=""
for d in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /lib /lib64; do
    if [ -e "$d/libGL.so.1" ]; then have_gl=1; break; fi
done
if [ -z "$have_gl" ]; then
    echo "solitaire: не найден libGL.so.1 (OpenGL/Mesa)." >&2
    echo "  Debian/Ubuntu/Mint: sudo apt install libgl1" >&2
    exit 1
fi

if [ -x /lib64/ld-linux-x86-64.so.2 ] && [ -d "$here/lib" ]; then
    exec /lib64/ld-linux-x86-64.so.2 --library-path "$here/lib" \
        "$here/solitaire" "$@"
fi
exec "$here/solitaire" "$@"
LAUNCHER
chmod +x "$DIST/run.sh" "$DIST/solitaire"

# --- 7. Проверки ----------------------------------------------------------
echo "==> Проверяю требования к glibc"
max_need=$(objdump -T "$DIST/solitaire" | grep -o 'GLIBC_[0-9.]\+' | \
           sed 's/GLIBC_//' | sort -Vu | tail -1 || true)
echo "    максимум GLIBC_$max_need (цель <= $MAX_GLIBC)"

if [ -n "${SKIP_TEST:-}" ] || ! command -v xvfb-run >/dev/null 2>&1; then
    echo "==> Тестовый запуск пропущен"
else
    echo "==> Тестовый запуск 5 c (Xvfb)"
    log="$BUILD/selftest.log"
    set +e
    timeout 5 xvfb-run -a "$DIST/run.sh" >"$log" 2>&1
    rc=$?
    set -e
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        echo "ВНИМАНИЕ: код $rc" >&2; tail -15 "$log" >&2
    else
        echo "    ок (124 = работало до таймаута)"
    fi
    grep -E 'Failed to open file|SHADER' "$log" | head -5 >&2 || true
fi

echo "==> Пакую"
tar czf "$SRC_DIR/dist/$PKG.tar.gz" -C "$SRC_DIR/dist" "$PKG"
( cd "$SRC_DIR/dist" && sha256sum "$PKG.tar.gz" > "$PKG.tar.gz.sha256" )
du -sh "$DIST" "$SRC_DIR/dist/$PKG.tar.gz"
echo "Готово: tar xzf $PKG.tar.gz && ./$PKG/run.sh"

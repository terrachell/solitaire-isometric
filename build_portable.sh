#!/bin/bash
# Сборка "Косынки" в самодостаточный пакет для любого x86_64 Linux.
#
# Пакет НЕ использует системную glibc: внутри лежат собственные
#   lib/ld-linux-x86-64.so.2  (загрузчик)
#   lib/libc.so.6, lib/libm.so.6
#   lib/libX11.so.6 и остальные X11-библиотеки
# Запуск идёт через run.sh, который стартует бинарник нашим ld.so с
# --library-path lib. Поэтому пакет работает и на дистрибутивах со старой
# glibc (Ubuntu 18.04/20.04, Debian 10, CentOS 7 и т.п.), и на новых.
#
# Что берётся из системы (это нельзя влинковать статически):
#   * драйвер OpenGL (libGL/libGLX от Mesa) - он сам dlopen'ит вендорский
#     драйвер (swrast_dri.so / iris_dri.so / nvidia), поэтому статически
#     линковать libGL нельзя в принципе;
#   * ядро + /dev/dri (или libasound для звука, если есть).
# Без libGL приложение не стартует - это единственная внешняя зависимость.
#
# Использование:
#   ./build_portable.sh              собрать пакет и архив
#   SKIP_TEST=1 ./build_portable.sh  без проверочного запуска под Xvfb
set -euo pipefail

SRC_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$SRC_DIR/.build"
RAYLIB_VER="6.2"
RAYLIB_DIR="$BUILD/raylib-$RAYLIB_VER"
PKG="solitaire-linux-x86_64"
DIST="$SRC_DIR/dist/$PKG"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Нет утилиты '$1'" >&2; exit 1; }; }
need gcc; need make; need tar; need strip

mkdir -p "$BUILD/bin"
rm -rf "$DIST"
mkdir -p "$DIST/lib" "$DIST/assets/cards" "$DIST/assets/fonts" \
         "$DIST/assets/jpg" "$DIST/assets/ogg" "$DIST/game"

# --- 1. raylib: статическая сборка, бэкенд GLFW/X11 -------------------------
if [ ! -f "$RAYLIB_DIR/src/libraylib.a" ]; then
    echo "==> Скачиваю raylib $RAYLIB_VER"
    curl -fsSL -o "$BUILD/raylib.tgz" \
        "https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/$RAYLIB_VER"
    tar xzf "$BUILD/raylib.tgz" -C "$BUILD"
    echo "==> Собираю libraylib.a"
    make -C "$RAYLIB_DIR/src" -j"$(nproc)" \
        PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC >/dev/null
fi

# --- 2. Игра ---------------------------------------------------------------
echo "==> Компилирую игру"
gcc -O2 -std=c99 -D_DEFAULT_SOURCE -static-libgcc \
    -Wall -Wno-unused-parameter \
    -I "$RAYLIB_DIR/src" \
    -o "$BUILD/bin/solitaire" \
    "$SRC_DIR/main.c" "$SRC_DIR"/game/*.c \
    "$RAYLIB_DIR/src/libraylib.a" \
    -Wl,-z,noexecstack -Wl,--as-needed \
    -lm -ldl -lpthread -lrt \
    -lX11 -lXrandr -lXinerama -lXcursor -lXi -lXext

cp -f "$BUILD/bin/solitaire" "$DIST/solitaire"
strip --strip-unneeded "$DIST/solitaire"

# rpath на случай прямого запуска через наш загрузчик без --library-path
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$DIST/solitaire"
fi

# --- 3. Своя glibc + X11-библиотеки ---------------------------------------
echo "==> Укладываю runtime-библиотеки (своя glibc, системная не нужна)"
cp -fL /usr/lib/ld-linux-x86-64.so.2 "$DIST/lib/ld-linux-x86-64.so.2"
for lib in $(ldd "$DIST/solitaire" | awk '/=>/ {print $3}'); do
    [ -e "$lib" ] || continue
    case "$(basename "$lib")" in
        # GL-стек и драйверы Mesa берём из системы, иначе приложение не найдёт
        # видеодрайвер: libGL сам грузит *_dri.so через dlopen.
        libGL*|libEGL*|libGLX*|libGLdispatch*|libOpenGL*|libdrm*|libgbm*) continue ;;
        ld-linux-x86-64.so.2) continue ;;
    esac
    cp -fL "$lib" "$DIST/lib/"
done

# --- 4. Ресурсы, которые реально грузятся из кода -------------------------
echo "==> Копирую ассеты"
# только PNG: в assets/cards рядом лежит посторонний nor.py
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

# ВАЖНО: шейдеры грузятся по относительному пути "game/lighting.vs".
# Без них raylib молча подставляет шейдер по умолчанию (нет подсветки/нормалей).
cp -f "$SRC_DIR/game/lighting.vs" "$DIST/game/lighting.vs"
cp -f "$SRC_DIR/game/lighting.fs" "$DIST/game/lighting.fs"

# --- 5. Лаунчер ------------------------------------------------------------
cat > "$DIST/run.sh" <<'LAUNCHER'
#!/bin/sh
# Запуск без системной glibc: libc и X11 берутся из lib/ рядом с этим файлом.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$here" || exit 1
ld="$here/lib/ld-linux-x86-64.so.2"
[ -x "$ld" ] || { echo "solitaire: повреждён пакет, нет $ld" >&2; exit 1; }

# libGL влинковать статически нельзя - его dlopen'ит GLFW из системы.
have_gl=0
for d in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib32 \
         /lib /lib64 /usr/local/lib /usr/lib/opengl; do
    if [ -e "$d/libGL.so.1" ]; then have_gl=1; break; fi
done
if [ "$have_gl" = 0 ]; then
    echo "solitaire: не найден libGL.so.1 (OpenGL)." >&2
    echo "  Debian/Ubuntu: sudo apt install libgl1 mesa-utils" >&2
    echo "  Arch:          sudo pacman -S mesa" >&2
    echo "  Fedora:        sudo dnf install mesa-libGL" >&2
    exit 1
fi

[ -n "${RUN_DEBUG:-}" ] && exec "$ld" --library-path "$here/lib" --list "$here/solitaire"
exec "$ld" --library-path "$here/lib" "$here/solitaire" "$@"
LAUNCHER
chmod +x "$DIST/run.sh" "$DIST/solitaire"

# --- 6. Проверка: запуск на нашей же glibc, без участия системной ---------
echo "==> Проверяю зависимости"
if ldd "$DIST/solitaire" | grep -q '/lib64/ld-linux'; then
    : # ожидаемо: в ELF прописан системный загрузчик, run.sh его подменяет
fi
libs_used=$("$DIST/lib/ld-linux-x86-64.so.2" --library-path "$DIST/lib" \
            --list "$DIST/solitaire" 2>/dev/null | awk '{print $1}' | sort -u)
if echo "$libs_used" | grep -qvE '^(linux-vdso|/);'; then
    missing=$(echo "$libs_used" | grep -E '^/' | while read -r p; do
                [ -e "$p" ] || echo "$p"; done)
    [ -n "$missing" ] && { echo "Не хватает библиотек в пакете: $missing" >&2; exit 1; }
fi

if [ -z "${SKIP_TEST:-}" ]; then
    runner=""
    if [ -n "${DISPLAY:-}" ]; then
        runner=""                                  # есть живой X-сервер - запускаем на нём
    elif command -v xvfb-run >/dev/null 2>&1; then
        runner="xvfb-run -a"                       # без GL-контекста возможен аварийный выход
    fi
    if [ -n "${DISPLAY:-}" ] || [ -n "$runner" ]; then
        echo "==> Тестовый запуск (5 c)"
        log="$BUILD/selftest.log"
        set +e
        # shellcheck disable=SC2086
        timeout 5 $runner "$DIST/run.sh" >"$log" 2>&1
        rc=$?
        set -e
        # 124 = приложение проработало все 5 секунд, т.е. окно открылось
        if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
            echo "ВНИМАНИЕ: тестовый запуск завершился с кодом $rc" >&2
            tail -15 "$log" >&2
        else
            echo "    запуск ок (код $rc, 124 = работало до таймаута)"
        fi
        if grep -qE 'Failed to open file|SHADER: \[Shader Id' "$log"; then
            echo "ВНИМАНИЕ: не все ресурсы загрузились:" >&2
            grep -E 'Failed to open file|SHADER' "$log" | head -10 >&2
        fi
    fi
fi

# --- 7. Архив --------------------------------------------------------------
echo "==> Пакую в tar.gz"
tar czf "$SRC_DIR/dist/$PKG.tar.gz" -C "$SRC_DIR/dist" "$PKG"
( cd "$SRC_DIR/dist" && sha256sum "$PKG.tar.gz" > "$PKG.tar.gz.sha256" )

echo
echo "Готово:"
du -sh "$DIST" "$SRC_DIR/dist/$PKG.tar.gz"
echo "Запуск: tar xzf $PKG.tar.gz && ./$PKG/run.sh"

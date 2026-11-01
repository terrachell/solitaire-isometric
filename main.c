#include "game/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Кадров в секунду. Ставим 100 под 100-герцовый монитор.
   VSYNC-флаг специально НЕ включаем: при включённом vsync частота
   прибивается к герцовке монитора, а SetTargetFPS(100) работает как лимитер. */
#define TARGET_FPS 100

/* Файл настроек камеры. Ищем в текущем каталоге, затем рядом с бинарником. */
#define CONF_FILE "solitaire.conf"

typedef struct {
    Vector3 position;
    Vector3 target;
    Vector3 up;
    float   fovy;
    int     projection;
} CameraConfig;

/* ------------------------------------------------------------------------- */
/* Разбор solitaire.conf: строки "ключ = значение", '#' и ';' — комментарии.  */
/* Значения по умолчанию совпадают с прежними хардкод-константами, так что   */
/* без файла поведение игры не меняется.                                     */
/* ------------------------------------------------------------------------- */

static char *Trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static bool KeyIs(const char *key, const char *name) {
    while (*key && *name) {
        if (tolower((unsigned char)*key) != tolower((unsigned char)*name)) return false;
        key++; name++;
    }
    return (*key == '\0') && (*name == '\0');
}

static const char *SkipSep(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    return p;
}

/* "0, 32, 26" -> Vector3 */
static bool ParseVec3(const char *value, Vector3 *out) {
    float v[3];
    char *end = NULL;
    const char *p = value;

    for (int i = 0; i < 3; i++) {
        p = SkipSep(p);
        if (!*p) return false;
        v[i] = (float)strtod(p, &end);
        if (end == p) return false;
        p = end;
    }
    if (*SkipSep(p) != '\0') return false;   // мусор после третьего числа

    out->x = v[0]; out->y = v[1]; out->z = v[2];
    return true;
}

static bool ParseFloatValue(const char *value, float *out) {
    char *end = NULL;
    float v = (float)strtod(value, &end);
    if (end == value) return false;
    if (*SkipSep(end) != '\0') return false;
    *out = v;
    return true;
}

static bool IsZeroVec3(Vector3 v) {
    return (v.x == 0.0f) && (v.y == 0.0f) && (v.z == 0.0f);
}

static void ApplySetting(CameraConfig *cfg, const char *key, const char *value,
                         const char *path, int line) {
    if (KeyIs(key, "camera.position")) {
        Vector3 v;
        if (ParseVec3(value, &v)) cfg->position = v;
        else TraceLog(LOG_WARNING, "CONF %s:%i: camera.position ждёт 'x, y, z' — оставил прежнее", path, line);
    } else if (KeyIs(key, "camera.target")) {
        Vector3 v;
        if (ParseVec3(value, &v)) cfg->target = v;
        else TraceLog(LOG_WARNING, "CONF %s:%i: camera.target ждёт 'x, y, z' — оставил прежнее", path, line);
    } else if (KeyIs(key, "camera.up")) {
        Vector3 v;
        if (ParseVec3(value, &v) && !IsZeroVec3(v)) cfg->up = v;
        else TraceLog(LOG_WARNING, "CONF %s:%i: camera.up ждёт ненулевой 'x, y, z' — оставил прежнее", path, line);
    } else if (KeyIs(key, "camera.fovy")) {
        float v;
        /* Ортокамера: fovy = высота видимой области в мировых единицах. */
        if (ParseFloatValue(value, &v) && v > 1.0f && v < 200.0f) cfg->fovy = v;
        else TraceLog(LOG_WARNING, "CONF %s:%i: camera.fovy вне диапазона 1..200 — оставил прежнее", path, line);
    } else if (KeyIs(key, "camera.projection")) {
        if (KeyIs(value, "ortho") || KeyIs(value, "orthographic")) cfg->projection = CAMERA_ORTHOGRAPHIC;
        else if (KeyIs(value, "persp") || KeyIs(value, "perspective")) cfg->projection = CAMERA_PERSPECTIVE;
        else TraceLog(LOG_WARNING, "CONF %s:%i: camera.projection ждёт ortho|persp — оставил прежнее", path, line);
    } else {
        TraceLog(LOG_WARNING, "CONF %s:%i: неизвестный ключ '%s'", path, line, key);
    }
}

static bool LoadConfigFile(const char *path, CameraConfig *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    int lineNo = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        lineNo++;

        char *comment = strpbrk(line, "#;");
        if (comment) *comment = '\0';

        char *s = Trim(line);
        if (!*s) continue;

        char *eq = strchr(s, '=');
        if (!eq) {
            TraceLog(LOG_WARNING, "CONF %s:%i: строка без '=' пропущена", path, lineNo);
            continue;
        }
        *eq = '\0';

        char *key = Trim(s);
        char *value = Trim(eq + 1);
        if (!*key) continue;

        ApplySetting(cfg, key, value, path, lineNo);
    }

    fclose(f);
    return true;
}

static bool LoadConfig(CameraConfig *cfg) {
    if (LoadConfigFile(CONF_FILE, cfg)) return true;

    /* рядом с исполняемым файлом (пакет dist/) */
    const char *appDir = GetApplicationDirectory();
    if (appDir && appDir[0]) {
        char path[1024];
        size_t n = strlen(appDir);
        snprintf(path, sizeof(path), "%s%s%s", appDir, (appDir[n - 1] == '/') ? "" : "/", CONF_FILE);
        if (LoadConfigFile(path, cfg)) return true;
    }
    return false;
}

int main(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);

    /* Камера: значения по умолчанию, поверх них — solitaire.conf (если есть). */
    CameraConfig cfg;
    cfg.position   = (Vector3){ 0.0f, 32.0f, 26.0f };
    cfg.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    cfg.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    cfg.fovy       = 12.48f;
    cfg.projection = CAMERA_ORTHOGRAPHIC;
    LoadConfig(&cfg);

    Vector3 dir = { cfg.position.x - cfg.target.x,
                    cfg.position.y - cfg.target.y,
                    cfg.position.z - cfg.target.z };
    if (IsZeroVec3(dir)) {
        TraceLog(LOG_WARNING, "CONF: position совпал с target — беру значение по умолчанию");
        cfg.position = (Vector3){ 0.0f, 32.0f, 26.0f };
        cfg.target   = (Vector3){ 0.0f, 0.0f, 0.0f };
    }

    InitWindow(1280, 800, "Косынка — Isometric Solitaire");
    SetTargetFPS(TARGET_FPS);

    Camera3D cam = { 0 };
    cam.position   = cfg.position;
    cam.target     = cfg.target;
    cam.up         = cfg.up;
    cam.fovy       = cfg.fovy;
    cam.projection = cfg.projection;

    Render_LoadAssets();
    Stats_Init();          // читаем solitaire_stats.json (винрейт, серии, настройки)
    Rules_Init();
    Rules_NewGame();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        Handle_Input(cam);
        Anim_Update(dt);

        BeginDrawing();
        ClearBackground((Color){ 18, 22, 18, 255 });
        Render_Scene(cam);
        Render_HUD(cam);
        EndDrawing();
    }

    Render_UnloadAssets();
    CloseWindow();
    return 0;
}

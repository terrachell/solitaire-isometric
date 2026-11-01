#include "game.h"
#include "layout.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <math.h>

// ---------------------------------------------------------------------------
//  Рендер сцены + новый интерфейс (панель инструментов, счётчики, статистика).
// ---------------------------------------------------------------------------

// --- Палитра интерфейса ---
static const Color UI_PANEL     = { 14, 20, 16, 210 };
static const Color UI_PANEL_TOP = { 22, 30, 24, 225 };
static const Color UI_BTN       = { 26, 34, 28, 232 };
static const Color UI_BTN_HOVER = { 46, 64, 48, 245 };
static const Color UI_BTN_ACTIVE= { 62, 88, 62, 248 };
static const Color UI_BORDER    = { 255, 255, 255, 55 };
static const Color UI_ACCENT    = { 226, 184, 106, 255 };
static const Color UI_TEXT      = { 238, 240, 235, 255 };
static const Color UI_TEXT_DIM  = { 160, 172, 160, 255 };
static const Color UI_WIN       = { 140, 214, 148, 255 };
static const Color UI_LOSE      = { 214, 142, 132, 255 };

static const char *RankName(int r) {
    switch (r) {
        case 1:  return "ace";
        case 11: return "jack";
        case 12: return "queen";
        case 13: return "king";
        default: { static char buf[4]; snprintf(buf, sizeof(buf), "%02d", r); return buf; }
    }
}

static const char *SuitName(int s) {
    switch (s) {
        case SUIT_CLUBS:    return "clubs";
        case SUIT_DIAMONDS: return "diamonds";
        case SUIT_HEARTS:   return "hearts";
        case SUIT_SPADES:   return "spades";
    }
    return "clubs";
}

static Mesh     g_tableMesh  = {0};
static Material g_tableMat   = {0};
static bool     g_tableReady = false;

static Mesh     g_cardMesh   = {0};
static Material g_cardMat    = {0};

void Render_LoadAssets(void) {
    // 1. Шрифт (латиница + кириллица)
    int codepoints[512];
    int count = 0;
    for (int i = 32; i <= 126; i++) codepoints[count++] = i;
    for (int i = 1024; i <= 1279; i++) codepoints[count++] = i;
    codepoints[count++] = 8212;
    codepoints[count++] = 183;   // ·

    assets.mainFont = LoadFontEx("assets/fonts/main.ttf", 64, codepoints, count);
    if (assets.mainFont.texture.id == 0) {
        TraceLog(LOG_ERROR, "Не удалось загрузить шрифт! Используется дефолтный.");
        assets.mainFont = GetFontDefault();
    } else {
        SetTextureFilter(assets.mainFont.texture, TEXTURE_FILTER_BILINEAR);
    }

    // 2. Текстура свечения
    Image glowImg = GenImageGradientRadial(128, 128, 0.0f, (Color){255,255,255,255}, (Color){0,0,0,0});
    assets.glowTex = LoadTextureFromImage(glowImg);
    UnloadImage(glowImg);

    // 3. Фон (сукно)
    assets.table = LoadTexture("assets/jpg/felt.png");
    if (assets.table.id == 0) assets.table = LoadTexture("assets/jpg/feltx2.jpg");
    if (assets.table.id != 0) {
        GenTextureMipmaps(&assets.table);
        SetTextureWrap(assets.table, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(assets.table, TEXTURE_FILTER_ANISOTROPIC_16X);
    }

    // 4. Карты
    for (int s = 0; s < SUIT_COUNT; s++) {
        for (int r = 1; r <= 13; r++) {
            char path[256], normalPath[256];
            snprintf(path, sizeof(path), "assets/cards/%s_%s.png", SuitName(s), RankName(r));
            assets.cardTex[s][r] = LoadTexture(path);
            if (assets.cardTex[s][r].id != 0) {
                GenTextureMipmaps(&assets.cardTex[s][r]);
                SetTextureFilter(assets.cardTex[s][r], TEXTURE_FILTER_ANISOTROPIC_16X);
            }
            snprintf(normalPath, sizeof(normalPath), "assets/cards/%s_%s_normal.png", SuitName(s), RankName(r));
            assets.cardNormals[s][r] = LoadTexture(normalPath);
            if (assets.cardNormals[s][r].id != 0) {
                GenTextureMipmaps(&assets.cardNormals[s][r]);
                SetTextureFilter(assets.cardNormals[s][r], TEXTURE_FILTER_ANISOTROPIC_16X);
            } else {
                Image img = GenImageColor(1, 1, (Color){128, 128, 255, 255});
                assets.cardNormals[s][r] = LoadTextureFromImage(img);
                UnloadImage(img);
            }
        }
    }

    assets.cardBack = LoadTexture("assets/cards/back01.png");
    if (assets.cardBack.id != 0) {
        GenTextureMipmaps(&assets.cardBack);
        SetTextureFilter(assets.cardBack, TEXTURE_FILTER_ANISOTROPIC_16X);
    }

    // 5. Меш и материал стола
    g_tableMesh = GenMeshPlane(100.0f, 100.0f, 10, 10);
    UploadMesh(&g_tableMesh, false);
    g_tableMat = LoadMaterialDefault();
    if (assets.table.id != 0) g_tableMat.maps[MATERIAL_MAP_DIFFUSE].texture = assets.table;
    g_tableReady = true;

    // 6. Меш и материал карт
    g_cardMesh = GenMeshPlane(CARD_W, CARD_H, 1, 1);
    g_cardMesh.tangents = (float *)MemAlloc(g_cardMesh.vertexCount * 4 * sizeof(float));
    for (int i = 0; i < g_cardMesh.vertexCount; i++) {
        g_cardMesh.tangents[i * 4 + 0] = 1.0f;
        g_cardMesh.tangents[i * 4 + 1] = 0.0f;
        g_cardMesh.tangents[i * 4 + 2] = 0.0f;
        g_cardMesh.tangents[i * 4 + 3] = 1.0f;
    }
    UploadMesh(&g_cardMesh, false);

    g_cardMat = LoadMaterialDefault();
    g_cardMat.shader = LoadShader("game/lighting.vs", "game/lighting.fs");
    g_cardMat.shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(g_cardMat.shader, "texture0");
    g_cardMat.shader.locs[SHADER_LOC_MAP_NORMAL]  = GetShaderLocation(g_cardMat.shader, "texture1");

    // 7. Звуки
    InitAudioDevice();
    assets.sndLift        = LoadSound("assets/ogg/sfx_shared_liftoff.wma.ogg");
    assets.sndDrop        = LoadSound("assets/ogg/sfx_shared_movecardsetdown.wma.ogg");
    assets.sndCardsClear  = LoadSound("assets/ogg/sfx_hearts_cardsclear.wma.ogg");
    assets.sndCardSparkle = LoadSound("assets/ogg/sfx_shared_cardsparkle.wma.ogg");
    assets.sndHintShown   = LoadSound("assets/ogg/sfx_shared_hintshown.wma.ogg");
    assets.sndIllegalMove = LoadSound("assets/ogg/sfx_shared_illegalmove.wma.ogg");
    assets.sndUndo        = LoadSound("assets/ogg/sfx_shared_undo.wma.ogg");

    SetSoundVolume(assets.sndLift,        0.5f);
    SetSoundVolume(assets.sndDrop,        0.5f);
    SetSoundVolume(assets.sndCardsClear,  0.5f);
    SetSoundVolume(assets.sndCardSparkle, 0.5f);
    SetSoundVolume(assets.sndHintShown,   0.5f);
    SetSoundVolume(assets.sndIllegalMove, 0.5f);
    SetSoundVolume(assets.sndUndo,        0.5f);
}

void Render_UnloadAssets(void) {
    if (g_tableReady) UnloadMesh(g_tableMesh);
    UnloadMesh(g_cardMesh);
    if (assets.table.id != 0) UnloadTexture(assets.table);
    if (assets.glowTex.id != 0) UnloadTexture(assets.glowTex);

    for (int s = 0; s < SUIT_COUNT; s++) {
        for (int r = 1; r <= 13; r++) {
            if (assets.cardTex[s][r].id != 0) UnloadTexture(assets.cardTex[s][r]);
            if (assets.cardNormals[s][r].id != 0) UnloadTexture(assets.cardNormals[s][r]);
        }
    }
    if (assets.cardBack.id != 0) UnloadTexture(assets.cardBack);
    if (assets.mainFont.texture.id != 0) UnloadFont(assets.mainFont);

    UnloadSound(assets.sndLift);
    UnloadSound(assets.sndDrop);
    UnloadSound(assets.sndCardsClear);
    UnloadSound(assets.sndCardSparkle);
    UnloadSound(assets.sndHintShown);
    UnloadSound(assets.sndIllegalMove);
    UnloadSound(assets.sndUndo);
    CloseAudioDevice();
}

// Публичная отрисовка одной карты (используется и в anim.c)
void Render_DrawCardAt(Vector3 center, Texture2D tex, Texture2D normalTex, Color tint) {
    if (tex.id == 0) return;
    g_cardMat.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    g_cardMat.maps[MATERIAL_MAP_DIFFUSE].color   = tint;
    g_cardMat.maps[MATERIAL_MAP_NORMAL].texture  = normalTex;
    Matrix transform = MatrixTranslate(center.x, center.y, center.z);
    DrawMesh(g_cardMesh, g_cardMat, transform);
}

static bool IsHinted(StackType type, int idx, int cardIdx) {
    if (g.hintStackType < 0) return false;
    if (g.hintStackType != (int)type) return false;
    if (g.hintStackIdx != idx) return false;
    if (type == STACK_WASTE) return (g.hintStackIdx == cardIdx);
    if (g.hintCardIdx < 0) return false;

    int cnt = (g.hintCount > 0) ? g.hintCount : 1;
    return (cardIdx >= g.hintCardIdx && cardIdx < g.hintCardIdx + cnt);
}

static bool IsHintTarget(StackType type, int idx) {
    return (g.hintTargetType == (int)type && g.hintTargetIdx == idx);
}

static void DrawHintGlow(Vector3 center, Color color) {
    float w = CARD_W * 0.5f;
    float h = CARD_H * 0.5f;
    float y = center.y + 0.01f;

    float pulse = (sinf(GetTime() * 5.0f) + 1.0f) * 0.5f;
    float thickness = 0.12f + 0.06f * pulse;
    unsigned char alpha = 150 + (unsigned char)(105 * pulse);

    rlDisableDepthTest();

    for (float t = 0; t < thickness; t += 0.02f) {
        Color c = { color.r, color.g, color.b, (unsigned char)(alpha * (1.0f - t/thickness)) };
        DrawLine3D((Vector3){ center.x - w - t, y, center.z - h - t }, (Vector3){ center.x + w + t, y, center.z - h - t }, c);
        DrawLine3D((Vector3){ center.x + w + t, y, center.z - h - t }, (Vector3){ center.x + w + t, y, center.z + h + t }, c);
        DrawLine3D((Vector3){ center.x + w + t, y, center.z + h + t }, (Vector3){ center.x - w - t, y, center.z + h + t }, c);
        DrawLine3D((Vector3){ center.x - w - t, y, center.z + h + t }, (Vector3){ center.x - w - t, y, center.z - h - t }, c);
    }

    float gw = CARD_W * 1.6f;
    float gh = CARD_H * 1.6f;

    rlSetTexture(assets.glowTex.id);
    rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, (unsigned char)(60 + 60 * pulse));
        rlVertex3f(center.x - gw/2, y - 0.05f, center.z - gh/2);
        rlVertex3f(center.x - gw/2, y - 0.05f, center.z + gh/2);
        rlVertex3f(center.x + gw/2, y - 0.05f, center.z + gh/2);
        rlVertex3f(center.x + gw/2, y - 0.05f, center.z - gh/2);
    rlEnd();
    rlSetTexture(0);

    rlEnableDepthTest();
}

static void DrawEmptySlot(Vector3 base) {
    Material mat = LoadMaterialDefault();
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = assets.cardBack;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 255, 255, 255, 80 };
    Matrix transform = MatrixTranslate(base.x, 0.01f, base.z);
    DrawMesh(g_cardMesh, mat, transform);
}

static void DrawStack(Stack *s, StackType type, int idx) {
    if (s->count == 0 && type == STACK_FOUNDATION) {
        DrawEmptySlot(GetStackBasePos(type, idx));
    }

    if (g.hintTargetType >= 0 && !g.holding && IsHintTarget(type, idx) && s->count == 0) {
        Vector3 base = GetStackBasePos(type, idx);
        DrawHintGlow((Vector3){ base.x, 0.02f, base.z }, (Color){ 255, 150, 50, 255 });
    }

    if (type == STACK_DECK) {
        for (int i = 0; i < s->count; i++) {
            if (Anim_IsCardFlying(s->cards[i])) continue;
            Vector3 base = GetStackBasePos(type, idx);
            Vector3 off  = StackCardOffset(type, i, idx);
            Vector3 pos = { base.x + off.x, off.y + 0.02f, base.z + off.z };
            Render_DrawCardAt(pos, assets.cardBack, assets.cardNormals[0][1], WHITE);
        }
        return;
    }

    if (type == STACK_WASTE) {
        if (g.wasteWindowCount == 0) {
            DrawEmptySlot(GetStackBasePos(STACK_WASTE, 0));
            return;
        }
        Vector3 base = GetStackBasePos(STACK_WASTE, 0);
        for (int i = 0; i < g.wasteWindowCount; i++) {
            if (g.wasteWindowHeld[i]) continue;
            if (Anim_IsCardFlying(g.wasteWindow[i])) continue;

            Vector3 off = StackCardOffset(STACK_WASTE, i, 0);
            Vector3 pos = { base.x + off.x, off.y + 0.02f, base.z + off.z };

            Texture2D tex  = assets.cardTex[g.wasteWindow[i].suit][g.wasteWindow[i].rank];
            Texture2D norm = assets.cardNormals[g.wasteWindow[i].suit][g.wasteWindow[i].rank];
            Render_DrawCardAt(pos, tex, norm, WHITE);

            if (IsHinted(STACK_WASTE, i, i)) {
                DrawHintGlow((Vector3){ pos.x, pos.y + 0.02f, pos.z }, (Color){ 0, 200, 255, 255 });
            }
        }
        return;
    }

    for (int i = s->count - 1; i >= 0; i--) {
        Card *c = &s->cards[i];
        if (Anim_IsCardFlying(*c)) continue;

        Vector3 base = GetStackBasePos(type, idx);
        Vector3 off = StackCardOffset(type, i, idx);
        Vector3 pos = { base.x + off.x, off.y + 0.02f, base.z + off.z };

        bool selected = (g.selectedStackType == (int)type && g.selectedStackIdx == idx && g.selectedCardIdx == i);

        Color tint = WHITE;
        if (selected) {
            tint = (Color){ 255, 245, 180, 255 };
            pos.y += LIFT_Y;
        }

        Texture2D tex = c->faceUp ? assets.cardTex[c->suit][c->rank] : assets.cardBack;
        Texture2D norm = c->faceUp ? assets.cardNormals[c->suit][c->rank] : assets.cardNormals[0][1];
        Render_DrawCardAt(pos, tex, norm, tint);

        if (IsHinted(type, idx, i)) {
            DrawHintGlow((Vector3){ pos.x, pos.y + 0.02f, pos.z }, (Color){ 0, 200, 255, 255 });
        }

        if (!g.holding && i == s->count - 1 && IsHintTarget(type, idx)) {
            DrawHintGlow((Vector3){ pos.x, pos.y + 0.02f, pos.z }, (Color){ 255, 150, 50, 255 });
        }
    }
}

void Render_Scene(Camera3D camera) {
    // Предел длины столбца зависит от размера окна — пересчитываем каждый кадр
    Layout_UpdateScreenLimit(camera);

    if (assets.table.id != 0) {
        Rectangle src  = { 0.0f, 0.0f, (float)assets.table.width, (float)assets.table.height };
        Rectangle dest = { 0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight() };
        DrawTexturePro(assets.table, src, dest, (Vector2){0, 0}, 0.0f, WHITE);
    } else {
        ClearBackground((Color){ 30, 120, 60, 255 });
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangleGradientV(0, 0, sw, 130, (Color){ 0, 0, 0, 165 }, (Color){ 0, 0, 0, 0 });
    DrawRectangleGradientV(0, sh - 150, sw, 150, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 175 });

    BeginMode3D(camera);
    DrawStack(&g.deck,  STACK_DECK,  0);
    DrawStack(&g.waste, STACK_WASTE, 0);
    for (int i = 0; i < MAX_TABLEAU; i++)    DrawStack(&g.tableau[i],    STACK_TABLEAU,    i);
    for (int i = 0; i < MAX_FOUNDATION; i++) DrawStack(&g.foundation[i], STACK_FOUNDATION, i);

    if (g.holding) {
        for (int i = g.heldCount - 1; i >= 0; i--) {
            Vector3 off = StackCardOffset(STACK_TABLEAU, i, -1);
            Vector3 pos = { g.heldPos.x + off.x, g.heldPos.y + LIFT_Y + off.y, g.heldPos.z + off.z };
            Texture2D tex  = assets.cardTex[g.heldCards[i].suit][g.heldCards[i].rank];
            Texture2D norm = assets.cardNormals[g.heldCards[i].suit][g.heldCards[i].rank];
            Render_DrawCardAt(pos, tex, norm, WHITE);
        }
    }

    Anim_Draw();
    EndMode3D();
}

// ============================ Интерфейс ============================

static Rectangle s_btnNew   = {0};
static Rectangle s_btnUndo  = {0};
static Rectangle s_btnHint  = {0};
static Rectangle s_btnAuto  = {0};
static Rectangle s_btnDraw  = {0};
static Rectangle s_btnStats = {0};
static Rectangle s_statsPanel = {0};

#define UI_FONT_SMALL  14.0f
#define UI_FONT_NORMAL 15.0f
#define UI_FONT_TITLE  22.0f

bool UI_PointInRect(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

static float TextW(const char *text, float size) {
    return MeasureTextEx(assets.mainFont, text, size, 0.6f).x;
}

static float ButtonW(const char *label) {
    return TextW(label, UI_FONT_NORMAL) + 26.0f;
}

// Раскладка пересчитывается по запросу: кнопки всегда доступны и для кликов,
// и для отрисовки (в том числе в первом кадре).
static void UpdateLayout(void) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float bh = 32.0f;
    float y  = (UI_TOPBAR_H - bh) * 0.5f;
    float x  = 12.0f;
    const float gap = 8.0f;

    char drawLabel[32];
    snprintf(drawLabel, sizeof(drawLabel), "Раздача: %d", g.drawCount);

    s_btnNew   = (Rectangle){ x, y, ButtonW("Новая  N"),       bh }; x += s_btnNew.width + gap;
    s_btnUndo  = (Rectangle){ x, y, ButtonW("Отмена  U"),      bh }; x += s_btnUndo.width + gap;
    s_btnHint  = (Rectangle){ x, y, ButtonW("Подсказка  H"),   bh }; x += s_btnHint.width + gap;
    s_btnAuto  = (Rectangle){ x, y, ButtonW("Сбор  Пробел"),   bh }; x += s_btnAuto.width + gap;
    s_btnDraw  = (Rectangle){ x, y, ButtonW(drawLabel),        bh }; x += s_btnDraw.width + gap;
    s_btnStats = (Rectangle){ x, y, ButtonW("Статистика  S"),  bh };

    float pw = 760.0f;
    float ph = 380.0f;
    if (pw > (float)sw - 40.0f) pw = (float)sw - 40.0f;
    if (ph > (float)sh - 100.0f) ph = (float)sh - 100.0f;
    s_statsPanel = (Rectangle){ (sw - pw) * 0.5f, (sh - ph) * 0.5f, pw, ph };
}

Rectangle UI_GetButtonNew(void)   { UpdateLayout(); return s_btnNew; }
Rectangle UI_GetButtonUndo(void)  { UpdateLayout(); return s_btnUndo; }
Rectangle UI_GetButtonHint(void)  { UpdateLayout(); return s_btnHint; }
Rectangle UI_GetButtonAuto(void)  { UpdateLayout(); return s_btnAuto; }
Rectangle UI_GetButtonDraw(void)  { UpdateLayout(); return s_btnDraw; }
Rectangle UI_GetButtonStats(void) { UpdateLayout(); return s_btnStats; }
Rectangle UI_GetStatsPanel(void)  { UpdateLayout(); return s_statsPanel; }

static void DrawButtonEx(Rectangle r, const char *label, bool enabled, bool active) {
    Vector2 m = GetMousePosition();
    bool hover = enabled && UI_PointInRect(m, r);

    Color fill = UI_BTN;
    if (!enabled)    fill = (Color){ 18, 22, 18, 170 };
    else if (active) fill = UI_BTN_ACTIVE;
    else if (hover)  fill = UI_BTN_HOVER;

    DrawRectangleRounded(r, 0.42f, 10, (Color){ 0, 0, 0, 90 });
    DrawRectangleRounded(r, 0.42f, 10, fill);

    Color border = active ? UI_ACCENT
                  : (hover ? (Color){ 255, 255, 255, 140 } : UI_BORDER);
    DrawRectangleRoundedLinesEx(r, 0.42f, 10, 1.0f, border);

    Color textColor = enabled ? (active ? (Color){ 255, 244, 214, 255 } : UI_TEXT)
                              : (Color){ 120, 128, 120, 255 };

    Vector2 ts = MeasureTextEx(assets.mainFont, label, UI_FONT_NORMAL, 0.6f);
    DrawTextEx(assets.mainFont, label,
        (Vector2){ r.x + (r.width - ts.x) / 2.0f, r.y + (r.height - ts.y) / 2.0f },
        UI_FONT_NORMAL, 0.6f, textColor);
}

static void DrawLabelValue(float x, float y, float w, const char *label, const char *value, bool accent) {
    DrawTextEx(assets.mainFont, label, (Vector2){ x, y }, UI_FONT_NORMAL, 0.6f, UI_TEXT_DIM);
    Vector2 vs = MeasureTextEx(assets.mainFont, value, UI_FONT_NORMAL, 0.6f);
    DrawTextEx(assets.mainFont, value, (Vector2){ x + w - vs.x, y }, UI_FONT_NORMAL, 0.6f,
               accent ? UI_ACCENT : UI_TEXT);
}

static void DrawStatsPanel(void) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 120 });

    Rectangle p = UI_GetStatsPanel();
    DrawRectangleRounded(p, 0.06f, 16, (Color){ 16, 22, 18, 245 });
    DrawRectangleRoundedLinesEx(p, 0.06f, 16, 1.0f, (Color){ 226, 184, 106, 160 });

    DrawTextEx(assets.mainFont, "Статистика",
               (Vector2){ p.x + 24, p.y + 20 }, UI_FONT_TITLE, 0.6f, UI_ACCENT);
    DrawLine((int)(p.x + 24), (int)(p.y + 52), (int)(p.x + p.width - 24), (int)(p.y + 52),
             (Color){ 255, 255, 255, 45 });

    char buf[64];
    float lx = p.x + 24;
    float lw = 292.0f;
    float y = p.y + 68;
    const float step = 28.0f;

    snprintf(buf, sizeof(buf), "%d", stats.games);
    DrawLabelValue(lx, y, lw, "Сыграно партий", buf, false); y += step;

    snprintf(buf, sizeof(buf), "%d", stats.wins);
    DrawLabelValue(lx, y, lw, "Побед", buf, false); y += step;

    snprintf(buf, sizeof(buf), "%.1f%%", (double)Stats_WinRate());
    DrawLabelValue(lx, y, lw, "Винрейт", buf, true); y += step;

    snprintf(buf, sizeof(buf), "%d", stats.streak);
    DrawLabelValue(lx, y, lw, "Текущая серия побед", buf, false); y += step;

    snprintf(buf, sizeof(buf), "%d", stats.bestStreak);
    DrawLabelValue(lx, y, lw, "Лучшая серия", buf, false); y += step;

    snprintf(buf, sizeof(buf), "%d", stats.bestScore);
    DrawLabelValue(lx, y, lw, "Лучший счёт", buf, false); y += step;

    int bm = (int)(stats.bestTime / 60.0);
    int bs = (int)stats.bestTime % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", bm, bs);
    DrawLabelValue(lx, y, lw, "Лучшее время", buf, false); y += step;

    double avg = Stats_AvgTime();
    int am = (int)(avg / 60.0);
    int as = (int)avg % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", am, as);
    DrawLabelValue(lx, y, lw, "Среднее время партии", buf, false); y += step;

    snprintf(buf, sizeof(buf), "по %d %s", g.drawCount, (g.drawCount == 1) ? "карте" : "карты");
    DrawLabelValue(lx, y, lw, "Раздача", buf, false);

    // --- правая колонка: журнал последних партий ---
    float dx = p.x + 336.0f;
    DrawLine((int)dx, (int)(p.y + 62), (int)dx, (int)(p.y + p.height - 36),
             (Color){ 255, 255, 255, 35 });

    float rx = dx + 18.0f;
    DrawTextEx(assets.mainFont, "Последние партии",
               (Vector2){ rx, p.y + 68 }, UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);

    int roomy = (int)((p.height - 128.0f) / 22.0f);
    int shown = stats.recentCount;
    if (shown > roomy) shown = roomy;
    if (shown < 0) shown = 0;

    if (shown == 0) {
        DrawTextEx(assets.mainFont, "пока нет сыгранных партий",
                   (Vector2){ rx, p.y + 96 }, UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);
    } else {
        for (int i = 0; i < shown; i++) {
            const GameResult *r = &stats.recent[i];
            float ry = p.y + 96.0f + i * 22.0f;

            char cell[32];
            snprintf(cell, sizeof(cell), "%d.", i + 1);
            DrawTextEx(assets.mainFont, cell, (Vector2){ rx, ry }, UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);

            DrawTextEx(assets.mainFont, r->win ? "победа" : "проигрыш",
                       (Vector2){ rx + 24, ry }, UI_FONT_SMALL, 0.6f,
                       r->win ? UI_WIN : UI_LOSE);

            snprintf(cell, sizeof(cell), "%d ход.", r->moves);
            DrawTextEx(assets.mainFont, cell, (Vector2){ rx + 118, ry }, UI_FONT_SMALL, 0.6f, UI_TEXT);

            snprintf(cell, sizeof(cell), "по %d", r->draw);
            DrawTextEx(assets.mainFont, cell, (Vector2){ rx + 186, ry }, UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);

            int mins = (int)(r->seconds / 60.0);
            int secs = (int)r->seconds % 60;
            snprintf(cell, sizeof(cell), "%02d:%02d", mins, secs);
            DrawTextEx(assets.mainFont, cell, (Vector2){ rx + 240, ry }, UI_FONT_SMALL, 0.6f, UI_TEXT);

            snprintf(cell, sizeof(cell), "%d очк.", r->score);
            DrawTextEx(assets.mainFont, cell, (Vector2){ rx + 300, ry }, UI_FONT_SMALL, 0.6f, UI_TEXT);
        }
    }

    const char *hint = "S или Esc — закрыть";
    Vector2 hs = MeasureTextEx(assets.mainFont, hint, UI_FONT_SMALL, 0.6f);
    DrawTextEx(assets.mainFont, hint,
               (Vector2){ p.x + (p.width - hs.x) / 2.0f, p.y + p.height - 28 },
               UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);
}

static void DrawHintBanner(int sw, int sh) {
    if (g.hintText[0] == '\0') return;
    if (GetTime() - g.hintTime > 10.0) return;

    float fs = 17.0f;
    Vector2 ts = MeasureTextEx(assets.mainFont, g.hintText, fs, 0.6f);

    Rectangle r = { sw / 2.0f - ts.x / 2.0f - 26.0f, (float)sh - UI_BOTBAR_H - 62.0f, ts.x + 52.0f, 42.0f };
    DrawRectangleRounded(r, 0.5f, 12, (Color){ 16, 22, 18, 235 });
    DrawRectangleRoundedLinesEx(r, 0.5f, 12, 1.0f, (Color){ 226, 184, 106, 180 });
    DrawTextEx(assets.mainFont, g.hintText,
               (Vector2){ r.x + (r.width - ts.x) / 2.0f, r.y + (r.height - ts.y) / 2.0f },
               fs, 0.6f, UI_TEXT);
}

static void DrawTopBar(int sw) {
    DrawRectangle(0, 0, sw, (int)UI_TOPBAR_H, UI_PANEL_TOP);
    DrawLine(0, (int)UI_TOPBAR_H, sw, (int)UI_TOPBAR_H, (Color){ 226, 184, 106, 90 });

    char drawLabel[32];
    snprintf(drawLabel, sizeof(drawLabel), "Раздача: %d", g.drawCount);

    DrawButtonEx(UI_GetButtonNew(),   "Новая  N",      true, false);
    DrawButtonEx(UI_GetButtonUndo(),  "Отмена  U",     true, false);
    DrawButtonEx(UI_GetButtonHint(),  "Подсказка  H",  !g.holding, false);
    DrawButtonEx(UI_GetButtonAuto(),  "Сбор  Пробел",  !g.holding, false);
    DrawButtonEx(UI_GetButtonDraw(),  drawLabel,       true, g.drawCount == 1);
    DrawButtonEx(UI_GetButtonStats(), "Статистика  S", true, g.statsPanelOpen);
}

static void DrawBottomBar(int sw, int sh) {
    DrawRectangle(0, sh - (int)UI_BOTBAR_H, sw, (int)UI_BOTBAR_H, UI_PANEL);
    DrawLine(0, sh - (int)UI_BOTBAR_H, sw, sh - (int)UI_BOTBAR_H, (Color){ 226, 184, 106, 70 });

    char buf[64];
    float y = sh - UI_BOTBAR_H + 9.0f;
    float x = 14.0f;

    double elapsed = GetTime() - g.startTime;
    int mins = (int)(elapsed / 60.0);
    int secs = (int)elapsed % 60;
    snprintf(buf, sizeof(buf), "Время  %02d:%02d", mins, secs);
    DrawTextEx(assets.mainFont, buf, (Vector2){ x, y }, UI_FONT_SMALL, 0.6f, UI_TEXT);
    x += TextW(buf, UI_FONT_SMALL) + 26.0f;

    snprintf(buf, sizeof(buf), "Ходы  %d", g.moveCount);
    DrawTextEx(assets.mainFont, buf, (Vector2){ x, y }, UI_FONT_SMALL, 0.6f, UI_TEXT);
    x += TextW(buf, UI_FONT_SMALL) + 26.0f;

    snprintf(buf, sizeof(buf), "Счёт  %d", g.score);
    DrawTextEx(assets.mainFont, buf, (Vector2){ x, y }, UI_FONT_SMALL, 0.6f, UI_TEXT);

    snprintf(buf, sizeof(buf), "Партий  %d", stats.games);
    Vector2 rs = MeasureTextEx(assets.mainFont, buf, UI_FONT_SMALL, 0.6f);
    float rx = sw - rs.x - 14.0f;
    DrawTextEx(assets.mainFont, buf, (Vector2){ rx, y }, UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);

    snprintf(buf, sizeof(buf), "Винрейт %.1f%%", (double)Stats_WinRate());
    Vector2 ws = MeasureTextEx(assets.mainFont, buf, UI_FONT_SMALL, 0.6f);
    rx -= ws.x + 26.0f;
    DrawTextEx(assets.mainFont, buf, (Vector2){ rx, y }, UI_FONT_SMALL, 0.6f, UI_ACCENT);
}

void Render_HUD(Camera3D camera) {
    (void)camera;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    UpdateLayout();

    DrawTopBar(sw);
    DrawBottomBar(sw, sh);
    DrawHintBanner(sw, sh);

    if (g.isWon) {
        const char *title = "Партия выиграна!";
        char line2[96];
        int mins = (int)((GetTime() - g.startTime) / 60.0);
        int secs = (int)(GetTime() - g.startTime) % 60;
        snprintf(line2, sizeof(line2), "Время %02d:%02d   Счёт %d   Ходов %d",
                 mins, secs, g.score, g.moveCount);

        float fs = 30.0f;
        Vector2 tw = MeasureTextEx(assets.mainFont, title, fs, 0.6f);
        float pw = tw.x + 120.0f;
        if (pw < 520.0f) pw = 520.0f;

        Rectangle r = { (sw - pw) * 0.5f, (sh - 150.0f) * 0.5f, pw, 150.0f };
        DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 90 });
        DrawRectangleRounded(r, 0.08f, 16, (Color){ 16, 22, 18, 245 });
        DrawRectangleRoundedLinesEx(r, 0.08f, 16, 1.0f, (Color){ 226, 184, 106, 190 });

        DrawTextEx(assets.mainFont, title,
                   (Vector2){ r.x + (r.width - tw.x) / 2.0f, r.y + 24.0f }, fs, 0.6f, UI_ACCENT);

        Vector2 l2 = MeasureTextEx(assets.mainFont, line2, UI_FONT_NORMAL, 0.6f);
        DrawTextEx(assets.mainFont, line2,
                   (Vector2){ r.x + (r.width - l2.x) / 2.0f, r.y + 72.0f },
                   UI_FONT_NORMAL, 0.6f, UI_TEXT);

        const char *l3 = "N — новая игра    S — статистика";
        Vector2 l3s = MeasureTextEx(assets.mainFont, l3, UI_FONT_SMALL, 0.6f);
        DrawTextEx(assets.mainFont, l3,
                   (Vector2){ r.x + (r.width - l3s.x) / 2.0f, r.y + 104.0f },
                   UI_FONT_SMALL, 0.6f, UI_TEXT_DIM);
    }

    if (g.statsPanelOpen) DrawStatsPanel();
}

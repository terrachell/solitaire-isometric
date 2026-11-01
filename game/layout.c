#include "layout.h"
#include <math.h>

// Шаг веера: обычный и минимальный (на случай очень длинных столбцов)
#define TAB_GAP_UP        0.50f
#define TAB_GAP_DOWN      0.26f
#define TAB_MIN_GAP_UP    0.10f
#define TAB_MIN_GAP_DOWN  0.06f

// Значение до первого пересчёта (пока окно не спроецировано на экран)
#define TAB_SPAN_FALLBACK 4.20f

// Максимальный вертикальный сдвиг нижней карты столбца (в юнитах мира),
// пересчитывается из реального размера окна в Layout_UpdateScreenLimit().
static float s_maxSpan = TAB_SPAN_FALLBACK;

Vector3 GetStackBasePos(StackType type, int stackIdx) {
    switch (type) {
        case STACK_DECK:
            return (Vector3){ POS_DECK_X, 0.0f, POS_DECK_Z };

        case STACK_WASTE:
            return (Vector3){ POS_WASTE_X, 0.0f, POS_WASTE_Z };

        case STACK_FOUNDATION:
            return (Vector3){ POS_FOUND_X + stackIdx * FOUNDATION_DX, 0.0f, POS_FOUND_Z };

        case STACK_TABLEAU:
            return (Vector3){ POS_TAB_X + stackIdx * TABLEAU_DX, 0.0f, POS_TAB_Z };
    }
    return (Vector3){ 0, 0, 0 };
}

// Ищем предел длины столбца так, чтобы самая нижняя карта оставалась на экране
// (между верхней и нижней панелями UI). Бинарный поиск по проекции в экран.
void Layout_UpdateScreenLimit(Camera3D cam) {
    float bottomY = (float)GetScreenHeight() - UI_BOTBAR_H - 10.0f;
    float topY    = UI_TOPBAR_H + 10.0f;
    if (bottomY < topY + 40.0f) bottomY = topY + 40.0f;

    float lo = 0.30f;
    float hi = 16.0f;

    for (int it = 0; it < 16; it++) {
        float mid = 0.5f * (lo + hi);
        float topOfBottom = 0.0f;
        float botOfBottom = 0.0f;
        bool ok = true;

        for (int i = 0; i < MAX_TABLEAU; i++) {
            Vector3 base = GetStackBasePos(STACK_TABLEAU, i);

            // центр самой нижней карты при таком сдвиге и её нижняя кромка
            Vector3 low  = { base.x, 0.0f, base.z + mid + CARD_H * 0.5f };
            Vector3 high = { base.x, 0.0f, base.z + CARD_H * 0.5f };

            Vector2 sl = GetWorldToScreen(low, cam);
            Vector2 sh = GetWorldToScreen(high, cam);

            if (!isfinite(sl.y) || !isfinite(sh.y)) { ok = false; break; }
            if (sl.y > botOfBottom) botOfBottom = sl.y;
            if (sh.y < topOfBottom || topOfBottom == 0.0f) topOfBottom = sh.y;

            if (sl.y > bottomY) { ok = false; break; }
        }
        (void)topOfBottom;

        if (ok) lo = mid;
        else    hi = mid;
    }

    s_maxSpan = lo;
    if (s_maxSpan < 0.60f) s_maxSpan = 0.60f;

    // верх стопки не должен заезжать под верхнюю панель
    Vector3 firstLow = { 0.0f, 0.0f, POS_TAB_Z - CARD_H * 0.5f };
    Vector2 s = GetWorldToScreen(firstLow, cam);
    if (isfinite(s.y) && s.y < topY) s_maxSpan *= 0.85f;
}

static float TableauSpan(const Stack *s, float gapUp, float gapDown) {
    float span = 0.0f;
    for (int i = 1; i < s->count; i++) span += s->cards[i].faceUp ? gapUp : gapDown;
    return span;
}

// Подбирает шаг веера так, чтобы длинный столбец плавно ужимался
// и его нижняя карта не вылезала за пределы экрана.
void TableauGaps(int stackIdx, float *gapUp, float *gapDown) {
    *gapUp   = TAB_GAP_UP;
    *gapDown = TAB_GAP_DOWN;

    if (stackIdx < 0 || stackIdx >= MAX_TABLEAU) return;
    const Stack *s = &g.tableau[stackIdx];
    if (s->count <= 1) return;

    float span = TableauSpan(s, *gapUp, *gapDown);
    if (span <= s_maxSpan) return;

    // мягкое сжатие: сначала шаг уменьшается чуть быстрее линейного,
    // чтобы столбец «подтягивался» без резкого схлопывания
    float k = s_maxSpan / span;
    k = powf(k, 0.85f);

    *gapUp   = TAB_GAP_UP * k;
    *gapDown = TAB_GAP_DOWN * k;

    if (*gapUp   < TAB_MIN_GAP_UP)   *gapUp   = TAB_MIN_GAP_UP;
    if (*gapDown < TAB_MIN_GAP_DOWN) *gapDown = TAB_MIN_GAP_DOWN;

    // Если даже минимальный шаг не влезает — дожимаем пропорционально
    span = TableauSpan(s, *gapUp, *gapDown);
    if (span > s_maxSpan) {
        float k2 = s_maxSpan / span;
        *gapUp   *= k2;
        *gapDown *= k2;
    }
}

Vector3 StackCardOffset(StackType type, int cardIndex, int stackIdx) {
    switch (type) {
        case STACK_TABLEAU: {
            // stackIdx < 0 — «виртуальный» веер (например, карты в руке)
            if (stackIdx < 0 || stackIdx >= MAX_TABLEAU) {
                return (Vector3){ 0.0f, 0.001f * cardIndex, cardIndex * TAB_GAP_UP };
            }

            float gapUp, gapDown;
            TableauGaps(stackIdx, &gapUp, &gapDown);

            const Stack *s = &g.tableau[stackIdx];
            float z = 0.0f;
            for (int i = 1; i <= cardIndex; i++) {
                if (i < s->count) {
                    z += s->cards[i].faceUp ? gapUp : gapDown;
                } else {
                    // карта вне стопки (анимации) — тянем тем же шагом
                    z += (s->count > 0) ? (s->cards[s->count - 1].faceUp ? gapUp : gapDown) : gapUp;
                }
            }
            return (Vector3){ 0.0f, 0.001f * cardIndex, z };
        }
        case STACK_WASTE:
            // cardIndex здесь — номер карты в окне сброса (0..2)
            return (Vector3){ cardIndex * 0.55f, 0.002f * cardIndex, 0.0f };
        case STACK_DECK:
            return (Vector3){
                0.0f * cardIndex,
                0.002f * cardIndex,
                -0.008f * cardIndex
            };
        default:
            return (Vector3){ 0.0f, 0.002f * cardIndex, 0.0f };
    }
}

#include "game.h"
#include "layout.h"
#include <math.h>

Vector3 ScreenToTablePlane(Camera3D camera, Vector2 screenPos, float planeY) {
    Ray ray = GetScreenToWorldRay(screenPos, camera);
    float dy = ray.direction.y;
    if (fabsf(dy) < 1e-6f) return (Vector3){ 0.0f, planeY, 0.0f };
    float t = (planeY - ray.position.y) / dy;
    return (Vector3){ ray.position.x + t * ray.direction.x, planeY, ray.position.z + t * ray.direction.z };
}

static bool PointInCard(Vector3 p, Vector3 cardCenter, float w, float h) {
    float dx = fabsf(p.x - cardCenter.x);
    float dz = fabsf(p.z - cardCenter.z);
    return (dx <= w * 0.5f) && (dz <= h * 0.5f);
}

bool HitTestCard(Camera3D camera, Vector2 mouseScreen, int *outStackType, int *outStackIdx, int *outCardIdx) {
    Vector3 p = ScreenToTablePlane(camera, mouseScreen, 0.0f);

    for (int col = MAX_TABLEAU - 1; col >= 0; col--) {
        Stack *s = &g.tableau[col];
        Vector3 base = GetStackBasePos(STACK_TABLEAU, col);

        if (s->count == 0) {
            if (PointInCard(p, base, CARD_W, CARD_H)) {
                *outStackType = STACK_TABLEAU; *outStackIdx = col; *outCardIdx = -1;
                return true;
            }
        } else {
            for (int i = s->count - 1; i >= 0; i--) {
                Vector3 off  = StackCardOffset(STACK_TABLEAU, i, col);
                Vector3 center = { base.x + off.x, 0.0f, base.z + off.z };
                if (PointInCard(p, center, CARD_W, CARD_H)) {
                    *outStackType = STACK_TABLEAU; *outStackIdx = col; *outCardIdx = i;
                    return true;
                }
            }
        }
    }

    for (int i = 0; i < MAX_FOUNDATION; i++) {
        Stack *s = &g.foundation[i];
        Vector3 base = GetStackBasePos(STACK_FOUNDATION, i);
        if (s->count == 0) {
            if (PointInCard(p, base, CARD_W, CARD_H)) {
                *outStackType = STACK_FOUNDATION; *outStackIdx = i; *outCardIdx = -1;
                return true;
            }
        } else {
            if (PointInCard(p, base, CARD_W, CARD_H)) {
                *outStackType = STACK_FOUNDATION; *outStackIdx = i; *outCardIdx = s->count - 1;
                return true;
            }
        }
    }

    if (g.wasteWindowCount > 0 && !g.holding) {
        int topWin = -1;
        for (int i = g.wasteWindowCount - 1; i >= 0; i--) {
            if (!g.wasteWindowHeld[i]) { topWin = i; break; }
        }
        if (topWin >= 0) {
            Vector3 base = GetStackBasePos(STACK_WASTE, 0);
            Vector3 off  = StackCardOffset(STACK_WASTE, topWin, 0);
            Vector3 center = { base.x + off.x, 0.0f, base.z + off.z };
            if (PointInCard(p, center, CARD_W, CARD_H)) {
                *outStackType = STACK_WASTE;
                *outStackIdx = 0;
                *outCardIdx = topWin;
                return true;
            }
        }
    }

    Vector3 base = GetStackBasePos(STACK_DECK, 0);
    if (PointInCard(p, base, CARD_W, CARD_H)) {
        *outStackType = STACK_DECK; *outStackIdx = 0; *outCardIdx = -1;
        return true;
    }

    return false;
}

void Handle_Input(Camera3D camera) {
    Vector2 mouse = GetMousePosition();

    // --- Клавиатура ---
    if (IsKeyPressed(KEY_N))     Rules_NewGame();
    if (IsKeyPressed(KEY_U))     Rules_Undo();
    if (IsKeyPressed(KEY_H))     Rules_Hint();
    if (IsKeyPressed(KEY_D))     Rules_ToggleDrawCount();
    if (IsKeyPressed(KEY_S))     g.statsPanelOpen = !g.statsPanelOpen;
    if (IsKeyPressed(KEY_SPACE)) Rules_AutoCollect();
    if (IsKeyPressed(KEY_ESCAPE)) g.statsPanelOpen = false;
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z)) Rules_Undo();

    // --- Кнопки интерфейса ---
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (UI_PointInRect(mouse, UI_GetButtonNew()))   { Rules_NewGame();         return; }
        if (UI_PointInRect(mouse, UI_GetButtonUndo()))  { Rules_Undo();            return; }
        if (UI_PointInRect(mouse, UI_GetButtonHint()))  { Rules_Hint();            return; }
        if (UI_PointInRect(mouse, UI_GetButtonAuto()))  { Rules_AutoCollect();     return; }
        if (UI_PointInRect(mouse, UI_GetButtonDraw()))  { Rules_ToggleDrawCount(); return; }
        if (UI_PointInRect(mouse, UI_GetButtonStats())) { g.statsPanelOpen = !g.statsPanelOpen; return; }
    }

    // Открытая панель статистики перекрывает стол
    if (g.statsPanelOpen) return;

    // Клики по полосам интерфейса не должны двигать карты
    if (mouse.y < UI_TOPBAR_H || mouse.y > GetScreenHeight() - UI_BOTBAR_H) return;

    // Двойной клик по пустому месту — авто-сбор
    static double lastClickTime = 0;
    static bool   lastClickWasEmpty = false;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int st, si, ci;
        if (!HitTestCard(camera, mouse, &st, &si, &ci)) {
            double now = GetTime();
            if (lastClickWasEmpty && (now - lastClickTime) < 0.3) {
                Rules_AutoCollect();
                lastClickWasEmpty = false;
            } else {
                lastClickWasEmpty = true;
            }
            lastClickTime = now;
        } else {
            lastClickWasEmpty = false;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int st, si, ci;
        if (!HitTestCard(camera, mouse, &st, &si, &ci)) {
            Rules_AutoCollect();
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int st, si, ci;
        if (HitTestCard(camera, mouse, &st, &si, &ci)) {
            if (st == STACK_DECK) {
                Rules_NextDeckCard();
                PlaySound(assets.sndDrop);
                return;
            }
            if (ci >= 0) {
                Rules_SelectCard(st, si, ci);
                bool wasHolding = g.holding;
                Rules_BeginHold(ScreenToTablePlane(camera, mouse, 0.0f));
                if (g.holding && !wasHolding) PlaySound(assets.sndLift);
                else if (!g.holding) Rules_ClearSelection();
            }
        } else {
            Rules_ClearSelection();
        }
    }

    if (g.holding) {
        Rules_UpdateHold(ScreenToTablePlane(camera, mouse, 0.0f));
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && g.holding) {
        int st, si, ci;
        if (HitTestCard(camera, mouse, &st, &si, &ci) && (st == STACK_TABLEAU || st == STACK_FOUNDATION)) {
            Rules_Drop(st, si);
        } else {
            Rules_Drop(-1, -1);
        }
    }
}

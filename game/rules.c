#include "game.h"
#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

GameState g;
Assets    assets;

void Rules_ClearHint(void) {
    g.hintStackType  = -1;
    g.hintTargetType = -1;
    g.hintCount      = 1;
    g.hintText[0]    = '\0';
}

// --- История для Undo ---
static GameState history[HISTORY_MAX];
static int historyCount = 0;

static void History_Push(void) {
    GameState snap = g;
    snap.holding = false;
    snap.heldCount = 0;
    snap.heldFromWindowIdx = -1;
    snap.selectedStackType = -1;
    snap.selectedStackIdx = -1;
    snap.selectedCardIdx = -1;

    if (historyCount >= HISTORY_MAX) {
        memmove(&history[0], &history[1], (HISTORY_MAX - 1) * sizeof(GameState));
        historyCount = HISTORY_MAX - 1;
    }
    history[historyCount++] = snap;
}

void Rules_Undo(void) {
    if (historyCount == 0) return;
    if (g.holding) return;

    GameState snap = history[--historyCount];
    double savedStart   = g.startTime;
    bool   savedIsWon   = g.isWon;
    int    savedDraw    = g.drawCount;
    bool   savedOpen    = g.statsPanelOpen;
    g = snap;
    g.startTime = savedStart;
    g.isWon     = savedIsWon;
    g.drawCount = savedDraw;
    g.statsPanelOpen = savedOpen;
    g.hintStackType = -1;
    g.hintTargetType = -1;
    g.hintTime = 0;
    g.hintText[0] = '\0';

    Anim_Clear();
    PlaySound(assets.sndUndo);
}

void Rules_Init(void) {
    memset(&g, 0, sizeof(g));
    g.selectedStackType = -1;
    g.holding = false;
    g.heldCount = 0;
    g.wasteWindowCount = 0;
    g.heldFromWindowIdx = -1;
    g.hintStackType = -1;
    g.hintTargetType = -1;
    g.drawCount = (stats.drawCount == 1) ? 1 : 3;

    g.deck.type = STACK_DECK;
    g.waste.type = STACK_WASTE;
    for (int i = 0; i < MAX_TABLEAU; i++) { g.tableau[i].type = STACK_TABLEAU; g.tableau[i].index = i; }
    for (int i = 0; i < MAX_FOUNDATION; i++) { g.foundation[i].type = STACK_FOUNDATION; g.foundation[i].index = i; }

    historyCount = 0;
    Anim_Clear();
}

void Rules_NewGame(void) {
    // Незаконченная партия, в которой были ходы, засчитывается как поражение
    if (!g.gameRecorded && g.moveCount > 0) {
        g.gameRecorded = true;
        Stats_RecordResult(false);
    }

    memset(g.deck.cards, 0, sizeof(g.deck.cards)); g.deck.count = 0;
    memset(g.waste.cards, 0, sizeof(g.waste.cards)); g.waste.count = 0;
    for (int i = 0; i < MAX_TABLEAU; i++) g.tableau[i].count = 0;
    for (int i = 0; i < MAX_FOUNDATION; i++) g.foundation[i].count = 0;
    g.selectedStackType = -1; g.holding = false; g.heldCount = 0;
    g.wasteWindowCount = 0;
    g.heldFromWindowIdx = -1;
    g.hintStackType = -1;
    g.hintTargetType = -1;
    g.score = 0;
    g.startTime = GetTime();
    g.isWon = false;
    g.moveCount = 0;
    g.gameRecorded = false;
    g.hintTime = 0;
    g.hintText[0] = '\0';
    g.prevFromType = -1;
    g.prevToType   = -1;
    g.posMemoryCount = 0;
    g.posMemoryHead  = 0;

    historyCount = 0;
    Anim_Clear();

    srand((unsigned)GetTime() * 1000); // миллисекунды
    for (int s = 0; s < SUIT_COUNT; s++) {
        for (int r = 1; r <= 13; r++) {
            Card c = { .rank = r, .suit = s, .faceUp = false, .inPlay = true, .held = false };
            g.deck.cards[g.deck.count++] = c;
        }
    }
    for (int i = g.deck.count - 1; i > 0; i--) {
        int j = GetRandomValue(0, i);
        Card tmp = g.deck.cards[i];
        g.deck.cards[i] = g.deck.cards[j];
        g.deck.cards[j] = tmp;
    }

    for (int col = 0; col < MAX_TABLEAU; col++) {
        for (int row = 0; row <= col; row++) {
            Card c = g.deck.cards[--g.deck.count];
            c.faceUp = (row == col);
            g.tableau[col].cards[g.tableau[col].count++] = c;
        }
    }
}

void Rules_ClearSelection(void) {
    g.selectedStackType = -1;
    g.selectedStackIdx = -1;
    g.selectedCardIdx = -1;
}

void Rules_SelectCard(int stackType, int stackIdx, int cardIdx) {
    g.selectedStackType = stackType;
    g.selectedStackIdx = stackIdx;
    g.selectedCardIdx = cardIdx;
}

static Stack *GetStack(int type, int idx) {
    switch (type) {
        case STACK_DECK: return &g.deck;
        case STACK_WASTE: return &g.waste;
        case STACK_TABLEAU: return (idx >= 0 && idx < MAX_TABLEAU) ? &g.tableau[idx] : NULL;
        case STACK_FOUNDATION: return (idx >= 0 && idx < MAX_FOUNDATION) ? &g.foundation[idx] : NULL;
    }
    return NULL;
}

static bool IsValidSequence(Stack *s, int startIdx) {
    if (startIdx >= s->count) return false;
    if (!s->cards[startIdx].faceUp) return false;

    for (int i = startIdx; i < s->count - 1; i++) {
        Card *top = &s->cards[i];
        Card *bottom = &s->cards[i + 1];

        bool topIsRed = (top->suit == SUIT_HEARTS || top->suit == SUIT_DIAMONDS);
        bool bottomIsRed = (bottom->suit == SUIT_HEARTS || bottom->suit == SUIT_DIAMONDS);

        if (top->rank != bottom->rank + 1) return false;
        if (topIsRed == bottomIsRed) return false;
    }
    return true;
}

int Rules_WasteTopWindowIdx(void) {
    for (int i = g.wasteWindowCount - 1; i >= 0; i--) {
        if (!g.wasteWindowHeld[i]) return i;
    }
    return -1;
}

void Rules_BeginHold(Vector3 worldMouse) {
    Rules_ClearHint();
    if (g.selectedStackType < 0) return;
    Stack *s = GetStack(g.selectedStackType, g.selectedStackIdx);
    if (!s) return;

    int startIdx = g.selectedCardIdx;
    if (startIdx < 0 || startIdx >= s->count) return;
    if (!s->cards[startIdx].faceUp) return;

    if (g.selectedStackType == STACK_WASTE) {
        int winIdx = startIdx;
        if (winIdx < 0 || winIdx >= g.wasteWindowCount) return;
        if (g.wasteWindowHeld[winIdx]) return;
        if (winIdx != Rules_WasteTopWindowIdx()) return;

        History_Push();

        g.heldCount = 1;
        g.heldCards[0] = g.wasteWindow[winIdx];
        g.heldCards[0].held = true;
        g.heldFromType = STACK_WASTE;
        g.heldFromIdx = 0;
        g.heldFromWindowIdx = winIdx;
        g.heldPos = worldMouse;
        g.holding = true;
        g.wasteWindowHeld[winIdx] = true;
        Rules_ClearSelection();
        return;
    }

    if (g.selectedStackType == STACK_TABLEAU && !IsValidSequence(s, startIdx)) {
        PlaySound(assets.sndIllegalMove);
        return;
    }

    History_Push();

    g.heldCount = s->count - startIdx;
    for (int i = 0; i < g.heldCount; i++) {
        g.heldCards[i] = s->cards[startIdx + i];
        g.heldCards[i].held = true;
    }
    g.heldFromType = g.selectedStackType;
    g.heldFromIdx = g.selectedStackIdx;
    g.heldFromWindowIdx = -1;
    g.heldPos = worldMouse;
    g.holding = true;

    s->count = startIdx;
    Rules_ClearSelection();
}

void Rules_UpdateHold(Vector3 worldMouse) {
    if (g.holding) g.heldPos = worldMouse;
}

static bool CanMoveToFoundation(Card *c, Stack *dest) {
    if (dest->count == 0) return (c->rank == 1);
    Card *top = &dest->cards[dest->count - 1];
    return (c->suit == top->suit && c->rank == top->rank + 1);
}

static bool CanMoveToTableau(Card *c, Stack *dest) {
    if (dest->count == 0) return (c->rank == 13);
    Card *top = &dest->cards[dest->count - 1];
    bool isRed = (c->suit == SUIT_HEARTS || c->suit == SUIT_DIAMONDS);
    bool topIsRed = (top->suit == SUIT_HEARTS || top->suit == SUIT_DIAMONDS);
    return (isRed != topIsRed && c->rank == top->rank - 1);
}

static void CheckWin(void) {
    int total = 0;
    for (int i = 0; i < MAX_FOUNDATION; i++) total += g.foundation[i].count;
    if (total >= 52) {
        g.isWon = true;
        if (!g.gameRecorded) {
            g.gameRecorded = true;
            Stats_RecordResult(true);
        }
    }
}

void Rules_ToggleDrawCount(void) {
    g.drawCount = (g.drawCount == 1) ? 3 : 1;
    stats.drawCount = g.drawCount;
    Stats_Save();
    Rules_ClearHint();
    snprintf(g.hintText, HINT_TEXT_MAX, "Раздача: по %d %s (начните новую игру для чистого расклада)",
             g.drawCount, (g.drawCount == 1) ? "карте" : "карты");
    g.hintTime = GetTime();
}

void Rules_Drop(int targetStackType, int targetStackIdx) {
    Rules_ClearHint();
    if (!g.holding) return;

    Stack *src = GetStack(g.heldFromType, g.heldFromIdx);
    Stack *dest = GetStack(targetStackType, targetStackIdx);

    bool moved = false;
    bool movedToFoundation = false;
    bool attemptedIllegal = false;
    int historyBeforeDrop = historyCount;

    if (dest != NULL && dest != src) {
        Card *cardToMove = &g.heldCards[0];
        bool canPlace = false;

        if (targetStackType == STACK_FOUNDATION && g.heldCount == 1) {
            canPlace = CanMoveToFoundation(cardToMove, dest);
        } else if (targetStackType == STACK_TABLEAU) {
            canPlace = CanMoveToTableau(cardToMove, dest);
        }

        if (canPlace) {
            for (int i = 0; i < g.heldCount; i++) {
                g.heldCards[i].faceUp = true;
                g.heldCards[i].held = false;
                dest->cards[dest->count++] = g.heldCards[i];
            }
            moved = true;
            movedToFoundation = (targetStackType == STACK_FOUNDATION);

            if (g.heldFromType == STACK_TABLEAU && src->count > 0) {
                src->cards[src->count - 1].faceUp = true;
            }
        } else {
            attemptedIllegal = true;
        }
    }

    if (!moved && src) {
        if (g.heldFromType == STACK_WASTE) {
            if (g.heldFromWindowIdx >= 0 && g.heldFromWindowIdx < g.wasteWindowCount) {
                g.wasteWindowHeld[g.heldFromWindowIdx] = false;
            }
        } else {
            for (int i = 0; i < g.heldCount; i++) {
                g.heldCards[i].faceUp = true;
                g.heldCards[i].held = false;
                src->cards[src->count++] = g.heldCards[i];
            }
        }
        if (historyCount > historyBeforeDrop) historyCount--;
    } else if (moved && g.heldFromType == STACK_WASTE) {
        int winIdx = g.heldFromWindowIdx;
        if (winIdx >= 0 && winIdx < g.wasteWindowCount) {
            g.wasteWindowHeld[winIdx] = true;
            Card target = g.wasteWindow[winIdx];
            for (int i = 0; i < g.waste.count; i++) {
                if (g.waste.cards[i].rank == target.rank && g.waste.cards[i].suit == target.suit) {
                    for (int j = i; j < g.waste.count - 1; j++) g.waste.cards[j] = g.waste.cards[j + 1];
                    g.waste.count--;
                    break;
                }
            }
        }
    }

    if (moved) {
        g.moveCount++;
        g.prevFromType = g.heldFromType;
        g.prevFromIdx  = g.heldFromIdx;
        g.prevToType   = targetStackType;
        g.prevToIdx    = targetStackIdx;
        if (movedToFoundation) {
            g.score += 10;
            PlaySound(assets.sndCardSparkle);
        } else {
            PlaySound(assets.sndDrop);
        }
    } else if (attemptedIllegal) {
        PlaySound(assets.sndIllegalMove);
        if (g.heldFromType == STACK_WASTE && g.heldFromWindowIdx >= 0) {
            g.wasteWindowHeld[g.heldFromWindowIdx] = false;
        }
    } else {
        PlaySound(assets.sndDrop);
    }

    g.holding = false;
    g.heldCount = 0;
    g.heldFromWindowIdx = -1;
    Rules_ClearSelection();

    if (moved) Rules_PositionRemember();

    CheckWin();
}

void Rules_NextDeckCard(void) {
    Rules_ClearHint();
    g.prevFromType = -1;
    g.prevToType   = -1;
    for (int i = 0; i < WASTE_WINDOW_MAX; i++) g.wasteWindowHeld[i] = false;

    if (g.deck.count <= 0) {
        if (g.waste.count == 0) return;
        History_Push();

        Vector3 wasteBase = GetStackBasePos(STACK_WASTE, 0);
        Vector3 deckBase  = GetStackBasePos(STACK_DECK, 0);

        int wasteCountBefore = g.waste.count;
        int deckCountBefore  = g.deck.count;

        // Запоминаем карты и их позиции ДО перемещения
        Card    recycleCards[MAX_STACK_CARDS];
        Vector3 recycleFrom[MAX_STACK_CARDS];
        int winStart = (wasteCountBefore > WASTE_WINDOW_MAX) ? (wasteCountBefore - WASTE_WINDOW_MAX) : 0;

        for (int i = 0; i < wasteCountBefore; i++) {
            recycleCards[i] = g.waste.cards[i];

            int idxInWindow = i - winStart;
            Vector3 from;
            if (idxInWindow >= 0) {
                Vector3 woff = StackCardOffset(STACK_WASTE, idxInWindow, 0);
                from = (Vector3){ wasteBase.x + woff.x, 0.02f, wasteBase.z + woff.z };
            } else {
                from = (Vector3){ wasteBase.x, 0.02f, wasteBase.z };
            }
            recycleFrom[i] = from;
        }

        // Перекладываем карты
        while (g.waste.count > 0) {
            Card c = g.waste.cards[--g.waste.count];
            c.faceUp = false;
            c.held = false;
            g.deck.cards[g.deck.count++] = c;
        }
        g.wasteWindowCount = 0;
        memset(g.wasteWindow, 0, sizeof(g.wasteWindow));
        Rules_PositionRemember();
        g.score -= 5;
        if (g.score < 0) g.score = 0;
        PlaySound(assets.sndCardsClear);

        // Анимация: карты летят из waste в deck (рубашкой вверх)
        for (int i = 0; i < wasteCountBefore; i++) {
            int deckIdx = wasteCountBefore - 1 - i;
            Vector3 dOff = StackCardOffset(STACK_DECK, deckIdx + deckCountBefore, 0);
            Vector3 to   = (Vector3){
                deckBase.x + dOff.x,
                dOff.y + 0.02f,
                deckBase.z + dOff.z
            };
            // Задержка обратна deckIdx: карта, летящая в низ колоды — первой.
            float delay = (wasteCountBefore - 1 - i) * 0.02f;
            Anim_AddCardBackDelayed(recycleCards[i], recycleFrom[i], to, delay);
        }

        return;
    }

    History_Push();

    // Настройка: выкидывать по одной карте или по три
    int draw  = (g.drawCount == 1) ? 1 : 3;
    int count = (g.deck.count >= draw) ? draw : g.deck.count;
    int wasteBefore = g.waste.count;

    for (int i = 0; i < count; i++) {
        Card c = g.deck.cards[--g.deck.count];
        c.faceUp = true;
        c.held = false;
        g.waste.cards[g.waste.count++] = c;
    }

    int total = g.waste.count;
    int winStart = (total > WASTE_WINDOW_MAX) ? (total - WASTE_WINDOW_MAX) : 0;
    int winCount = total - winStart;
    if (winCount > WASTE_WINDOW_MAX) winCount = WASTE_WINDOW_MAX;

    g.wasteWindowCount = winCount;
    for (int i = 0; i < winCount; i++) {
        g.wasteWindow[i] = g.waste.cards[winStart + i];
        g.wasteWindowHeld[i] = false;
    }
    for (int i = winCount; i < WASTE_WINDOW_MAX; i++) {
        memset(&g.wasteWindow[i], 0, sizeof(Card));
        g.wasteWindowHeld[i] = false;
    }

    // --- Анимация раздачи ---
    Vector3 deckBase  = GetStackBasePos(STACK_DECK, 0);
    Vector3 wasteBase = GetStackBasePos(STACK_WASTE, 0);

    for (int i = 0; i < count; i++) {
        int idxInWaste  = wasteBefore + i;
        int idxInWindow = idxInWaste - winStart;
        if (idxInWindow < 0 || idxInWindow >= winCount) continue;

        Card c = g.waste.cards[idxInWaste];

        int deckIdx = g.deck.count + count - 1 - i;
        Vector3 dOff = StackCardOffset(STACK_DECK, deckIdx, 0);
        Vector3 from = { deckBase.x + dOff.x, dOff.y + 0.02f, deckBase.z + dOff.z };

        Vector3 wOff = StackCardOffset(STACK_WASTE, idxInWindow, 0);
        Vector3 to   = { wasteBase.x + wOff.x, wOff.y + 0.02f, wasteBase.z + wOff.z };

        Anim_AddCardDelayed(c, from, to, i * 0.04f);
    }
}

static bool TryMoveTopToFoundation(int stackType, int stackIdx) {
    if (stackType == STACK_TABLEAU) {
        Stack *s = &g.tableau[stackIdx];
        if (s->count == 0) return false;
        Card *c = &s->cards[s->count - 1];
        if (!c->faceUp) return false;

        for (int f = 0; f < MAX_FOUNDATION; f++) {
            Stack *fnd = &g.foundation[f];
            bool can = (fnd->count == 0) ? (c->rank == 1) :
                (c->suit == fnd->cards[fnd->count - 1].suit && c->rank == fnd->cards[fnd->count - 1].rank + 1);
            if (can) {
                // --- Анимация ---
                Vector3 base = GetStackBasePos(STACK_TABLEAU, stackIdx);
                Vector3 off  = StackCardOffset(STACK_TABLEAU, s->count - 1, stackIdx);
                Vector3 from = { base.x + off.x, off.y + 0.02f, base.z + off.z };

                Vector3 fbase = GetStackBasePos(STACK_FOUNDATION, f);
                Vector3 foff  = StackCardOffset(STACK_FOUNDATION, fnd->count, f);
                Vector3 to    = { fbase.x + foff.x, foff.y + 0.02f, fbase.z + foff.z };
                Anim_AddCard(*c, from, to);
                // --- конец ---

                fnd->cards[fnd->count++] = *c;
                s->count--;
                if (s->count > 0) s->cards[s->count - 1].faceUp = true;
                return true;
            }
        }
    } else if (stackType == STACK_WASTE) {
        int topWin = -1;
        for (int i = g.wasteWindowCount - 1; i >= 0; i--) {
            if (!g.wasteWindowHeld[i]) { topWin = i; break; }
        }
        if (topWin < 0) return false;

        Card c = g.wasteWindow[topWin];
        for (int f = 0; f < MAX_FOUNDATION; f++) {
            Stack *fnd = &g.foundation[f];
            bool can = (fnd->count == 0) ? (c.rank == 1) :
                (c.suit == fnd->cards[fnd->count - 1].suit && c.rank == fnd->cards[fnd->count - 1].rank + 1);
            if (can) {
                // --- Анимация ---
                Vector3 wbase = GetStackBasePos(STACK_WASTE, 0);
                Vector3 from  = { wbase.x + topWin * 0.45f, 0.02f, wbase.z };

                Vector3 fbase = GetStackBasePos(STACK_FOUNDATION, f);
                Vector3 foff  = StackCardOffset(STACK_FOUNDATION, fnd->count, f);
                Vector3 to    = { fbase.x + foff.x, foff.y + 0.02f, fbase.z + foff.z };
                Anim_AddCard(c, from, to);
                // --- конец ---

                fnd->cards[fnd->count++] = c;
                g.wasteWindowHeld[topWin] = true;
                for (int i = 0; i < g.waste.count; i++) {
                    if (g.waste.cards[i].rank == c.rank && g.waste.cards[i].suit == c.suit) {
                        for (int j = i; j < g.waste.count - 1; j++) g.waste.cards[j] = g.waste.cards[j + 1];
                        g.waste.count--;
                        break;
                    }
                }
                return true;
            }
        }
    }
    return false;
}

void Rules_AutoCollect(void) {
    Rules_ClearHint();
    if (g.holding) return;

    bool anyPossible = false;
    {
        int topWin = Rules_WasteTopWindowIdx();
        if (topWin >= 0) {
            Card c = g.wasteWindow[topWin];
            for (int f = 0; f < MAX_FOUNDATION; f++) {
                Stack *fnd = &g.foundation[f];
                if ((fnd->count == 0 && c.rank == 1) ||
                    (fnd->count > 0 && c.suit == fnd->cards[fnd->count - 1].suit &&
                     c.rank == fnd->cards[fnd->count - 1].rank + 1)) { anyPossible = true; break; }
            }
        }
        if (!anyPossible) {
            for (int i = 0; i < MAX_TABLEAU; i++) {
                Stack *s = &g.tableau[i];
                if (s->count == 0) continue;
                Card *c = &s->cards[s->count - 1];
                if (!c->faceUp) continue;
                for (int f = 0; f < MAX_FOUNDATION; f++) {
                    Stack *fnd = &g.foundation[f];
                    if ((fnd->count == 0 && c->rank == 1) ||
                        (fnd->count > 0 && c->suit == fnd->cards[fnd->count - 1].suit &&
                         c->rank == fnd->cards[fnd->count - 1].rank + 1)) { anyPossible = true; break; }
                }
                if (anyPossible) break;
            }
        }
    }
    if (!anyPossible) return;

    History_Push();

    bool movedAny = false;
    bool found = true;
    while (found) {
        found = false;
        if (TryMoveTopToFoundation(STACK_WASTE, 0)) { found = true; movedAny = true; g.score += 10; continue; }
        for (int i = 0; i < MAX_TABLEAU; i++) {
            if (TryMoveTopToFoundation(STACK_TABLEAU, i)) { found = true; movedAny = true; g.score += 10; break; }
        }
    }

    if (movedAny) {
        PlaySound(assets.sndCardSparkle);
        CheckWin();
    }
}

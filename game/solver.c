#include "game.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
//  Советник по ходам.
//
//  В отличие от простого «поиска любого валидного хода» здесь строится модель
//  позиции, для неё перебираются ВСЕ допустимые ходы (плюс взятие из колоды),
//  каждый ход проигрывается и оценивается:
//      1) изменение эвристической оценки позиции,
//      2) бонусы за смысл хода (открыл закрытую карту, освободил столбец…),
//      3) лучшие продолжения на два полухода вперёд (перебор в глубину).
//  Игроку показывается объективно лучший ход, а не первый подходящий.
// ---------------------------------------------------------------------------

#define SOLVER_MAX_MOVES 256
#define NO_SCORE         (-1.0e9f)

#ifndef SEARCH_DEPTH
#define SEARCH_DEPTH     2       // полухода: сам ход + лучший ответ на него
#endif
#ifndef REPLY_WEIGHT
#define REPLY_WEIGHT     0.55f   // вес более далёких продолжений
#endif
// Полуходов вглубь считаем 2: ход и лучший ответ на него. Более глубокий перебор
// эвристика не выдерживает — замер на 400 партиях дал падение винрейта
// (2 полухода 25% / 7.5%, 3 полухода 16.5% / 5.5%), поэтому глубина оставлена
// минимально достаточной, а качество подсказки даёт оценка хода + бонусы.
#ifndef MOVE_REPLY_WEIGHT
#define MOVE_REPLY_WEIGHT 1.0f   // вес лучшего ответа в оценке корневого хода
#endif
#define REPLY_DECAY      0.55f   // затухание веса с каждым полуходом
#ifndef INNER_WIDTH
#define INNER_WIDTH      10      // сколько лучших продолжений досматриваем глубже
#endif

typedef struct {
    Card tab[MAX_TABLEAU][MAX_STACK_CARDS];
    int  tabCount[MAX_TABLEAU];
    int  foundRank[SUIT_COUNT];   // верхний ранг в доме масти (0 = пусто)
    Card waste[MAX_STACK_CARDS];
    int  wasteCount;
    bool wasteAvailable;          // верх сброса реально можно взять (окно не исчерпано)
    bool wasteUnknown;            // верх сброса неизвестен (после раздачи)
    int  deckCount;
    int  drawCount;
} SBoard;

typedef struct {
    int  fromType;
    int  fromIdx;
    int  cardIdx;   // индекс нижней карты переносимой группы в столбце
    int  count;     // сколько карт переносим
    int  toType;
    int  toIdx;     // для дома здесь лежит масть (0..3)
    Card card;      // нижняя карта группы
    int  kind;      // 0 — обычный ход, 1 — раздача, 2 — переворот сброса
    float score;
} SMove;

typedef struct {
    int  fromType;
    int  fromIdx;
    int  toType;
    int  toIdx;
    int  srcOldCount;
    int  dstOldCount;
    int  wasteOldCount;
    int  deckOldCount;
    int  foundOldRank;
    int  suit;
    int  revealedIdx;      // индекс карты, которую ход открыл (-1 = нет)
    bool hiddenRevealed;
    bool wasteWasUnknown;
    bool wasteWasAvailable;
} SUndo;

// ------------------------------------------------------------- хэш позиции

static unsigned long HashBoard(const SBoard *b) {
    unsigned long h = 1469598103934665603UL;
    #define HASH_MIX(v) do { h ^= (unsigned long)(v); h *= 1099511628211UL; } while (0)

    for (int i = 0; i < MAX_TABLEAU; i++) {
        HASH_MIX(b->tabCount[i]);
        for (int k = 0; k < b->tabCount[i]; k++) {
            HASH_MIX(b->tab[i][k].rank);
            HASH_MIX(b->tab[i][k].suit);
            HASH_MIX(b->tab[i][k].faceUp ? 1 : 0);
        }
    }
    for (int s = 0; s < SUIT_COUNT; s++) HASH_MIX(b->foundRank[s]);
    HASH_MIX(b->deckCount);
    HASH_MIX(b->wasteCount);
    HASH_MIX(b->wasteAvailable ? 1 : 0);
    #undef HASH_MIX
    return h;
}

static bool IsRed(int suit) {
    return suit == SUIT_HEARTS || suit == SUIT_DIAMONDS;
}

// ---------------------------------------------------------------- доска

static void BoardFromGame(SBoard *b) {
    memset(b, 0, sizeof(*b));

    for (int i = 0; i < MAX_TABLEAU; i++) {
        b->tabCount[i] = g.tableau[i].count;
        if (b->tabCount[i] > 0) {
            memcpy(b->tab[i], g.tableau[i].cards, sizeof(Card) * (size_t)b->tabCount[i]);
        }
    }

    for (int f = 0; f < MAX_FOUNDATION; f++) {
        const Stack *s = &g.foundation[f];
        if (s->count > 0) {
            const Card *top = &s->cards[s->count - 1];
            b->foundRank[top->suit] = top->rank;
        }
    }

    b->wasteCount = g.waste.count;
    if (b->wasteCount > 0) memcpy(b->waste, g.waste.cards, sizeof(Card) * (size_t)b->wasteCount);
    // Верх сброса можно взять только если окно сброса не исчерпано
    b->wasteAvailable = (b->wasteCount > 0) && (Rules_WasteTopWindowIdx() >= 0);
    b->wasteUnknown   = false;

    b->deckCount = g.deck.count;
    b->drawCount = (g.drawCount == 1) ? 1 : 3;
}

unsigned long Rules_PositionHash(void) {
    SBoard b;
    BoardFromGame(&b);
    return HashBoard(&b);
}

void Rules_PositionRemember(void) {
    unsigned long h = Rules_PositionHash();

    for (int i = 0; i < g.posMemoryCount; i++) {
        if (g.posMemory[i] == h) return;   // уже видели — не дублируем
    }

    int slot = g.posMemoryHead;
    if (g.posMemoryCount < POS_MEMORY) g.posMemoryCount++;
    g.posMemory[slot] = h;
    g.posMemoryHead = (slot + 1) % POS_MEMORY;
}

static bool PositionSeen(unsigned long h) {
    for (int i = 0; i < g.posMemoryCount; i++) {
        if (g.posMemory[i] == h) return true;
    }
    return false;
}

// Валидна ли последовательность (убывание по рангу, чередование цвета)
static bool RunValid(const SBoard *b, int col, int start) {
    int cnt = b->tabCount[col];
    if (start < 0 || start >= cnt) return false;
    if (!b->tab[col][start].faceUp) return false;

    for (int i = start; i + 1 < cnt; i++) {
        const Card *a = &b->tab[col][i];
        const Card *c = &b->tab[col][i + 1];
        if (a->rank != c->rank + 1) return false;
        if (IsRed(a->suit) == IsRed(c->suit)) return false;
    }
    return true;
}

static bool CanPlaceOnColumn(const SBoard *b, Card c, int col) {
    int cnt = b->tabCount[col];
    if (cnt == 0) return c.rank == RANK_K;
    const Card *top = &b->tab[col][cnt - 1];
    if (!top->faceUp) return false;
    return (top->rank == c.rank + 1) && (IsRed(top->suit) != IsRed(c.suit));
}

static void AddMove(SMove *mv, int *n, int fromType, int fromIdx, int cardIdx,
                    int count, int toType, int toIdx, Card card) {
    if (*n >= SOLVER_MAX_MOVES) return;
    SMove *m = &mv[*n];
    m->fromType = fromType;
    m->fromIdx  = fromIdx;
    m->cardIdx  = cardIdx;
    m->count    = count;
    m->toType   = toType;
    m->toIdx    = toIdx;
    m->card     = card;
    m->kind     = 0;
    m->score    = 0.0f;
    (*n)++;
}

static int GenMoves(const SBoard *b, SMove *mv) {
    int n = 0;

    for (int i = 0; i < MAX_TABLEAU; i++) {
        int cnt = b->tabCount[i];
        if (cnt == 0) continue;

        for (int start = cnt - 1; start >= 0; start--) {
            if (!b->tab[i][start].faceUp) break;
            if (!RunValid(b, i, start)) continue;

            Card c = b->tab[i][start];
            int moveCount = cnt - start;

            // одиночная верхняя карта — в дом
            if (start == cnt - 1 && b->foundRank[c.suit] == c.rank - 1) {
                AddMove(mv, &n, STACK_TABLEAU, i, start, 1, STACK_FOUNDATION, c.suit, c);
            }

            // группа — в другой столбец
            for (int j = 0; j < MAX_TABLEAU; j++) {
                if (j == i) continue;
                // перенос всего столбца в пустой — пустой ход, не предлагаем
                if (b->tabCount[j] == 0 && start == 0) continue;
                if (!CanPlaceOnColumn(b, c, j)) continue;
                AddMove(mv, &n, STACK_TABLEAU, i, start, moveCount, STACK_TABLEAU, j, c);
            }
        }
    }

    if (b->wasteCount > 0 && b->wasteAvailable && !b->wasteUnknown) {
        Card c = b->waste[b->wasteCount - 1];
        if (b->foundRank[c.suit] == c.rank - 1) {
            AddMove(mv, &n, STACK_WASTE, 0, -1, 1, STACK_FOUNDATION, c.suit, c);
        }
        for (int j = 0; j < MAX_TABLEAU; j++) {
            if (!CanPlaceOnColumn(b, c, j)) continue;
            AddMove(mv, &n, STACK_WASTE, 0, -1, 1, STACK_TABLEAU, j, c);
        }
    }

    // взятие из колоды и переворот сброса — тоже возможные ходы
    if (b->deckCount > 0) {
        Card none = { 0 };
        AddMove(mv, &n, STACK_DECK, 0, -1, 1, STACK_DECK, 0, none);
        mv[n - 1].kind = 1;
    } else if (b->wasteCount > 0) {
        Card none = { 0 };
        AddMove(mv, &n, STACK_DECK, 0, -1, 1, STACK_DECK, 0, none);
        mv[n - 1].kind = 2;
    }

    return n;
}

static void ApplyMove(SBoard *b, const SMove *m, SUndo *u) {
    memset(u, 0, sizeof(*u));
    u->revealedIdx = -1;
    u->fromType   = m->fromType;
    u->fromIdx    = m->fromIdx;
    u->toType     = m->toType;
    u->toIdx      = m->toIdx;
    u->suit       = m->card.suit;
    u->wasteWasUnknown   = b->wasteUnknown;
    u->wasteWasAvailable = b->wasteAvailable;

    if (m->fromType == STACK_TABLEAU) {
        int src = m->fromIdx;
        u->srcOldCount = b->tabCount[src];

        if (m->toType == STACK_FOUNDATION) {
            u->foundOldRank = b->foundRank[u->suit];
            Card c = b->tab[src][--b->tabCount[src]];
            b->foundRank[c.suit] = c.rank;
        } else {
            int dst = m->toIdx;
            u->dstOldCount = b->tabCount[dst];
            for (int i = 0; i < m->count; i++) {
                Card c = b->tab[src][m->cardIdx + i];
                c.faceUp = true;
                c.held   = false;
                b->tab[dst][b->tabCount[dst]++] = c;
            }
            b->tabCount[src] = m->cardIdx;
        }

        // открываем карту под снятой группой
        if (b->tabCount[src] > 0 && !b->tab[src][b->tabCount[src] - 1].faceUp) {
            b->tab[src][b->tabCount[src] - 1].faceUp = true;
            u->hiddenRevealed = true;
            u->revealedIdx    = b->tabCount[src] - 1;
        }

    } else if (m->fromType == STACK_WASTE) {
        u->wasteOldCount = b->wasteCount;
        Card c = b->waste[b->wasteCount - 1];
        b->wasteCount--;
        c.faceUp = true;
        c.held   = false;

        if (m->toType == STACK_FOUNDATION) {
            u->foundOldRank = b->foundRank[c.suit];
            b->foundRank[c.suit] = c.rank;
        } else {
            u->dstOldCount = b->tabCount[m->toIdx];
            b->tab[m->toIdx][b->tabCount[m->toIdx]++] = c;
        }
        b->wasteUnknown   = false;
        b->wasteAvailable = (b->wasteCount > 0);

    } else if (m->fromType == STACK_DECK) {
        u->deckOldCount  = b->deckCount;
        u->wasteOldCount = b->wasteCount;

        if (m->kind == 2) {
            // переворот сброса: все карты сброса уходят в колоду рубашкой вверх
            b->deckCount = b->wasteCount;
            b->wasteCount = 0;
        } else {
            int take = (b->drawCount < b->deckCount) ? b->drawCount : b->deckCount;
            b->deckCount -= take;
            b->wasteCount += take;
        }
        b->wasteAvailable = false;
        // какая именно карта легла сверху — неизвестно
        b->wasteUnknown = true;
    }
}

static void UndoMove(SBoard *b, const SUndo *u) {
    switch (u->fromType) {
        case STACK_TABLEAU:
            // возвращаем «закрытость» карте, которую открыл ход
            if (u->hiddenRevealed && u->revealedIdx >= 0 && u->revealedIdx < MAX_STACK_CARDS) {
                b->tab[u->fromIdx][u->revealedIdx].faceUp = false;
            }
            b->tabCount[u->fromIdx] = u->srcOldCount;
            if (u->toType == STACK_FOUNDATION) {
                b->foundRank[u->suit] = u->foundOldRank;
            } else {
                b->tabCount[u->toIdx] = u->dstOldCount;
            }
            break;

        case STACK_WASTE:
            b->wasteCount     = u->wasteOldCount;
            b->wasteAvailable = u->wasteWasAvailable;
            if (u->toType == STACK_FOUNDATION) {
                b->foundRank[u->suit] = u->foundOldRank;
            } else {
                b->tabCount[u->toIdx] = u->dstOldCount;
            }
            break;

        case STACK_DECK:
            b->deckCount      = u->deckOldCount;
            b->wasteCount     = u->wasteOldCount;
            b->wasteUnknown   = u->wasteWasUnknown;
            b->wasteAvailable = u->wasteWasAvailable;
            break;

        default:
            break;
    }
}

// ------------------------------------------------------------------ оценка

// Веса эвристики (вынесены, чтобы их можно было настраивать и замерять)
#ifndef EVAL_W_FOUND
#define EVAL_W_FOUND   12.0f
#endif
#ifndef EVAL_W_EMPTY
#define EVAL_W_EMPTY   16.0f
#endif
#ifndef EVAL_W_HIDDEN
#define EVAL_W_HIDDEN   4.0f
#endif
#ifndef EVAL_W_RUN
#define EVAL_W_RUN      1.0f
#endif
#ifndef EVAL_W_WASTE
#define EVAL_W_WASTE    0.5f
#endif

static float EvalBoard(const SBoard *b) {
    int hidden    = 0;
    int emptyCols = 0;
    int runBonus  = 0;
    int found     = 0;

    for (int i = 0; i < MAX_TABLEAU; i++) {
        int cnt = b->tabCount[i];
        if (cnt == 0) { emptyCols++; continue; }

        for (int k = 0; k < cnt; k++) {
            if (!b->tab[i][k].faceUp) hidden++;
        }

        // длина упорядоченного хвоста столбца
        int tail = 1;
        for (int k = cnt - 1; k > 0; k--) {
            const Card *a = &b->tab[i][k - 1];
            const Card *c = &b->tab[i][k];
            if (!a->faceUp || !c->faceUp) break;
            if (a->rank != c->rank + 1) break;
            if (IsRed(a->suit) == IsRed(c->suit)) break;
            tail++;
        }
        runBonus += tail * tail;
    }

    for (int s = 0; s < SUIT_COUNT; s++) found += b->foundRank[s];

    float score = 0.0f;
    score += EVAL_W_FOUND  * (float)found;      // прогресс в доме — главная цель
    score += EVAL_W_EMPTY  * (float)emptyCols;  // свободный столбец — сильный ресурс
    score -= EVAL_W_HIDDEN * (float)hidden;     // закрытые карты — плохо
    score += EVAL_W_RUN    * (float)runBonus;   // собранные последовательности
    score -= EVAL_W_WASTE  * (float)b->wasteCount;
    return score;
}

// Смысл хода, который не виден в статической оценке позиции
static float MoveBonus(const SBoard *before, const SMove *m) {
    if (m->fromType == STACK_DECK) {
        // раздача открывает новые карты, переворот сброса даёт новый проход
        return (m->kind == 2) ? 2.0f : 3.0f;
    }

    float bonus = 0.0f;

    // Не советуем ходить туда-обратно: откат последнего хода почти всегда вреден
    if (g.prevFromType >= 0 &&
        m->fromType == g.prevToType && m->fromIdx == g.prevToIdx &&
        m->toType == g.prevFromType && m->toIdx == g.prevFromIdx) {
        bonus -= 40.0f;
    }
    if (m->toType == STACK_FOUNDATION) bonus += 18.0f;

    if (m->fromType == STACK_TABLEAU) {
        int src = m->fromIdx;
        bool revealsHidden = (m->cardIdx > 0) && !before->tab[src][m->cardIdx - 1].faceUp;
        bool emptiesColumn = (m->cardIdx == 0);
        bool destWasEmpty  = (m->toType == STACK_TABLEAU) && (before->tabCount[m->toIdx] == 0);

        if (revealsHidden)      bonus += 30.0f;
        else if (emptiesColumn) bonus += 6.0f;
        else                    bonus -= 6.0f;

        // король (группа) в пустой столбец без вскрытия карт — почти всегда вредно
        if (destWasEmpty && !revealsHidden) bonus -= (m->count > 1) ? 20.0f : 14.0f;
    } else if (m->fromType == STACK_WASTE) {
        bonus += 6.0f;
        if (m->toType == STACK_TABLEAU && before->tabCount[m->toIdx] == 0) bonus -= 12.0f;
    }
    return bonus;
}

// Лучший достижимый прирост оценки за `depth` полуходов.
// Соперника нет, поэтому это чистая максимизация, но с двумя ограничениями:
//   1) глубже спускаемся только по INNER_WIDTH самым выгодным ходам,
//   2) вес продолжения падает с каждым полуходом (дальние ветки шумят).
static float LookAhead(SBoard *b, int depth, float weight) {
    if (depth <= 0) return 0.0f;

    SMove mv[SOLVER_MAX_MOVES];
    int n = GenMoves(b, mv);
    if (n == 0) return 0.0f;

    float base = EvalBoard(b);
    float imm[SOLVER_MAX_MOVES];

    // мгновенная оценка каждого хода — по ней и выбираем, что смотреть глубже
    for (int i = 0; i < n; i++) {
        SUndo u;
        ApplyMove(b, &mv[i], &u);
        imm[i] = (EvalBoard(b) - base) + MoveBonus(b, &mv[i]);
        UndoMove(b, &u);
    }

    int order[SOLVER_MAX_MOVES];
    for (int i = 0; i < n; i++) order[i] = i;

    int take = (n < INNER_WIDTH) ? n : INNER_WIDTH;
    for (int a = 0; a < take; a++) {           // частичная сортировка: top-take
        int best = a;
        for (int c = a + 1; c < n; c++) {
            if (imm[order[c]] > imm[order[best]]) best = c;
        }
        int tmp = order[a]; order[a] = order[best]; order[best] = tmp;
    }

    float best = 0.0f;                          // «ничего не делать» тоже вариант
    for (int a = 0; a < take; a++) {
        int i = order[a];
        SUndo u;
        ApplyMove(b, &mv[i], &u);
        float v = imm[i] + weight * LookAhead(b, depth - 1, weight * REPLY_DECAY);
        UndoMove(b, &u);
        if (v > best) best = v;
    }
    return best;
}

// Итоговая оценка хода: изменение позиции + смысл хода + лучшие продолжения
static float ScoreMove(SBoard *b, const SMove *m) {
    float base = EvalBoard(b);

    SUndo u;
    ApplyMove(b, m, &u);
    float delta  = EvalBoard(b) - base;
    float reply  = MOVE_REPLY_WEIGHT * LookAhead(b, SEARCH_DEPTH - 1, REPLY_WEIGHT);

    // Возврат в уже пройденную позицию — почти всегда хождение по кругу
    float loopPenalty = PositionSeen(HashBoard(b)) ? -60.0f : 0.0f;

    UndoMove(b, &u);

#ifdef SOLVER_DEBUG
    fprintf(stderr, "      delta=%.1f bonus=%.1f reply=%.1f\n",
            (double)delta, (double)MoveBonus(b, m), (double)reply);
#endif
    return delta + MoveBonus(b, m) + reply + loopPenalty;
}

// ---------------------------------------------------------------- вывод

static void RankText(int rank, char *out, size_t n) {
    switch (rank) {
        case RANK_A: snprintf(out, n, "Т");  break;
        case RANK_J: snprintf(out, n, "В");  break;
        case RANK_Q: snprintf(out, n, "Д");  break;
        case RANK_K: snprintf(out, n, "К");  break;
        default:     snprintf(out, n, "%d", rank); break;
    }
}

static void CardText(Card c, char *out, size_t n) {
    char r[8];
    RankText(c.rank, r, sizeof(r));

    const char *suit = "пик";
    switch (c.suit) {
        case SUIT_CLUBS:    suit = "крестей"; break;
        case SUIT_DIAMONDS: suit = "бубён";   break;
        case SUIT_HEARTS:   suit = "червей";  break;
        case SUIT_SPADES:   suit = "пик";     break;
    }
    snprintf(out, n, "%s %s", r, suit);
}

// Склонение слова «карта» для родительного падежа: 2 карты, 5 карт
static const char *CardsWord(int n) {
    int m10  = n % 10;
    int m100 = n % 100;
    if (m10 == 1 && m100 != 11) return "карта";
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return "карты";
    return "карт";
}

static int FoundationIdxForSuit(int suit) {    for (int f = 0; f < MAX_FOUNDATION; f++) {
        int cnt = g.foundation[f].count;
        if (cnt > 0 && g.foundation[f].cards[cnt - 1].suit == suit) return f;
    }
    for (int f = 0; f < MAX_FOUNDATION; f++) {
        if (g.foundation[f].count == 0) return f;
    }
    return 0;
}

static void SetHintFromMove(const SMove *m, int totalMoves) {
    g.hintStackType = m->fromType;
    g.hintCount     = m->count;

    if (m->fromType == STACK_WASTE) {
        int topWin = Rules_WasteTopWindowIdx();
        if (topWin < 0) topWin = 0;
        g.hintStackIdx = topWin;
        g.hintCardIdx  = topWin;
        g.hintCount    = 1;
    } else {
        g.hintStackIdx = m->fromIdx;
        g.hintCardIdx  = m->cardIdx;
    }

    char name[40];
    CardText(m->card, name, sizeof(name));

    if (m->toType == STACK_FOUNDATION) {
        g.hintTargetType = STACK_FOUNDATION;
        g.hintTargetIdx  = FoundationIdxForSuit(m->toIdx);
        snprintf(g.hintText, HINT_TEXT_MAX, "Лучший ход: %s → в дом", name);
    } else {
        g.hintTargetType = STACK_TABLEAU;
        g.hintTargetIdx  = m->toIdx;

        int destCount = g.tableau[m->toIdx].count;
        char group[48] = "";
        if (m->count > 1) snprintf(group, sizeof(group), " (%d %s)", m->count, CardsWord(m->count));

        if (destCount == 0) {
            snprintf(g.hintText, HINT_TEXT_MAX, "Лучший ход: %s%s → пустой столбец %d",
                     name, group, m->toIdx + 1);
        } else {
            char topName[40];
            CardText(g.tableau[m->toIdx].cards[destCount - 1], topName, sizeof(topName));
            snprintf(g.hintText, HINT_TEXT_MAX, "Лучший ход: %s%s на %s (столбец %d)",
                     name, group, topName, m->toIdx + 1);
        }
    }

    (void)totalMoves;
    g.hintTime = GetTime();
}

static void SetDrawHint(void) {
    g.hintStackType  = STACK_DECK;
    g.hintStackIdx   = 0;
    g.hintCardIdx    = -1;
    g.hintCount      = 1;
    g.hintTargetType = -1;
    g.hintTime       = GetTime();

    if (g.deck.count > 0) {
        if (g.drawCount == 1) {
            snprintf(g.hintText, HINT_TEXT_MAX, "Лучший ход: возьмите карту из колоды");
        } else {
            snprintf(g.hintText, HINT_TEXT_MAX, "Лучший ход: возьмите %d карты из колоды", g.drawCount);
        }
    } else {
        snprintf(g.hintText, HINT_TEXT_MAX, "Колода пуста — переверните сброс");
    }
}

void Rules_Hint(void) {
    if (g.holding) return;

    g.hintStackType  = -1;
    g.hintTargetType = -1;
    g.hintCount      = 1;
    g.hintText[0]    = '\0';

    SBoard b;
    BoardFromGame(&b);

    SMove mv[SOLVER_MAX_MOVES];
    int n = GenMoves(&b, mv);

    int   bestIdx   = -1;
    float bestScore = NO_SCORE;

    for (int i = 0; i < n; i++) {
        mv[i].score = ScoreMove(&b, &mv[i]);
        if (mv[i].score > bestScore) {
            bestScore = mv[i].score;
            bestIdx   = i;
        }
    }

#ifdef SOLVER_DEBUG
    for (int i = 0; i < n; i++) {
        char nm[40];
        CardText(mv[i].card, nm, sizeof(nm));
        fprintf(stderr, "  [%3d] %s from %d/%d cnt=%d -> %d/%d kind=%d = %.1f%s\n",
                i, nm, mv[i].fromType, mv[i].fromIdx, mv[i].count,
                mv[i].toType, mv[i].toIdx, mv[i].kind, (double)mv[i].score,
                (i == bestIdx) ? "  <<<" : "");
    }
    fprintf(stderr, "  board: deck=%d waste=%d avail=%d | cols:", b.deckCount, b.wasteCount, (int)b.wasteAvailable);
    for (int i = 0; i < MAX_TABLEAU; i++) fprintf(stderr, " %d", b.tabCount[i]);
    fprintf(stderr, " | found:");
    for (int i = 0; i < SUIT_COUNT; i++) fprintf(stderr, " %d", b.foundRank[i]);
    fprintf(stderr, "\n");
#endif

    if (bestIdx >= 0) {
        if (mv[bestIdx].fromType == STACK_DECK) SetDrawHint();
        else                                    SetHintFromMove(&mv[bestIdx], n);

        PlaySound(assets.sndHintShown);
        return;
    }

    if (g.deck.count > 0 || g.waste.count > 0) {
        SetDrawHint();
        PlaySound(assets.sndHintShown);
        return;
    }

    snprintf(g.hintText, HINT_TEXT_MAX, "Ходов больше нет — отмена (U) или новая игра (N)");
    g.hintTime = GetTime();
    PlaySound(assets.sndIllegalMove);
}

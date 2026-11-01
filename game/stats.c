#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATS_PATH "solitaire_stats.json"

Stats stats;

// --- Чтение одного числа из плоского JSON по ключу ---
static double JsonNum(const char *txt, const char *key, double def) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    const char *p = strstr(txt, pat);
    if (!p) return def;
    p = strchr(p + strlen(pat), ':');
    if (!p) return def;
    p++;

    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return def;
    return v;
}

// --- Чтение числа из ограниченного фрагмента JSON (одна запись журнала) ---
static double JsonNumIn(const char *from, const char *to, const char *key, double def) {
    if (!from || !to || to <= from) return def;

    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t plen = strlen(pat);

    const char *p = NULL;
    for (const char *q = from; q + plen <= to; q++) {
        if (memcmp(q, pat, plen) == 0) { p = q; break; }
    }
    if (!p) return def;

    const char *c = memchr(p + plen, ':', (size_t)(to - (p + plen)));
    if (!c) return def;

    char *end = NULL;
    double v = strtod(c + 1, &end);
    if (end == c + 1) return def;
    return v;
}

// Журнал последних партий: массив объектов "recent": [ {...}, {...} ]
static void LoadRecent(const char *txt) {
    stats.recentCount = 0;

    const char *key = strstr(txt, "\"recent\"");
    if (!key) return;
    const char *arr = strchr(key, '[');
    if (!arr) return;

    const char *cur = arr;
    while (stats.recentCount < STATS_RECENT) {
        const char *ob = strchr(cur, '{');
        if (!ob) break;
        const char *cb = strchr(ob, '}');
        if (!cb) break;

        GameResult r;
        memset(&r, 0, sizeof(r));
        r.win     = (int)JsonNumIn(ob, cb, "win", 0);
        r.moves   = (int)JsonNumIn(ob, cb, "moves", 0);
        r.score   = (int)JsonNumIn(ob, cb, "score", 0);
        r.draw    = (int)JsonNumIn(ob, cb, "draw", 3);
        r.seconds = JsonNumIn(ob, cb, "seconds", 0.0);

        if (r.win  != 0) r.win = 1;
        if (r.moves < 0) r.moves = 0;
        if (r.score < 0) r.score = 0;
        if (r.draw != 1 && r.draw != 3) r.draw = 3;
        if (r.seconds < 0.0) r.seconds = 0.0;

        stats.recent[stats.recentCount++] = r;
        cur = cb + 1;
    }
}

void Stats_Init(void) {
    memset(&stats, 0, sizeof(stats));
    stats.drawCount = 3;

    FILE *f = fopen(STATS_PATH, "rb");
    if (!f) return;

    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (n == 0) return;

    stats.games      = (int)JsonNum(buf, "games", 0);
    stats.wins       = (int)JsonNum(buf, "wins", 0);
    stats.streak     = (int)JsonNum(buf, "streak", 0);
    stats.bestStreak = (int)JsonNum(buf, "bestStreak", 0);
    stats.bestScore  = (int)JsonNum(buf, "bestScore", 0);
    stats.totalMoves = (int)JsonNum(buf, "totalMoves", 0);
    stats.bestTime   = JsonNum(buf, "bestTime", 0.0);
    stats.totalTime  = JsonNum(buf, "totalTime", 0.0);
    stats.drawCount  = (int)JsonNum(buf, "drawCount", 3);

    LoadRecent(buf);

    // Санитарная проверка мусора в файле
    if (stats.games < 0) stats.games = 0;
    if (stats.wins  < 0) stats.wins  = 0;
    if (stats.wins  > stats.games) stats.wins = stats.games;
    if (stats.streak < 0) stats.streak = 0;
    if (stats.bestStreak < stats.streak) stats.bestStreak = stats.streak;
    if (stats.drawCount != 1 && stats.drawCount != 3) stats.drawCount = 3;
}

void Stats_Save(void) {
    FILE *f = fopen(STATS_PATH ".tmp", "wb");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"games\": %d,\n",      stats.games);
    fprintf(f, "  \"wins\": %d,\n",       stats.wins);
    fprintf(f, "  \"winrate\": %.2f,\n",  Stats_WinRate());
    fprintf(f, "  \"streak\": %d,\n",     stats.streak);
    fprintf(f, "  \"bestStreak\": %d,\n", stats.bestStreak);
    fprintf(f, "  \"bestScore\": %d,\n",  stats.bestScore);
    fprintf(f, "  \"bestTime\": %.1f,\n", stats.bestTime);
    fprintf(f, "  \"totalTime\": %.1f,\n", stats.totalTime);
    fprintf(f, "  \"totalMoves\": %d,\n", stats.totalMoves);

    // Журнал последних партий (новые сверху)
    fprintf(f, "  \"recent\": [");
    for (int i = 0; i < stats.recentCount; i++) {
        const GameResult *r = &stats.recent[i];
        fprintf(f, "%s\n    { \"win\": %d, \"moves\": %d, \"score\": %d, \"draw\": %d, \"seconds\": %.1f }",
                (i == 0) ? "" : ",",
                r->win ? 1 : 0, r->moves, r->score, (r->draw == 1) ? 1 : 3, r->seconds);
    }
    fprintf(f, "%s],\n", (stats.recentCount > 0) ? "\n  " : "");

    fprintf(f, "  \"drawCount\": %d\n",   stats.drawCount);
    fprintf(f, "}\n");

    if (fclose(f) != 0) return;
    rename(STATS_PATH ".tmp", STATS_PATH);
}

float Stats_WinRate(void) {
    if (stats.games <= 0) return 0.0f;
    return 100.0f * (float)stats.wins / (float)stats.games;
}

double Stats_AvgTime(void) {
    if (stats.games <= 0) return 0.0;
    return stats.totalTime / (double)stats.games;
}

void Stats_RecordResult(bool win) {
    double elapsed = GetTime() - g.startTime;
    if (elapsed < 0.0) elapsed = 0.0;

    stats.games++;
    stats.totalTime  += elapsed;
    stats.totalMoves += g.moveCount;

    // Сдвигаем журнал: самая свежая партия — первой
    if (stats.recentCount < STATS_RECENT) stats.recentCount++;
    for (int i = stats.recentCount - 1; i > 0; i--) {
        stats.recent[i] = stats.recent[i - 1];
    }
    memset(&stats.recent[0], 0, sizeof(GameResult));
    stats.recent[0].win     = win ? 1 : 0;
    stats.recent[0].moves   = g.moveCount;
    stats.recent[0].score   = g.score;
    stats.recent[0].draw    = (g.drawCount == 1) ? 1 : 3;
    stats.recent[0].seconds = elapsed;

    if (win) {
        stats.wins++;
        stats.streak++;
        if (stats.streak > stats.bestStreak) stats.bestStreak = stats.streak;
        if (g.score > stats.bestScore) stats.bestScore = g.score;
        if (stats.bestTime <= 0.0 || elapsed < stats.bestTime) stats.bestTime = elapsed;
    } else {
        stats.streak = 0;
    }

    Stats_Save();
}

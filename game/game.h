#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include <stdbool.h>

// --- Игровые типы ---
typedef enum { SUIT_CLUBS, SUIT_DIAMONDS, SUIT_HEARTS, SUIT_SPADES, SUIT_COUNT } Suit;
typedef enum { RANK_A = 1, RANK_J = 11, RANK_Q = 12, RANK_K = 13 } Rank;

typedef struct {
    int  rank;
    int  suit;
    bool faceUp;
    bool inPlay;
    bool held;
} Card;

typedef enum {
    STACK_DECK, STACK_WASTE, STACK_TABLEAU, STACK_FOUNDATION
} StackType;

#define MAX_TABLEAU      7
#define MAX_FOUNDATION   4
#define MAX_STACK_CARDS  52
#define TOTAL_CARDS      52

#define WASTE_WINDOW_MAX 3
#define POS_MEMORY       24

#define HISTORY_MAX      50
#define HINT_TEXT_MAX    192

// Высота полос интерфейса (используется и при отрисовке, и при вводе)
#define UI_TOPBAR_H      52.0f
#define UI_BOTBAR_H      34.0f

typedef struct {
    StackType type;
    int index;
    Card cards[MAX_STACK_CARDS];
    int  count;
} Stack;

typedef struct {
    Stack deck;
    Stack waste;
    Stack tableau[MAX_TABLEAU];
    Stack foundation[MAX_FOUNDATION];

    Card wasteWindow[WASTE_WINDOW_MAX];
    int  wasteWindowCount;
    bool wasteWindowHeld[WASTE_WINDOW_MAX];

    int  selectedStackType;
    int  selectedStackIdx;
    int  selectedCardIdx;

    bool  holding;
    Card  heldCards[MAX_STACK_CARDS];
    int   heldCount;
    int   heldFromType;
    int   heldFromIdx;
    int   heldFromWindowIdx;
    Vector3 heldPos;

    // --- Мета ---
    int    score;
    double startTime;
    bool   isWon;

    // --- Настройки / учёт партии ---
    int    drawCount;      // сколько карт выкидывать из колоды: 1 или 3
    int    moveCount;      // число действий в партии
    bool   gameRecorded;   // результат партии уже записан в статистику
    bool   statsPanelOpen; // открыта панель статистики

    // --- Память о пройденных позициях (чтобы советник не зацикливал игру) ---
    unsigned long posMemory[POS_MEMORY];
    int           posMemoryCount;
    int           posMemoryHead;

    // --- Последний сделанный ход (чтобы советник не предлагал отменять его) ---
    int    prevFromType;
    int    prevFromIdx;
    int    prevToType;
    int    prevToIdx;

    // --- Подсказка ---
    int    hintStackType;
    int    hintStackIdx;
    int    hintCardIdx;
    int    hintCount;      // сколько карт в предложенной группе
    double hintTime;       // когда показана (для автоскрытия текста)
    int    hintTargetType;
    int    hintTargetIdx;
    char   hintText[HINT_TEXT_MAX];
} GameState;

// --- Статистика (solitaire_stats.json) ---
#define STATS_RECENT 12

// Один сохранённый результат партии (для журнала последних игр)
typedef struct {
    int    win;
    int    moves;
    int    score;
    int    draw;
    double seconds;
} GameResult;

typedef struct {
    int    games;
    int    wins;
    int    streak;
    int    bestStreak;
    int    bestScore;
    int    totalMoves;
    double bestTime;
    double totalTime;
    int    drawCount;

    int        recentCount;                 // сколько записей журнала заполнено
    GameResult recent[STATS_RECENT];        // новые сверху
} Stats;

typedef struct {
    Texture2D table;
    Texture2D cardTex[SUIT_COUNT][14];
    Texture2D cardBack;
    Texture2D cardNormals[SUIT_COUNT][14];

    Sound sndLift;
    Sound sndDrop;
    Sound sndCardsClear;
    Sound sndCardSparkle;
    Sound sndHintShown;
    Sound sndIllegalMove;
    Sound sndUndo;

    Font mainFont;
    Texture2D glowTex;
} Assets;

extern GameState g;
extern Assets    assets;
extern Stats     stats;

// --- Функции ---
Vector3 StackCardOffset(StackType type, int cardIndex, int stackIdx);

void Rules_Init(void);
void Rules_NewGame(void);
void Rules_SelectCard(int stackType, int stackIdx, int cardIdx);
void Rules_ClearSelection(void);
void Rules_BeginHold(Vector3 worldMouse);
void Rules_UpdateHold(Vector3 worldMouse);
void Rules_Drop(int targetStackType, int targetStackIdx);
void Rules_NextDeckCard(void);
int  Rules_WasteTopWindowIdx(void);

void Rules_Undo(void);
void Rules_Hint(void);
unsigned long Rules_PositionHash(void);
void          Rules_PositionRemember(void);
void Rules_AutoCollect(void);
void Rules_ToggleDrawCount(void);
void Rules_ClearHint(void);

// --- Статистика ---
void  Stats_Init(void);
void  Stats_Save(void);
void  Stats_RecordResult(bool win);
float Stats_WinRate(void);
double Stats_AvgTime(void);

// --- Интерфейс ---
Rectangle UI_GetButtonNew(void);
Rectangle UI_GetButtonUndo(void);
Rectangle UI_GetButtonHint(void);
Rectangle UI_GetButtonAuto(void);
Rectangle UI_GetButtonDraw(void);
Rectangle UI_GetButtonStats(void);
Rectangle UI_GetStatsPanel(void);
bool  UI_PointInRect(Vector2 p, Rectangle r);

void Handle_Input(Camera3D camera);
Vector3 ScreenToTablePlane(Camera3D camera, Vector2 screenPos, float planeY);
bool HitTestCard(Camera3D camera, Vector2 mouseScreen, int *outStackType, int *outStackIdx, int *outCardIdx);

void Render_LoadAssets(void);
void Render_UnloadAssets(void);
void Render_Scene(Camera3D camera);
void Render_HUD(Camera3D camera);

// --- Экспорт из render.c для anim.c ---
void Render_DrawCardAt(Vector3 center, Texture2D tex, Texture2D normalTex, Color tint);

// --- Анимации (anim.c) ---
#define ANIM_MAX       64
#define ANIM_DURATION  0.15f

typedef struct {
    bool    active;
    Card    card;
    Vector3 from;
    Vector3 to;
    float   t;          // 0..1
    float   delay;      // секунды до старта
    float   arcHeight;
    bool    showBack;   // true = рисовать рубашкой
} CardAnim;

void Anim_AddCard(Card card, Vector3 from, Vector3 to);
void Anim_AddCardDelayed(Card card, Vector3 from, Vector3 to, float delay);
void Anim_AddCardBackDelayed(Card card, Vector3 from, Vector3 to, float delay);
void Anim_Update(float dt);
void Anim_Draw(void);
bool Anim_IsCardFlying(Card c);
void Anim_Clear(void);

#endif

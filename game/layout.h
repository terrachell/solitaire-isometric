#ifndef LAYOUT_H
#define LAYOUT_H

#include "raylib.h"
#include "game.h"

#define LIFT_Y           0.6f

// ---------- Размеры карт ----------
#define CARD_W 2.0f
#define CARD_H 2.8f

// ---------- Смещение карт в стопке (веер) ----------
#define CARD_OFFSET_Z    0.5f
#define CARD_THICKNESS   0.02f

// ---------- Позиции стопок ----------
#define POS_DECK_X       -7.40f
#define POS_DECK_Z       -3.40f

#define POS_WASTE_X      -5.00f
#define POS_WASTE_Z      -3.40f

#define POS_FOUND_X      -0.20f
#define POS_FOUND_Z      -3.40f
#define FOUNDATION_DX    2.40f

#define POS_TAB_X        -7.40f
#define POS_TAB_Z        1.00f
#define TABLEAU_DX       2.40f

// ---------- Функции ----------
Vector3 GetStackBasePos(StackType type, int stackIdx);

// Пересчитывает предел длины столбца под текущее окно/камеру:
// стопка ужимается так, чтобы нижняя карта не уходила под панель интерфейса.
void Layout_UpdateScreenLimit(Camera3D cam);

// Текущий шаг веера для столбца (сжимается при длинной стопке)
void TableauGaps(int stackIdx, float *gapUp, float *gapDown);

#endif // LAYOUT_H

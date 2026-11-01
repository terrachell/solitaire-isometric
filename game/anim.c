#include "game.h"
#include "raymath.h"
#include <string.h>

static CardAnim s_anims[ANIM_MAX];

static bool CardEquals(Card a, Card b) {
    return a.rank == b.rank && a.suit == b.suit;
}

void Anim_Clear(void) {
    memset(s_anims, 0, sizeof(s_anims));
}

static void Anim_AddInternal(Card card, Vector3 from, Vector3 to, float delay, bool showBack) {
    for (int i = 0; i < ANIM_MAX; i++) {
        if (!s_anims[i].active) {
            s_anims[i].active    = true;
            s_anims[i].card      = card;
            s_anims[i].from      = from;
            s_anims[i].to        = to;
            s_anims[i].t         = 0.0f;
            s_anims[i].delay     = delay;
            s_anims[i].arcHeight = 0.20f + Vector3Distance(from, to) * 0.04f;
            s_anims[i].showBack  = showBack;
            return;
        }
    }
}

void Anim_AddCard(Card card, Vector3 from, Vector3 to) {
    Anim_AddInternal(card, from, to, 0.0f, false);
}

void Anim_AddCardDelayed(Card card, Vector3 from, Vector3 to, float delay) {
    Anim_AddInternal(card, from, to, delay, false);
}

void Anim_AddCardBackDelayed(Card card, Vector3 from, Vector3 to, float delay) {
    Anim_AddInternal(card, from, to, delay, true);
}

void Anim_Update(float dt) {
    for (int i = 0; i < ANIM_MAX; i++) {
        if (!s_anims[i].active) continue;

        if (s_anims[i].delay > 0.0f) {
            s_anims[i].delay -= dt;
            if (s_anims[i].delay < 0.0f) s_anims[i].delay = 0.0f;
            continue;
        }

        s_anims[i].t += dt / ANIM_DURATION;
        if (s_anims[i].t >= 1.0f) {
            s_anims[i].active = false;
        }
    }
}

bool Anim_IsCardFlying(Card c) {
    for (int i = 0; i < ANIM_MAX; i++) {
        if (s_anims[i].active && CardEquals(s_anims[i].card, c)) return true;
    }
    return false;
}

void Anim_Draw(void) {
    for (int i = 0; i < ANIM_MAX; i++) {
        CardAnim *a = &s_anims[i];
        if (!a->active) continue;
        if (a->delay > 0.0f) continue;

        float t     = a->t;
        float eased = t * t * (3.0f - 2.0f * t);

        Vector3 pos = Vector3Lerp(a->from, a->to, eased);
        pos.y += sinf(eased * PI) * a->arcHeight;

        Texture2D tex  = a->showBack
            ? assets.cardBack
            : assets.cardTex[a->card.suit][a->card.rank];
        Texture2D norm = a->showBack
            ? assets.cardNormals[0][1]
            : assets.cardNormals[a->card.suit][a->card.rank];

        Render_DrawCardAt(pos, tex, norm, WHITE);
    }
}
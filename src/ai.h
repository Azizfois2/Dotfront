#ifndef AI_H
#define AI_H

#include "game.h"

/* Runs the offline opponent for player `p` (0 or 1). Call every frame. */
void ai_update(Game *g, int p, float dt);

#endif /* AI_H */

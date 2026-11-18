#include "render.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

static void fmt_time(char *buf, size_t n, float t) {
    int secs = (int)t;
    snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
}

static void shadow_text(FB *fb, int cx, int y, const char *s, int scale, uint32_t color) {
    gfx_text_center(fb, cx + 2, y + 2, s, scale, 0x99000000);
    gfx_text_center(fb, cx, y, s, scale, color);
}

/* ------------------------------------------------------------------ */

static void draw_minimap(FB *fb, const Game *g, const Renderer *r, const View *v) {
    int mx = fb->w - r->mw - 14;
    int my = fb->h - r->mh - 34;
    int local = g->local;
    gfx_fill(fb, mx - 4, my - 4, r->mw + 8, r->mh + 8, 0xCC0A0F15);
    for (int y = 0; y < r->mh; y++) {
        for (int x = 0; x < r->mw; x++) {
            float wx = ((float)x + 0.5f) / (float)r->mw * (float)g->w.width;
            float wy = ((float)y + 0.5f) / (float)r->mh * (float)g->w.height;
            int cc = (int)(wx / CELL), rr2 = (int)(wy / CELL);
            if (cc > g->w.cols - 1) cc = g->w.cols - 1;
            if (rr2 > g->w.rows - 1) rr2 = g->w.rows - 1;
            float vis = (float)g->vis[local][rr2][cc] / 255.0f;
            float seen = (float)g->seen[local][rr2][cc] / 255.0f;
            float fog = g->spectator ? 0.0f
                      : (1.0f - seen) * 0.88f + seen * (1.0f - vis) * 0.55f;
            uint32_t c0 = r->mini[y * r->mw + x];
            if ((g->spectator || seen > 0.2f) && g->terr_owner[rr2][cc] >= 0) {
                uint32_t oc = owner_color(g->terr_owner[rr2][cc]);
                c0 = rgb((uint8_t)(((c0 >> 16) & 255) * 0.72f + ((oc >> 16) & 255) * 0.28f),
                         (uint8_t)(((c0 >> 8) & 255) * 0.72f + ((oc >> 8) & 255) * 0.28f),
                         (uint8_t)((c0 & 255) * 0.72f + (oc & 255) * 0.28f));
            }
            uint32_t rr = (uint32_t)lerpf((float)((c0 >> 16) & 255), 7.0f, fog);
            uint32_t gg = (uint32_t)lerpf((float)((c0 >> 8) & 255), 11.0f, fog);
            uint32_t bb = (uint32_t)lerpf((float)(c0 & 255), 20.0f, fog);
            gfx_px(fb, mx + x, my + y, 0xFF000000u | (rr << 16) | (gg << 8) | bb);
        }
    }

    for (int i = 0; i < g->ncity; i++) {
        const City *c = &g->cities[i];
        if (!g->spectator &&
            game_seen_at(g, local, c->pos.x, c->pos.y) < 0.05f) continue;
        int owner = g->spectator ? c->owner : game_known_city_owner(g, local, i);
        int x = mx + (int)(c->pos.x / (float)g->w.width * (float)r->mw);
        int y = my + (int)(c->pos.y / (float)g->w.height * (float)r->mh);
        gfx_disc(fb, (float)x, (float)y, 2.6f, 0xFF15151A);
        gfx_disc(fb, (float)x, (float)y, 1.9f, owner_color(owner));
    }
    /* mark the faction's starting city so it is always easy to find */
    if (!g->spectator) {
        Vec2 cp = g->capital[local];
        int x = mx + (int)(cp.x / (float)g->w.width * (float)r->mw);
        int y = my + (int)(cp.y / (float)g->w.height * (float)r->mh);
        gfx_line(fb, (float)x - 4.0f, (float)y - 4.0f,
                 (float)x + 4.0f, (float)y - 4.0f, 1.0f, C_WHITE);
        gfx_line(fb, (float)x - 4.0f, (float)y + 4.0f,
                 (float)x + 4.0f, (float)y + 4.0f, 1.0f, C_WHITE);
        gfx_line(fb, (float)x - 4.0f, (float)y - 4.0f,
                 (float)x - 4.0f, (float)y + 4.0f, 1.0f, C_WHITE);
        gfx_line(fb, (float)x + 4.0f, (float)y - 4.0f,
                 (float)x + 4.0f, (float)y + 4.0f, 1.0f, C_WHITE);
    }
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &g->units[i];
        if (!u->alive) continue;
        if (!g->spectator && u->owner != local &&
            game_visibility_at(g, local, u->pos.x, u->pos.y) < 0.30f) continue;
        int x = mx + (int)(u->pos.x / (float)g->w.width * (float)r->mw);
        int y = my + (int)(u->pos.y / (float)g->w.height * (float)r->mh);
        uint32_t col = owner_color(u->owner);
        gfx_px(fb, x, y, col);
        gfx_px(fb, x + 1, y, col);
        gfx_px(fb, x, y + 1, col);
        gfx_px(fb, x + 1, y + 1, col);
    }

    float cw = (float)v->w / v->zoom / (float)g->w.width * (float)r->mw;
    float ch = (float)v->h / v->zoom / (float)g->w.height * (float)r->mh;
    float cx = v->cam.x / (float)g->w.width * (float)r->mw;
    float cy = v->cam.y / (float)g->w.height * (float)r->mh;
    gfx_rect_a(fb, (float)mx + cx, (float)my + cy,
               fminf(cw, (float)r->mw), fminf(ch, (float)r->mh), C_WHITE, 210);
    gfx_rect(fb, (float)mx - 4, (float)my - 4,
             (float)r->mw + 8, (float)r->mh + 8, 0xFF4A5568);
}

static void draw_topbar(FB *fb, const Game *g) {
    char buf[128];
    int local = g->local;
    gfx_fill_a(fb, 0, 0, fb->w, 46, 0xFF080B10, 205);
    gfx_fill_a(fb, 0, 46, fb->w, 2, 0xFF2A3442, 255);

    if (g->spectator) {
        int alive = 0;
        for (int p = 0; p < g->players; p++)
            if (g->cities_owned[p] > 0 || g->units_owned[p] > 0) alive++;
        gfx_disc(fb, 22.0f, 23.0f, 7.0f, C_WHITE);
        snprintf(buf, sizeof(buf), "SPECTATING  %d FACTIONS ALIVE", alive);
        gfx_text(fb, 36, 12, buf, 2, C_WHITE);
    } else {
        gfx_disc(fb, 22.0f, 23.0f, 7.0f, owner_color(local));
        snprintf(buf, sizeof(buf), "%s  CITIES %d  ARMY %d",
                 owner_name(local), g->cities_owned[local], g->units_owned[local]);
        gfx_text(fb, 36, 12, buf, 2, owner_color(local));
    }

    char t[16];
    fmt_time(t, sizeof(t), g->time);
    gfx_text_center(fb, fb->w / 2, 12, t, 2, C_WHITE);

    int diff = g->difficulty[local < g->players ? local : 0];
    for (int p = 1; p < g->players; p++)
        if (g->difficulty[p] > diff) diff = g->difficulty[p];
    const char *dn = diff == DIFF_EASY ? "EASY"
                   : diff == DIFF_HARD ? "HARD" : "NORMAL";
    snprintf(buf, sizeof(buf), "%d FACTIONS  SEED %u  AI %s",
             g->players, g->w.seed, dn);
    gfx_text(fb, 36, 31, buf, 1, 0xFF8C9AAB);

    /* Only scouted cities and currently visible units are known. */
    int x = fb->w - 18;
    for (int p = g->players - 1; p >= 0; p--) {
        if (!g->spectator && p == local) continue;
        int known, seen_units;
        if (g->spectator) {
            known = g->cities_owned[p];
            seen_units = g->units_owned[p];
        } else {
            known = game_known_city_count(g, local, p);
            seen_units = game_visible_unit_count(g, local, p);
        }
        snprintf(buf, sizeof(buf), "%s %d/%d", owner_name(p), known, seen_units);
        int w = font_text_width(buf, 1);
        x -= w;
        if (x < fb->w / 2 + 46) break;
        int alive = g->cities_owned[p] > 0 || g->units_owned[p] > 0;
        uint32_t col = alive ? owner_color(p) : 0xFF5A5A62;
        gfx_text(fb, x, 14, buf, 1, col);
        gfx_disc(fb, (float)x - 8.0f, 17.0f, 3.2f, col);
        x -= 20;
    }
}

static void draw_selection_info(FB *fb, const Game *g) {
    char buf[128];
    int y = fb->h - 60;
    if (g->nsel > 0) {
        int n[UT_COUNT] = {0, 0, 0};
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!g->selected[i] || !g->units[i].alive) continue;
            int t = g->units[i].type;
            if (t >= 0 && t < UT_COUNT) n[t]++;
        }
        snprintf(buf, sizeof(buf),
                 "%d UNITS  %d SCOUT  %d SOLDIER  %d HEAVY  -  RMB MOVE  G ATTACK-MOVE",
                 g->nsel, n[UT_SCOUT], n[UT_SOLDIER], n[UT_HEAVY]);
    } else if (g->sel_city >= 0 && g->cities[g->sel_city].owner == g->local) {
        const City *c = &g->cities[g->sel_city];
        int t = c->prod_type;
        if (t < 0 || t >= UT_COUNT) t = UT_SOLDIER;
        char q[32] = "";
        if (c->qlen > 0)
            snprintf(q, sizeof(q), "  -  %d QUEUED", c->qlen);
        if (c->fort_t > 0.0f)
            snprintf(buf, sizeof(buf), "FORTIFYING  %.1fs%s  -  RMB RALLY",
                     (double)c->fort_t, q);
        else if (c->fortified)
            snprintf(buf, sizeof(buf), "CITY FORTIFIED  %s%s  -  RMB RALLY",
                     UNIT_TYPE[t].name, q);
        else
            snprintf(buf, sizeof(buf),
                     "CITY %s  -  1/2/3  -  SHIFT+1/2/3 QUEUE%s  -  F FORTIFY  -  RMB RALLY",
                     UNIT_TYPE[t].name, q);
    } else if (g->sel_city >= 0) {
        snprintf(buf, sizeof(buf), "ENEMY / NEUTRAL CITY  -  MOVE UNITS INSIDE TO CAPTURE");
    } else {
        snprintf(buf, sizeof(buf), "LEFT DRAG TO BOX-SELECT  -  RIGHT DRAG TO MOVE");
    }
    gfx_fill_a(fb, 10, y - 4, font_text_width(buf, 2) + 16, 24, 0xFF080B10, 165);
    gfx_text(fb, 18, y, buf, 2, 0xFFCFE3F5);
}

static void draw_help(FB *fb, const Game *g) {
    int h = 24;
    int y = fb->h - h;
    gfx_fill_a(fb, 0, y, fb->w, h, 0xFF080B10, 200);
    const char *help = g->spectator
        ? "SPECTATING - NO FOG   1-6 FOCUS FACTION   ARROWS/WASD PAN   "
          "MIDDLE DRAG PAN   EDGE SCROLL   WHEEL ZOOM   R NEW GAME   Q QUIT"
        : "LMB SELECT   RMB MOVE   SHIFT+RMB STAGE   SPACE SEND/PAUSE   C CLEAR   "
          "G ATTACK   H HOLD   X STOP   Z ALL   F FORTIFY   SHIFT+1/2/3 QUEUE   "
          "CTRL+1-6 GROUP   1-6 RECALL   T HELP   MINIMAP LMB JUMP / RMB ORDER   "
          "F5/F9 SAVE/LOAD   M SOUND   R NEW   ARROWS PAN   WHEEL ZOOM";
    gfx_text(fb, 12, y + 8, help, 1, 0xFF8C9AAB);
}

static void draw_menu(FB *fb, const Game *g) {
    gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 205);
    int cx = fb->w / 2;
    int y = fb->h / 2 - 200;
    shadow_text(fb, cx, y, "WAR OF DOTS", 7, C_WHITE);

    char line[128];
    snprintf(line, sizeof(line), "SEED %u    %d CITIES    %s / %s",
             g->w.seed, g->ncity, world_style_name(g->w.style),
             world_shape_name(g->w.shape));
    shadow_text(fb, cx, y + 86, line, 2, 0xFF9FB4C8);

    snprintf(line, sizeof(line), "<   %d OPPONENTS   >", g->players - 1);
    shadow_text(fb, cx, y + 120, line, 3, C_WHITE);
    shadow_text(fb, cx, y + 154, "LEFT / RIGHT CHOOSE OPPONENTS BEFORE STARTING", 1,
                0xFF71808F);

    /* faction chips so the player knows who is on the map */
    int chip_w = 18;
    int total_w = g->players * chip_w;
    int px = cx - total_w / 2;
    for (int p = 0; p < g->players; p++) {
        gfx_disc(fb, (float)px + 7.0f, (float)y + 184.0f, 6.0f, owner_color(p));
        px += chip_w;
    }

    shadow_text(fb, cx, y + 206, "SPACE   START MATCH", 3, C_WHITE);
    if (g->save_available)
        shadow_text(fb, cx, y + 246, "F9   CONTINUE SAVED MATCH", 2, 0xFFBFD4E6);

    const char *names[DIFF_COUNT] = { "EASY", "NORMAL", "HARD" };
    int total = 0;
    for (int i = 0; i < DIFF_COUNT; i++) total += font_text_width(names[i], 2) + 46;
    int x = cx - total / 2;
    for (int i = 0; i < DIFF_COUNT; i++) {
        char item[32];
        snprintf(item, sizeof(item), "%d %s", i + 1, names[i]);
        uint32_t col = (g->difficulty[1] == i) ? C_WHITE : 0xFF71808F;
        gfx_text(fb, x, y + 290, item, 2, col);
        x += font_text_width(item, 2) + 46;
    }

    shadow_text(fb, cx, y + 336, "R NEW MAP     M SOUND ON/OFF     Q QUIT", 2, 0xFF9FB4C8);
    shadow_text(fb, cx, y + 372, "LMB SELECT   RMB MOVE   SHIFT+RMB STAGE PATH   SPACE SEND", 1,
                0xFF71808F);
    shadow_text(fb, cx, y + 390, "1/2/3 UNIT   SHIFT+1/2/3 QUEUE   T HELP   F FORTIFY   F5/F9 SAVE/LOAD", 1,
                0xFF71808F);
}

static void draw_eliminated(FB *fb, const Game *g) {
    gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 190);
    int cy = fb->h / 2 - 76;
    shadow_text(fb, fb->w / 2, cy, "YOU WERE ELIMINATED", 6, C_WHITE);
    shadow_text(fb, fb->w / 2, cy + 62,
                owner_name(g->local), 3, owner_color(g->local));
    shadow_text(fb, fb->w / 2, cy + 116,
                "R   NEW GAME", 3, 0xFFCFE3F5);
    shadow_text(fb, fb->w / 2, cy + 152,
                "SPACE   WATCH THE REST (NO FOG)", 3, 0xFFCFE3F5);
    shadow_text(fb, fb->w / 2, cy + 188,
                "Q   QUIT", 3, 0xFFCFE3F5);
}

static void draw_tutorial(FB *fb) {
    gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 238);
    int cx = fb->w / 2;
    shadow_text(fb, cx, 42, "HOW TO PLAY", 5, C_WHITE);
    int lx = 84, rx = fb->w / 2 + 36, y = 118;

    gfx_text(fb, lx, y, "SELECT & MOVE", 2, C_GOLD);
    gfx_text(fb, lx, y + 30, "LMB select   drag a box   SHIFT adds units", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 48, "RMB drag draws a route; SHIFT+RMB stages it", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 66, "SPACE sends the stage, else pauses; C clears", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 84, "G attack-move   H hold   X stop   Z select all", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 102, "CTRL+1-6 stores a group; 1-6 recalls it (no city selected)", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 120, "WASD / arrows / middle drag / screen edge scroll", 1, 0xFFCFE3F5);

    gfx_text(fb, lx, y + 142, "CITIES & SUPPLY", 2, C_GOLD);
    gfx_text(fb, lx, y + 172, "Stand inside a city to capture it. More dots capture", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 190, "faster. Your cities produce units automatically.", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 208, "Units heal near friendly cities/territory and lose", 1, 0xFFCFE3F5);
    gfx_text(fb, lx, y + 226, "health on enemy soil. Keep your army supplied.", 1, 0xFFCFE3F5);

    gfx_text(fb, rx, y, "PRODUCTION", 2, C_GOLD);
    gfx_text(fb, rx, y + 30, "Select your city: 1 scout / 2 soldier / 3 heavy", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 48, "SHIFT+1/2/3 queues up to five extra units", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 66, "F fortifies for 10 s; RMB sets the rally point", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 84, "F5 saves the match; F9 loads it again", 1, 0xFFCFE3F5);

    gfx_text(fb, rx, y + 142, "FOG, MAP & CAMERA", 2, C_GOLD);
    gfx_text(fb, rx, y + 172, "Hills see far, forests short. Enemy units and city", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 190, "owners disappear outside your vision.", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 208, "Minimap LMB jumps the camera; RMB gives orders.", 1, 0xFFCFE3F5);
    gfx_text(fb, rx, y + 226, "Spectating: 1-6 focuses a faction, Q quits.", 1, 0xFFCFE3F5);

    shadow_text(fb, cx, fb->h - 70, "T OR ESC   CLOSE HELP", 2, 0xFFBFD4E6);
}

void render_hud(FB *fb, const Game *g, const Renderer *r, const View *v) {
    if (!g->menu && g->winner < 0) {
        draw_topbar(fb, g);
        draw_selection_info(fb, g);
        draw_minimap(fb, g, r, v);
        draw_help(fb, g);
    }

    if (g->notice_t > 0.0f && !g->menu && g->winner < 0) {
        int w = font_text_width(g->notice, 2) + 28;
        gfx_fill_a(fb, (fb->w - w) / 2, fb->h - 104, w, 28, 0xFF080B10, 220);
        gfx_text_center(fb, fb->w / 2, fb->h - 96, g->notice, 2, C_WHITE);
    }

    if (g->winner >= 0) {
        char line[64];
        gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 165);
        int cy = fb->h / 2 - 60;
        char who[48];
        if (g->winner == -2) snprintf(who, sizeof(who), "DRAW");
        else snprintf(who, sizeof(who), "%s WINS", owner_name(g->winner));
        shadow_text(fb, fb->w / 2, cy, who, 7,
                    g->winner >= 0 ? owner_color(g->winner) : C_NEUTRAL);
        char tm[32];
        fmt_time(tm, sizeof(tm), g->time);
        snprintf(line, sizeof(line), "FINAL TIME  %s", tm);
        shadow_text(fb, fb->w / 2, cy + 76, line, 3, C_WHITE);

        shadow_text(fb, fb->w / 2, cy + 108, "FACTION      CAPTURES  BUILT  LOST  PEAK", 1,
                    0xFF9FB4C8);
        int rows = (g->players + 1) / 2;
        for (int p = 0; p < g->players; p++) {
            int col = p / rows, row = p % rows;
            int lx = fb->w / 2 - 250 + col * 260;
            int ly = cy + 128 + row * 16;
            snprintf(line, sizeof(line), "%-7s  %6d %7d %5d %5d",
                     owner_name(p), g->captures[p], g->built[p],
                     g->lost[p], g->peak[p]);
            gfx_text(fb, lx, ly, line, 1, owner_color(p));
        }
        shadow_text(fb, fb->w / 2, cy + 128 + rows * 16 + 22,
                    "R NEW MATCH    F9 LOAD    Q QUIT", 2, 0xFFBFD4E6);
    } else if (g->eliminated) {
        draw_eliminated(fb, g);
    } else if (g->menu) {
        draw_menu(fb, g);
    } else if (g->paused) {
        gfx_fill_a(fb, 0, 0, fb->w, fb->h, 0xFF05070A, 120);
        int starting = g->time < 0.01f;
        shadow_text(fb, fb->w / 2, fb->h / 2 - 40,
                    starting ? "WAR OF DOTS" : "PAUSED", 7, C_WHITE);
        shadow_text(fb, fb->w / 2, fb->h / 2 + 34,
                    starting ? "SPACE TO START" : "SPACE TO RESUME", 3, 0xFFBFD4E6);
    }

    if (v->show_help) draw_tutorial(fb);
}

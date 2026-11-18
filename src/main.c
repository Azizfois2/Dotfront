#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "game.h"
#include "ai.h"
#include "render.h"
#include "platform.h"

#define SIM_DT (1.0f / 60.0f)
#define SAVE_PATH "wod_save.bin"

/* ================================================================== */
/* Headless AI-vs-AI test                                              */
/* ================================================================== */

static const char *diff_name(int d) {
    return d == DIFF_EASY ? "easy" : (d == DIFF_HARD ? "hard" : "normal");
}

static int run_headless(uint32_t seed, float seconds, int diff, int players) {
    Game g;
    game_init(&g, seed, players);
    for (int p = 0; p < g.players; p++) {
        g.difficulty[p] = diff;
        g.ai_enabled[p] = 1;
    }
    g.paused = 0;

    int tcount[T_COUNT] = {0};
    for (int r2 = 0; r2 < g.w.rows; r2++)
        for (int c2 = 0; c2 < g.w.cols; c2++) tcount[g.w.terr[r2][c2]]++;
    printf("difficulty %s, players %d, map %s / %s %dx%d (%d cols x %d rows)\n",
           diff_name(diff), g.players, world_style_name(g.w.style),
           world_shape_name(g.w.shape), g.w.width, g.w.height, g.w.cols, g.w.rows);
    printf("seed %u: %d cities  terrain water %d plains %d forest %d hill %d mountain %d\n",
           seed, g.ncity, tcount[T_WATER], tcount[T_PLAINS], tcount[T_FOREST],
           tcount[T_HILL], tcount[T_MOUNTAIN]);
    for (int p = 0; p < g.players; p++)
        printf("  %s capital (%4.0f,%3.0f)  cities %d  units %d\n",
               owner_name(p), g.capital[p].x, g.capital[p].y,
               g.cities_owned[p], g.units_owned[p]);

    float t = 0.0f;
    int next_report = 15;
    while (t < seconds && g.winner < 0) {
        game_update(&g, SIM_DT);
        for (int p = 0; p < g.players; p++)
            if (g.ai_enabled[p]) ai_update(&g, p, SIM_DT);
        t += SIM_DT;
        if (t >= (float)next_report) {
            int busy[MAX_PLAYERS] = { 0 };
            for (int i = 0; i < MAX_UNITS; i++) {
                const Unit *u = &g.units[i];
                if (!u->alive) continue;
                if (u->pi < u->plen) busy[u->owner]++;
            }
            printf("  t=%3ds", (int)t);
            for (int p = 0; p < g.players; p++) {
                printf("  %s:%dc/%du/%db", owner_name(p), g.cities_owned[p],
                       g.units_owned[p], busy[p]);
            }
            printf("\n");
            for (int c = 0; c < g.ncity; c++) {
                const City *city = &g.cities[c];
                int total = 0;
                int counts[MAX_PLAYERS] = { 0 };
                for (int i = 0; i < MAX_UNITS; i++) {
                    const Unit *u = &g.units[i];
                    if (!u->alive || vdist(u->pos, city->pos) >= CITY_CAPTURE_R) continue;
                    counts[u->owner]++;
                    total++;
                }
                if (total == 0) continue;
                printf("      city %2d owner=%-2d cap=%.2f", c, city->owner,
                       city->cap);
                for (int p = 0; p < g.players; p++)
                    if (counts[p]) printf("  %s=%d", owner_name(p), counts[p]);
                printf("\n");
            }
            next_report += 15;
        }
    }
    if (g.winner == -2) printf("result: DRAW at t=%.1fs\n", (double)t);
    else if (g.winner < 0) printf("result: TIMEOUT at t=%.1fs\n", (double)t);
    else printf("result: %s WINS at t=%.1fs\n", owner_name(g.winner), (double)t);
    for (int p = 0; p < g.players; p++)
        printf("  %s: %d cities, %d units, %d captures\n", owner_name(p),
               g.cities_owned[p], g.units_owned[p], g.captures[p]);
    return g.winner < 0 ? 2 : 0;
}

/* ================================================================== */
/* Interactive game                                                    */
/* ================================================================== */

typedef struct {
    int left_down, middle_down, right_down;
    int shift_down;
    int dragging_box;
    Vec2 press_world;
    int press_x, press_y;
    int last_mx, last_my;
    int edge_armed;             /* ignore edge scroll until the mouse is used */
    int path_active;
    Vec2 path[VIEW_PATH_MAX];
    int plen;
    Vec2 last_path_pt;
    /* staged: Shift+RMB appends segments; Space sends, C clears */
    int staging;
    Vec2 stage[VIEW_PATH_MAX];
    int stage_len;
    int stage_marks[8];
    int stage_marks_n;
    Vec2 last_stage_pt;
    uint8_t down[PK_COUNT];
} Input;

static void clamp_camera(View *v, const Game *g) {
    float view_w = (float)v->w / v->zoom;
    float view_h = (float)v->h / v->zoom;
    float maxx = (float)g->w.width - view_w;
    float maxy = (float)g->w.height - view_h;
    if (maxx < 0.0f) v->cam.x = maxx * 0.5f;
    else v->cam.x = clampf(v->cam.x, 0.0f, maxx);
    if (maxy < 0.0f) v->cam.y = maxy * 0.5f;
    else v->cam.y = clampf(v->cam.y, 0.0f, maxy);
}

static Vec2 mouse_world(const View *v, int mx, int my) {
    return v2(v->cam.x + (float)mx / v->zoom, v->cam.y + (float)my / v->zoom);
}

static void reset_view(View *v, const Game *g) {
    float fit = fmaxf((float)v->w / (float)g->w.width,
                      (float)v->h / (float)g->w.height);
    /* Start close enough that the whole map does not fit on screen: players
     * should have to pan, especially vertically, to see the whole shape.
     * Centre on the local faction's starting city, not on the map centre. */
    v->zoom = fmaxf(fit * 1.25f, 1.0f);
    Vec2 cap = g->capital[g->local];
    v->cam.x = cap.x - (float)v->w / v->zoom * 0.5f;
    v->cam.y = cap.y - (float)v->h / v->zoom * 0.5f;
    clamp_camera(v, g);
}

/* Spectators watch the whole map: no fog, and a full-map starting view. */
static void view_whole_map(View *v, const Game *g) {
    float fit = fmaxf((float)v->w / (float)g->w.width,
                      (float)v->h / (float)g->w.height);
    v->zoom = fit * 0.97f;
    v->cam.x = ((float)g->w.width - (float)v->w / v->zoom) * 0.5f;
    v->cam.y = ((float)g->w.height - (float)v->h / v->zoom) * 0.5f;
    clamp_camera(v, g);
}

static void begin_spectate(Game *g, View *v) {
    g->eliminated = 0;
    g->spectator = 1;
    g->paused = 0;
    view_whole_map(v, g);
    game_notice(g, "SPECTATING - NO FOG - Q QUIT");
}

static void apply_path(Game *g, Input *in) {
    if (in->plen <= 0) return;
    if (g->nsel > 0) game_order_selected(g, in->path, in->plen);
    else if (g->sel_city >= 0 && g->cities[g->sel_city].owner == g->local)
        game_set_city_rally(g, in->path[in->plen - 1]);
    in->plen = 0;
}

static void clear_stage(Input *in) {
    in->stage_len = 0;
    in->stage_marks_n = 0;
    in->staging = 0;
}

static void submit_stage(Game *g, Input *in) {
    if (in->stage_len <= 0) return;
    if (g->nsel > 0) game_order_selected(g, in->stage, in->stage_len);
    else if (g->sel_city >= 0 && g->cities[g->sel_city].owner == g->local)
        game_set_city_rally(g, in->stage[in->stage_len - 1]);
    clear_stage(in);
}

static void reset_after_load(Game *g, View *v, Renderer *r, Input *in) {
    render_free(r);
    render_init(r, g);
    if (g->spectator) view_whole_map(v, g);
    else reset_view(v, g);
    in->plen = 0;
    in->path_active = 0;
    clear_stage(in);
    in->dragging_box = 0;
    in->left_down = in->right_down = in->middle_down = 0;
}

static void new_match(Game *g, View *v, Renderer *r, Input *in,
                      uint32_t *seed, int *ai_diff, int players) {
    uint32_t s = (*seed) * 1664525u + 1013904223u;
    *seed = s;
    game_init(g, s, players);
    for (int p = 1; p < g->players; p++) g->difficulty[p] = *ai_diff;
    g->save_available = (uint8_t)game_save_exists(SAVE_PATH);
    reset_after_load(g, v, r, in);
    g->menu = 1;
    g->paused = 1;
}

/* Sound masks and PSND_* share their order. */
static void play_sfx(Game *g, Platform *plat, double now) {
    static double last[SFX_COUNT];
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!(g->sfx & (1u << i))) continue;
        double gap = (i == SFX_DEATH) ? 0.18 : 0.10;
        if (g->sound_on && now - last[i] >= gap) {
            plat_sound(i);
            last[i] = now;
        }
    }
    g->sfx = 0;
}

static void handle_key(Game *g, View *v, Renderer *r, Input *in,
                       PlatKey key, uint32_t *seed, int *running, int *ai_diff,
                       int *players) {
    if (g->eliminated) {
        if (key == PK_SPACE) {
            begin_spectate(g, v);
            g->sfx |= (uint16_t)SFX_UI;
        } else if (key == PK_R) {
            new_match(g, v, r, in, seed, ai_diff, *players);
            printf("new game, %d factions, seed %u, map %s / %s, AI %s\n",
                   *players, *seed, world_style_name(g->w.style),
                   world_shape_name(g->w.shape), diff_name(*ai_diff));
        } else if (key == PK_Q) {
            *running = 0;
        }
        return;
    }
    switch (key) {
    case PK_ESC:
        if (g->menu) *running = 0;
        else game_clear_selection(g);
        break;
    case PK_Z:
        if (!g->menu) game_select_all(g);
        break;
    case PK_SPACE:
        if (g->menu) {
            g->menu = 0;
            g->paused = 0;
            g->sfx |= (uint16_t)SFX_UI;
        } else if (g->winner < 0 && in->stage_len > 0) {
            submit_stage(g, in);
            g->sfx |= (uint16_t)SFX_ORDER;
            game_notice(g, "STAGED ORDERS SENT");
        } else if (g->winner < 0) {
            g->paused = !g->paused;
            g->sfx |= (uint16_t)SFX_UI;
        }
        break;
    case PK_C:
        if (!g->menu) {
            clear_stage(in);
            in->plen = 0;
            in->path_active = 0;
            game_notice(g, "STAGED PATH CLEARED");
        }
        break;
    case PK_LEFT:
    case PK_RIGHT:
        if (g->menu) {
            int n = *players + (key == PK_RIGHT ? 1 : -1);
            if (n < 2) n = MAX_PLAYERS;
            if (n > MAX_PLAYERS) n = 2;
            *players = n;
            new_match(g, v, r, in, seed, ai_diff, *players);
            printf("new game, %d factions, seed %u, map %s / %s\n",
                   *players, *seed, world_style_name(g->w.style),
                   world_shape_name(g->w.shape));
        }
        break;
    case PK_1:
    case PK_2:
    case PK_3:
        if (g->menu) {
            *ai_diff = (int)key - (int)PK_1;
            for (int p = 1; p < g->players; p++) g->difficulty[p] = *ai_diff;
        } else {
            game_set_city_production(g, (int)key - (int)PK_1);
            g->sfx |= (uint16_t)SFX_UI;
        }
        break;
    case PK_R:
        new_match(g, v, r, in, seed, ai_diff, *players);
        printf("new game, %d factions, seed %u, map %s / %s, AI %s\n",
               *players, *seed, world_style_name(g->w.style),
               world_shape_name(g->w.shape), diff_name(*ai_diff));
        break;
    case PK_M:
        g->sound_on = !g->sound_on;
        g->sfx |= (uint16_t)SFX_UI;
        game_notice(g, g->sound_on ? "SOUND ON" : "SOUND OFF");
        break;
    case PK_F5:
        if (!g->menu && !g->eliminated && g->winner < 0) {
            if (game_save(g, SAVE_PATH)) {
                g->save_available = 1;
                g->sfx |= (uint16_t)SFX_UI;
                game_notice(g, "MATCH SAVED");
            } else {
                game_notice(g, "SAVE FAILED");
            }
        }
        break;
    case PK_F9:
        if (game_load(g, SAVE_PATH)) {
            *ai_diff = g->difficulty[1];
            *players = g->players;
            reset_after_load(g, v, r, in);
            g->sfx |= (uint16_t)SFX_UI;
        } else {
            game_notice(g, "NO VALID SAVE");
        }
        break;
    case PK_G:
        if (!g->menu && g->nsel > 0) {
            game_order_attack_move(g, mouse_world(v, in->last_mx, in->last_my));
            g->sfx |= (uint16_t)SFX_ORDER;
        }
        break;
    case PK_H:
        if (!g->menu && g->nsel > 0) {
            game_order_hold(g);
            g->sfx |= (uint16_t)SFX_ORDER;
        }
        break;
    case PK_X:
        if (!g->menu && g->nsel > 0) {
            game_order_stop(g);
            g->sfx |= (uint16_t)SFX_ORDER;
        }
        break;
    case PK_F:
        if (!g->menu) {
            game_fortify_city(g);
            g->sfx |= (uint16_t)SFX_UI;
        }
        break;
    case PK_Q:
        *running = 0;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint32_t seed = (uint32_t)time(NULL);
    float headless = 0.0f;
    int ai_diff = DIFF_NORMAL;
    int players = 4;
    int spectate = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--players") && i + 1 < argc) {
            players = (int)strtol(argv[++i], NULL, 10);
            if (players < 2) players = 2;
            if (players > MAX_PLAYERS) players = MAX_PLAYERS;
        }
        else if (!strcmp(argv[i], "--spectate")) spectate = 1;
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) {
            const char *d = argv[++i];
            if (!strcmp(d, "easy")) ai_diff = DIFF_EASY;
            else if (!strcmp(d, "normal")) ai_diff = DIFF_NORMAL;
            else if (!strcmp(d, "hard")) ai_diff = DIFF_HARD;
            else {
                fprintf(stderr, "unknown difficulty '%s' (easy, normal, hard)\n", d);
                return 2;
            }
        } else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--seed N] [--players 2-6] [--headless SECONDS] "
                   "[--difficulty easy|normal|hard]\n"
                   "  default        play blue against 2-6 offline AI factions\n"
                   "  --players N    factions in the match (default 4)\n"
                   "  --headless N   run AI vs AI for N simulated seconds\n"
                   "  --spectate     let the AI play blue too, then watch\n"
                   "  --difficulty   AI profile (also 1/2/3 in the menu)\n", argv[0]);
            return 0;
        }
    }
    if (headless > 0.0f) return run_headless(seed, headless, ai_diff, players);

    Platform *plat = plat_open("WAR OF DOTS - C", WIN_W, WIN_H);
    if (!plat) {
        fprintf(stderr, "cannot open window (try --headless)\n");
        return 1;
    }
    FB fb = { .w = WIN_W, .h = WIN_H, .px = plat_pixels(plat) };

    Game g;
    game_init(&g, seed, players);
    for (int p = 1; p < g.players; p++) g.difficulty[p] = ai_diff;
    g.save_available = (uint8_t)game_save_exists(SAVE_PATH);
    if (spectate) {
        g.ai_enabled[0] = 1;
        g.menu = 0;
        g.paused = 0;
        g.spectator = 1;
    }
    printf("WAR OF DOTS (C) - seed %u, %d factions, %s / %s %dx%d, %d cities, AI %s%s\n",
           seed, g.players, world_style_name(g.w.style),
           world_shape_name(g.w.shape), g.w.width, g.w.height,
           g.ncity, diff_name(ai_diff), spectate ? ", spectating" : "");

    Renderer r;
    render_init(&r, &g);

    View v = { .w = WIN_W, .h = WIN_H };
    if (g.spectator) view_whole_map(&v, &g);
    else reset_view(&v, &g);
    Input in;
    memset(&in, 0, sizeof(in));
    in.last_mx = -1;
    in.last_my = -1;

    int running = 1;
    double last = plat_time();

    while (running) {
        PlatEvent ev;
        while (plat_poll(plat, &ev)) {
            switch (ev.type) {
            case PEV_QUIT:
                running = 0;
                break;
            case PEV_KEYDOWN:
                if (ev.key > PK_NONE && ev.key < PK_COUNT && !in.down[ev.key]) {
                    in.down[ev.key] = 1;
                    if (ev.key == PK_SHIFT) in.shift_down = 1;
                    else handle_key(&g, &v, &r, &in, ev.key, &seed, &running,
                                    &ai_diff, &players);
                }
                break;
            case PEV_KEYUP:
                if (ev.key > PK_NONE && ev.key < PK_COUNT) in.down[ev.key] = 0;
                if (ev.key == PK_SHIFT) in.shift_down = 0;
                break;
            case PEV_MOUSEDOWN: {
                if (g.menu || g.winner >= 0) break;
                int mx = ev.x, my = ev.y;
                if (ev.button == 1) {
                    in.left_down = 1;
                    in.press_x = mx;
                    in.press_y = my;
                    in.press_world = mouse_world(&v, mx, my);
                    in.dragging_box = 0;
                } else if (ev.button == 3) {
                    in.right_down = 1;
                    int can_order = g.nsel > 0 ||
                        (g.sel_city >= 0 && g.cities[g.sel_city].owner == g.local);
                    if (can_order && in.shift_down && in.stage_len < VIEW_PATH_MAX) {
                        in.staging = 1;
                        Vec2 w = mouse_world(&v, mx, my);
                        in.last_stage_pt = w;
                        in.stage[in.stage_len++] = w;
                    } else if (can_order) {
                        clear_stage(&in);
                        in.path_active = 1;
                        in.plen = 0;
                        in.path[in.plen++] = mouse_world(&v, mx, my);
                        in.last_path_pt = in.path[0];
                    }
                } else if (ev.button == 2) {
                    in.middle_down = 1;
                    in.last_mx = mx;
                    in.last_my = my;
                }
                break;
            }
            case PEV_MOUSEUP: {
                if (g.menu || g.winner >= 0) break;
                int mx = ev.x, my = ev.y;
                if (ev.button == 1) {
                    if (in.dragging_box) {
                        game_box_select(&g, in.press_world, mouse_world(&v, mx, my),
                                        in.shift_down);
                    } else {
                        game_click_select(&g, in.press_world, in.shift_down);
                    }
                    if (g.nsel > 0 || g.sel_city >= 0) g.sfx |= (uint16_t)SFX_SELECT;
                    in.left_down = 0;
                    in.dragging_box = 0;
                } else if (ev.button == 3) {
                    if (in.staging) {
                        Vec2 w = mouse_world(&v, mx, my);
                        if (in.stage_len < VIEW_PATH_MAX &&
                            (in.stage_len == 0 ||
                             vdist(w, in.stage[in.stage_len - 1]) > 4.0f))
                            in.stage[in.stage_len++] = w;
                        if (in.stage_marks_n < 8 && in.stage_len > 0)
                            in.stage_marks[in.stage_marks_n++] = in.stage_len - 1;
                        in.staging = 0;
                        game_notice(&g, "STAGE SET - SPACE SEND, C CLEAR");
                        g.sfx |= (uint16_t)SFX_UI;
                    } else if (in.path_active) {
                        apply_path(&g, &in);
                        g.sfx |= (uint16_t)SFX_ORDER;
                        in.path_active = 0;
                    }
                    in.right_down = 0;
                } else if (ev.button == 2) {
                    in.middle_down = 0;
                }
                break;
            }
            case PEV_MOUSEMOVE: {
                int mx = ev.x, my = ev.y;
                if (g.menu || g.winner >= 0) { in.last_mx = mx; in.last_my = my; break; }
                if (in.left_down) {
                    int dx = mx - in.press_x, dy = my - in.press_y;
                    if (dx * dx + dy * dy > 36) in.dragging_box = 1;
                }
                if (in.middle_down) {
                    v.cam.x -= (float)(mx - in.last_mx) / v.zoom;
                    v.cam.y -= (float)(my - in.last_my) / v.zoom;
                    clamp_camera(&v, &g);
                }
                if (in.path_active && in.plen < VIEW_PATH_MAX) {
                    Vec2 w = mouse_world(&v, mx, my);
                    if (vdist(w, in.last_path_pt) > 16.0f) {
                        in.path[in.plen++] = w;
                        in.last_path_pt = w;
                    }
                }
                if (in.staging && in.stage_len < VIEW_PATH_MAX) {
                    Vec2 w = mouse_world(&v, mx, my);
                    if (vdist(w, in.last_stage_pt) > 16.0f) {
                        in.stage[in.stage_len++] = w;
                        in.last_stage_pt = w;
                    }
                }
                if (mx > 12 && mx < fb.w - 12 && my > 12 && my < fb.h - 12)
                    in.edge_armed = 1;
                in.last_mx = mx;
                in.last_my = my;
                break;
            }
            case PEV_WHEEL: {
                if (g.menu || g.winner >= 0) break;
                float nz = v.zoom;
                if (ev.wheel > 0) {
                    for (int i2 = 0; i2 < ev.wheel; i2++) nz *= 1.15f;
                } else {
                    for (int i2 = 0; i2 > ev.wheel; i2--) nz /= 1.15f;
                }
                nz = clampf(nz, 0.65f, 3.0f);
                Vec2 w = mouse_world(&v, ev.x, ev.y);
                v.zoom = nz;
                v.cam.x = w.x - (float)ev.x / nz;
                v.cam.y = w.y - (float)ev.y / nz;
                clamp_camera(&v, &g);
                break;
            }
            default:
                break;
            }
        }

        double t = plat_time();
        float dt = (float)(t - last);
        last = t;
        if (dt > 0.1f) dt = 0.1f;

        /* camera keys */
        float pan = 520.0f * dt / v.zoom;
        if (g.menu) pan = 0.0f;
        if (in.down[PK_A] || in.down[PK_LEFT])
            v.cam.x -= pan;
        if (in.down[PK_D] || in.down[PK_RIGHT])
            v.cam.x += pan;
        if (in.down[PK_W] || in.down[PK_UP])
            v.cam.y -= pan;
        if (in.down[PK_S] || in.down[PK_DOWN])
            v.cam.y += pan;
        /* A is also "select all"; camera pan only while held is fine, but avoid
         * panning on a quick tap: only pan if the key is still down next frames. */

        /* edge scrolling: push the camera when the cursor rests on a border */
        if (!g.menu && g.winner < 0 && !in.middle_down && in.edge_armed &&
            in.last_mx >= 0 && in.last_my >= 0) {
            const float edge = 9.0f;
            float esp = 600.0f * dt / v.zoom;
            if (in.last_mx < edge) v.cam.x -= esp;
            if (in.last_mx > fb.w - edge) v.cam.x += esp;
            if (in.last_my < edge) v.cam.y -= esp;
            if (in.last_my > fb.h - edge) v.cam.y += esp;
        }

        clamp_camera(&v, &g);
        game_update(&g, dt);
        for (int p = 0; p < g.players; p++)
            if (g.ai_enabled[p]) ai_update(&g, p, dt);

        /* Local faction wiped out while other AI factions still fight:
         * stop the simulation and offer restart / fog-free spectating. */
        if (!g.menu && g.winner < 0 && !g.spectator && !g.eliminated &&
            g.cities_owned[g.local] == 0 && g.units_owned[g.local] == 0) {
            g.eliminated = 1;
            g.paused = 1;
            g.sfx |= (uint16_t)SFX_UI;
            game_notice(&g, "ELIMINATED - SPACE TO SPECTATE");
        }

        play_sfx(&g, plat, t);

        v.box_active = in.dragging_box;
        if (in.dragging_box) {
            v.box_a = in.press_world;
            v.box_b = mouse_world(&v, in.last_mx, in.last_my);
        }
        v.path_active = in.path_active;
        v.plen = in.plen;
        memcpy(v.path, in.path, sizeof(Vec2) * (size_t)in.plen);
        v.stage_active = in.staging;
        v.stage_len = in.stage_len;
        v.stage_marks_n = in.stage_marks_n;
        memcpy(v.stage, in.stage, sizeof(Vec2) * (size_t)in.stage_len);
        memcpy(v.stage_marks, in.stage_marks, sizeof(int) * (size_t)in.stage_marks_n);

        fb.px = plat_pixels(plat);
        render_world(&fb, &g, &r, &v);
        render_hud(&fb, &g, &r, &v);
        plat_present(plat);

        double frame_end = plat_time();
        double spare = SIM_DT - (frame_end - t);
        if (spare > 0.0) plat_sleep(spare);
    }

    render_free(&r);
    plat_close(plat);
    return 0;
}

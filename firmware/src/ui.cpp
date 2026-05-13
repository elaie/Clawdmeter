#include "ui.h"
#include "splash.h"
#include "wifi_setup.h"
#include <lvgl.h>
#include "logo.h"
#include "icons.h"
#include "display_cfg.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_mono_32);

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Layout constants for 466×466 round AMOLED ----
#define SCR_W         LCD_WIDTH
#define SCR_H         LCD_HEIGHT
#define MARGIN        30
#define TITLE_Y       30
#define CONTENT_Y     100
#define CONTENT_W     (SCR_W - 2 * MARGIN)

// ---- Usage screen: shared container + per-layout sub-roots ----
static lv_obj_t* usage_container;

// Legacy rect layout removed; three arc layouts below.
// The lbl_anim / spinner lives on usage_container directly (shared).
static lv_obj_t* lbl_anim;

// ---- Layout A — concentric arcs ----
static lv_obj_t* la_root;
static lv_obj_t* la_arc_weekly;    // outer arc = weekly
static lv_obj_t* la_arc_session;   // inner arc = session
static lv_obj_t* la_lbl_session;   // big % center
static lv_obj_t* la_lbl_sreset;    // "Resets in Xh Ym"
static lv_obj_t* la_lbl_weekly;    // "WEEKLY 18% • 6d 4h"
static lv_obj_t* la_dot;           // status dot in arc gap

// ---- Layout B — stacked half-arcs ----
static lv_obj_t* lb_root;
static lv_obj_t* lb_arc_session;   // top semicircle = session
static lv_obj_t* lb_arc_weekly;    // bottom semicircle = weekly
static lv_obj_t* lb_lbl_session;   // big % in top half
static lv_obj_t* lb_lbl_weekly;    // big % in bottom half
static lv_obj_t* lb_lbl_sreset;
static lv_obj_t* lb_lbl_wreset;
static lv_obj_t* lb_divider;       // hairline at midpoint, colored by status

// ---- Layout C — dominant session + small weekly pill ----
static lv_obj_t* lc_root;
static lv_obj_t* lc_arc_session;
static lv_obj_t* lc_lbl_session;   // huge center %
static lv_obj_t* lc_lbl_sreset;    // "Resets in …"
static lv_obj_t* lc_pill_weekly;   // "W 18%" pill at bottom

// ---- Network screen widgets ----
static lv_obj_t* net_container;
static lv_obj_t* lbl_net_status;
static lv_obj_t* lbl_net_ssid;
static lv_obj_t* lbl_net_ip;
static lv_obj_t* lbl_net_rssi;
static lv_obj_t* btn_net_reconfig;

// ---- WiFi setup screen widgets ----
static lv_obj_t* ws_container;
static lv_obj_t* ws_view_scan;
static lv_obj_t* ws_view_pick;
static lv_obj_t* ws_view_pass;
static lv_obj_t* ws_view_connecting;
static lv_obj_t* ws_view_failed;
static lv_obj_t* ws_view_done;
static lv_obj_t* ws_lbl_scan_status;
static lv_obj_t* ws_list;
static lv_obj_t* ws_lbl_pass_title;
static lv_obj_t* ws_ta_pass;
static lv_obj_t* ws_keyboard;
static lv_obj_t* ws_lbl_connecting;
static lv_obj_t* ws_lbl_failed;
static wifi_setup_state_t ws_last_state = WIFI_SETUP_IDLE;
static uint32_t ws_done_at_ms = 0;

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static usage_layout_t current_usage_layout = USAGE_LAYOUT_B_HALVES;

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

// Per-frame hold time. Modeled on Claude Code's spinner (Cavalry triangle
// oscillator, range 0..5, period 5s) — turn-around frames (0 and 5) appear
// once per cycle, middle frames twice, so 0/5 read as held longer.
static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);
static lv_obj_t* make_pill(lv_obj_t* parent, const char* text);
static void net_reconfig_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    // Bubble click events up to the screen / usage_container so a tap anywhere
    // on the panel fires the global click handler.
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

// ---- Shared arc factory ----
// Creates an lv_arc centered at (cx, cy) with outer radius r.
// Knob hidden, rotation set so sweep starts at start_angle (degrees, LVGL
// convention: 0=right, 90=bottom). bg_start/bg_end define the full track arc.
// Returns the arc object; caller styles LV_PART_INDICATOR color.
static lv_obj_t* make_arc(lv_obj_t* parent,
                           int cx, int cy, int r, int thickness,
                           int bg_start, int bg_end) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, r * 2, r * 2);
    lv_obj_align(arc, LV_ALIGN_TOP_LEFT, cx - r, cy - r);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_arc_set_bg_angles(arc, bg_start, bg_end);
    lv_arc_set_angles(arc, bg_start, bg_start);  // empty until first update
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, thickness, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, thickness, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    return arc;
}

// Map a 0–100 value onto the arc's bg angle range.
static void arc_set_pct(lv_obj_t* arc, int pct, int bg_start, int bg_end) {
    // LVGL arc angles: value goes from bg_start toward bg_end.
    // lv_arc_set_value handles this automatically when range is 0–100.
    lv_arc_set_value(arc, pct);
}

// ======== Layout A — Concentric arcs ========
// Outer arc (r=210, thick=18) = weekly usage
// Inner arc (r=172, thick=18) = session usage
// Gap at top (270°→90° sweep = 270°) — status dot sits in the 90° gap at top
// Center text: session %, session reset, weekly summary
#define LA_CX      (SCR_W / 2)
#define LA_CY      (SCR_H / 2)
#define LA_R_OUTER 210
#define LA_R_INNER 172
#define LA_THICK   18
// Sweep: 135° start, 45° end (270° arc, gap at bottom)
#define LA_BG_START 135
#define LA_BG_END   45

static void init_layout_a(lv_obj_t* parent) {
    la_root = lv_obj_create(parent);
    lv_obj_set_size(la_root, SCR_W, SCR_H);
    lv_obj_set_pos(la_root, 0, 0);
    lv_obj_set_style_bg_opa(la_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(la_root, 0, 0);
    lv_obj_set_style_pad_all(la_root, 0, 0);
    lv_obj_clear_flag(la_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(la_root, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Outer = weekly
    la_arc_weekly = make_arc(la_root, LA_CX, LA_CY, LA_R_OUTER, LA_THICK, LA_BG_START, LA_BG_END);
    lv_obj_set_style_arc_color(la_arc_weekly, COL_AMBER, LV_PART_INDICATOR);

    // Inner = session
    la_arc_session = make_arc(la_root, LA_CX, LA_CY, LA_R_INNER, LA_THICK, LA_BG_START, LA_BG_END);
    lv_obj_set_style_arc_color(la_arc_session, COL_GREEN, LV_PART_INDICATOR);

    // Status dot in the arc gap (top-center, between the two arcs)
    la_dot = lv_obj_create(la_root);
    lv_obj_set_size(la_dot, 14, 14);
    lv_obj_set_style_radius(la_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(la_dot, 0, 0);
    lv_obj_set_style_bg_color(la_dot, COL_GREEN, 0);
    lv_obj_set_style_bg_opa(la_dot, LV_OPA_COVER, 0);
    lv_obj_align(la_dot, LV_ALIGN_TOP_MID, 0, (SCR_H / 2) - LA_R_OUTER + 14);
    lv_obj_clear_flag(la_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(la_dot, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Center: big session %
    la_lbl_session = lv_label_create(la_root);
    lv_label_set_text(la_lbl_session, "---%");
    lv_obj_set_style_text_font(la_lbl_session, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(la_lbl_session, COL_TEXT, 0);
    lv_obj_align(la_lbl_session, LV_ALIGN_CENTER, 0, -28);
    lv_obj_add_flag(la_lbl_session, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Center: session reset time
    la_lbl_sreset = lv_label_create(la_root);
    lv_label_set_text(la_lbl_sreset, "---");
    lv_obj_set_style_text_font(la_lbl_sreset, &font_styrene_20, 0);
    lv_obj_set_style_text_color(la_lbl_sreset, COL_DIM, 0);
    lv_obj_align(la_lbl_sreset, LV_ALIGN_CENTER, 0, 14);
    lv_obj_add_flag(la_lbl_sreset, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Center: weekly summary line
    la_lbl_weekly = lv_label_create(la_root);
    lv_label_set_text(la_lbl_weekly, "Weekly ---%");
    lv_obj_set_style_text_font(la_lbl_weekly, &font_styrene_20, 0);
    lv_obj_set_style_text_color(la_lbl_weekly, COL_DIM, 0);
    lv_obj_align(la_lbl_weekly, LV_ALIGN_CENTER, 0, 42);
    lv_obj_add_flag(la_lbl_weekly, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ======== Layout B — Stacked half-arcs ========
// Top semicircle (270°→90° = top half) = session
// Bottom semicircle (90°→270° = bottom half) = weekly
// Hairline divider at mid-height, colored by status
#define LB_CX      (SCR_W / 2)
#define LB_CY      (SCR_H / 2)
#define LB_R       208
#define LB_THICK   20
#define LB_S_START 180   // session arc: 180°→0° (top semicircle)
#define LB_S_END   0
#define LB_W_START 0     // weekly arc: 0°→180° (bottom semicircle)
#define LB_W_END   180

static void init_layout_b(lv_obj_t* parent) {
    lb_root = lv_obj_create(parent);
    lv_obj_set_size(lb_root, SCR_W, SCR_H);
    lv_obj_set_pos(lb_root, 0, 0);
    lv_obj_set_style_bg_opa(lb_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lb_root, 0, 0);
    lv_obj_set_style_pad_all(lb_root, 0, 0);
    lv_obj_clear_flag(lb_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lb_root, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Top arc = session
    lb_arc_session = make_arc(lb_root, LB_CX, LB_CY, LB_R, LB_THICK, LB_S_START, LB_S_END);
    lv_obj_set_style_arc_color(lb_arc_session, COL_GREEN, LV_PART_INDICATOR);

    // Bottom arc = weekly
    lb_arc_weekly = make_arc(lb_root, LB_CX, LB_CY, LB_R, LB_THICK, LB_W_START, LB_W_END);
    lv_obj_set_style_arc_color(lb_arc_weekly, COL_AMBER, LV_PART_INDICATOR);

    // Session % — upper half center
    lb_lbl_session = lv_label_create(lb_root);
    lv_label_set_text(lb_lbl_session, "---%");
    lv_obj_set_style_text_font(lb_lbl_session, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lb_lbl_session, COL_TEXT, 0);
    lv_obj_align(lb_lbl_session, LV_ALIGN_CENTER, 0, -62);
    lv_obj_add_flag(lb_lbl_session, LV_OBJ_FLAG_EVENT_BUBBLE);

    lb_lbl_sreset = lv_label_create(lb_root);
    lv_label_set_text(lb_lbl_sreset, "---");
    lv_obj_set_style_text_font(lb_lbl_sreset, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lb_lbl_sreset, COL_DIM, 0);
    lv_obj_align(lb_lbl_sreset, LV_ALIGN_CENTER, 0, -28);
    lv_obj_add_flag(lb_lbl_sreset, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Divider removed (was a green hairline appearing as a side bar)
    lb_divider = NULL;

    // Weekly % — lower half center
    lb_lbl_weekly = lv_label_create(lb_root);
    lv_label_set_text(lb_lbl_weekly, "---%");
    lv_obj_set_style_text_font(lb_lbl_weekly, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lb_lbl_weekly, COL_TEXT, 0);
    lv_obj_align(lb_lbl_weekly, LV_ALIGN_CENTER, 0, 28);
    lv_obj_add_flag(lb_lbl_weekly, LV_OBJ_FLAG_EVENT_BUBBLE);

    lb_lbl_wreset = lv_label_create(lb_root);
    lv_label_set_text(lb_lbl_wreset, "---");
    lv_obj_set_style_text_font(lb_lbl_wreset, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lb_lbl_wreset, COL_DIM, 0);
    lv_obj_align(lb_lbl_wreset, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_flag(lb_lbl_wreset, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ======== Layout C — Dominant session + small weekly pill ========
// Big session arc (270° sweep), huge % in center, reset below.
// Weekly pill at bottom of the donut hole.
#define LC_CX      (SCR_W / 2)
#define LC_CY      (SCR_H / 2)
#define LC_R       210
#define LC_THICK   24
#define LC_BG_START 135
#define LC_BG_END   45

static void init_layout_c(lv_obj_t* parent) {
    lc_root = lv_obj_create(parent);
    lv_obj_set_size(lc_root, SCR_W, SCR_H);
    lv_obj_set_pos(lc_root, 0, 0);
    lv_obj_set_style_bg_opa(lc_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lc_root, 0, 0);
    lv_obj_set_style_pad_all(lc_root, 0, 0);
    lv_obj_clear_flag(lc_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lc_root, LV_OBJ_FLAG_EVENT_BUBBLE);

    lc_arc_session = make_arc(lc_root, LC_CX, LC_CY, LC_R, LC_THICK, LC_BG_START, LC_BG_END);
    lv_obj_set_style_arc_color(lc_arc_session, COL_GREEN, LV_PART_INDICATOR);

    // Huge session %
    lc_lbl_session = lv_label_create(lc_root);
    lv_label_set_text(lc_lbl_session, "---%");
    lv_obj_set_style_text_font(lc_lbl_session, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lc_lbl_session, COL_TEXT, 0);
    lv_obj_align(lc_lbl_session, LV_ALIGN_CENTER, 0, -24);
    lv_obj_add_flag(lc_lbl_session, LV_OBJ_FLAG_EVENT_BUBBLE);

    lc_lbl_sreset = lv_label_create(lc_root);
    lv_label_set_text(lc_lbl_sreset, "---");
    lv_obj_set_style_text_font(lc_lbl_sreset, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lc_lbl_sreset, COL_DIM, 0);
    lv_obj_align(lc_lbl_sreset, LV_ALIGN_CENTER, 0, 16);
    lv_obj_add_flag(lc_lbl_sreset, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Weekly pill
    lc_pill_weekly = make_pill(lc_root, "W ---%");
    lv_obj_align(lc_pill_weekly, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_flag(lc_pill_weekly, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ======== Usage Screen — container + three layouts ========

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, SCR_W, SCR_H);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    init_layout_b(usage_container);
    init_layout_c(usage_container);

    // Show only the default layout; hide the rest
    lv_obj_clear_flag(lb_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lc_root, LV_OBJ_FLAG_HIDDEN);

    // Shared anim/spinner label at the bottom (sits above all layout sub-roots)
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -55);
}

// RGB565A8: planar — w*h RGB565 pixels followed by w*h alpha bytes.
// Stride is RGB565-only (w*2); LVGL infers alpha plane location from header.
static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

// ---- Battery icon initialization ----
static void init_battery_icons(void) {
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Network Screen ========

static void init_network_screen(lv_obj_t* scr) {
    net_container = lv_obj_create(scr);
    lv_obj_set_size(net_container, SCR_W, SCR_H);
    lv_obj_set_pos(net_container, 0, 0);
    lv_obj_set_style_bg_opa(net_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(net_container, 0, 0);
    lv_obj_set_style_pad_all(net_container, 0, 0);
    lv_obj_clear_flag(net_container, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_net_title = lv_label_create(net_container);
    lv_label_set_text(lbl_net_title, "Network");
    lv_obj_set_style_text_font(lbl_net_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_net_title, COL_TEXT, 0);
    lv_obj_align(lbl_net_title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    // Info panel
    lv_obj_t* p_info = make_panel(net_container, MARGIN, CONTENT_Y, CONTENT_W, 200);

    lbl_net_status = lv_label_create(p_info);
    lv_label_set_text(lbl_net_status, "Connecting...");
    lv_obj_set_style_text_font(lbl_net_status, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_net_status, COL_DIM, 0);
    lv_obj_set_pos(lbl_net_status, 0, 2);

    lbl_net_ssid = lv_label_create(p_info);
    lv_label_set_text(lbl_net_ssid, "SSID: ---");
    lv_obj_set_style_text_font(lbl_net_ssid, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_net_ssid, COL_DIM, 0);
    lv_obj_set_pos(lbl_net_ssid, 0, 64);

    lbl_net_ip = lv_label_create(p_info);
    lv_label_set_text(lbl_net_ip, "IP: ---");
    lv_obj_set_style_text_font(lbl_net_ip, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_net_ip, COL_DIM, 0);
    lv_obj_set_pos(lbl_net_ip, 0, 100);

    lbl_net_rssi = lv_label_create(p_info);
    lv_label_set_text(lbl_net_rssi, "RSSI: ---");
    lv_obj_set_style_text_font(lbl_net_rssi, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_net_rssi, COL_DIM, 0);
    lv_obj_set_pos(lbl_net_rssi, 0, 136);

    // Reconfigure WiFi button is gated off until touch is calibrated for the
    // 1.75 panel. The on-device setup flow (SCREEN_WIFI_SETUP) and its module
    // are still compiled in; restore this button when re-enabling.
    btn_net_reconfig = NULL;

    // Attribution
    lv_obj_t* lbl_credit = lv_label_create(net_container);
    lv_label_set_text(lbl_credit, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_font(lbl_credit, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -46);

    lv_obj_t* lbl_credit2 = lv_label_create(net_container);
    lv_label_set_text(lbl_credit2, "Clawd animation by @amaanbuilds");
    lv_obj_set_style_text_font(lbl_credit2, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_credit2, COL_DIM, 0);
    lv_obj_align(lbl_credit2, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Start hidden
    lv_obj_add_flag(net_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== WiFi Setup Screen ========
//
// Five sub-views overlaid in the same container; ui_wifi_setup_tick() shows
// the one matching wifi_setup_get_state(). The list and password text are
// rebuilt whenever we enter their state so we always render fresh data.

static void ws_show_only(lv_obj_t* visible) {
    lv_obj_t* views[] = { ws_view_scan, ws_view_pick, ws_view_pass,
                          ws_view_connecting, ws_view_failed, ws_view_done };
    for (lv_obj_t* v : views) {
        if (!v) continue;
        if (v == visible) lv_obj_clear_flag(v, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ws_list_item_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    Serial.printf("ui_wifi_setup: picked idx=%d\n", idx);
    wifi_setup_pick_network(idx);
}

static void ws_rescan_cb(lv_event_t* e) {
    (void)e;
    wifi_setup_rescan();
}

static void ws_back_cb(lv_event_t* e) {
    (void)e;
    wifi_setup_back_to_list();
}

static void ws_keyboard_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char* pass = lv_textarea_get_text(ws_ta_pass);
        Serial.printf("ui_wifi_setup: submit password (len=%d)\n", (int)strlen(pass));
        wifi_setup_submit_password(pass);
    } else if (code == LV_EVENT_CANCEL) {
        wifi_setup_back_to_list();
    }
}

static void ws_retry_cb(lv_event_t* e) {
    (void)e;
    wifi_setup_retry_connect();
}

static lv_obj_t* ws_make_text_button(lv_obj_t* parent, const char* text,
                                     lv_event_cb_t cb, lv_color_t fg) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 56);
    lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_obj_center(lbl);
    return btn;
}

static void init_wifi_setup_screen(lv_obj_t* scr) {
    ws_container = lv_obj_create(scr);
    lv_obj_set_size(ws_container, SCR_W, SCR_H);
    lv_obj_set_pos(ws_container, 0, 0);
    lv_obj_set_style_bg_color(ws_container, COL_BG, 0);
    lv_obj_set_style_bg_opa(ws_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ws_container, 0, 0);
    lv_obj_set_style_pad_all(ws_container, 0, 0);
    lv_obj_clear_flag(ws_container, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Scanning view ----
    ws_view_scan = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_scan, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_scan, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_scan, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_scan, 0, 0);
    lv_obj_set_style_pad_all(ws_view_scan, 0, 0);
    lv_obj_clear_flag(ws_view_scan, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(ws_view_scan);
        lv_label_set_text(title, "WiFi Setup");
        lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
        lv_obj_set_style_text_color(title, COL_TEXT, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

        lv_obj_t* spin = lv_spinner_create(ws_view_scan);
        lv_obj_set_size(spin, 80, 80);
        lv_obj_align(spin, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_arc_color(spin, COL_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_color(spin, COL_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);

        ws_lbl_scan_status = lv_label_create(ws_view_scan);
        lv_label_set_text(ws_lbl_scan_status, "Scanning networks...");
        lv_obj_set_style_text_font(ws_lbl_scan_status, &font_styrene_24, 0);
        lv_obj_set_style_text_color(ws_lbl_scan_status, COL_DIM, 0);
        lv_obj_align(ws_lbl_scan_status, LV_ALIGN_CENTER, 0, 80);
    }

    // ---- Pick view (built dynamically every time we enter this state) ----
    ws_view_pick = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_pick, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_pick, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_pick, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_pick, 0, 0);
    lv_obj_set_style_pad_all(ws_view_pick, 0, 0);
    lv_obj_clear_flag(ws_view_pick, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(ws_view_pick);
        lv_label_set_text(title, "Pick a Network");
        lv_obj_set_style_text_font(title, &font_styrene_28, 0);
        lv_obj_set_style_text_color(title, COL_TEXT, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

        ws_list = lv_list_create(ws_view_pick);
        lv_obj_set_size(ws_list, SCR_W - 2 * MARGIN, SCR_H - 160);
        lv_obj_align(ws_list, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_set_style_bg_color(ws_list, COL_PANEL, 0);
        lv_obj_set_style_bg_opa(ws_list, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ws_list, 12, 0);
        lv_obj_set_style_border_width(ws_list, 0, 0);
        lv_obj_set_style_pad_all(ws_list, 6, 0);

        lv_obj_t* rescan = ws_make_text_button(ws_view_pick, "Rescan",
                                               ws_rescan_cb, COL_ACCENT);
        lv_obj_align(rescan, LV_ALIGN_BOTTOM_MID, 0, -20);
    }

    // ---- Password entry view ----
    ws_view_pass = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_pass, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_pass, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_pass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_pass, 0, 0);
    lv_obj_set_style_pad_all(ws_view_pass, 0, 0);
    lv_obj_clear_flag(ws_view_pass, LV_OBJ_FLAG_SCROLLABLE);
    {
        ws_lbl_pass_title = lv_label_create(ws_view_pass);
        lv_label_set_text(ws_lbl_pass_title, "Password");
        lv_obj_set_style_text_font(ws_lbl_pass_title, &font_styrene_24, 0);
        lv_obj_set_style_text_color(ws_lbl_pass_title, COL_TEXT, 0);
        lv_label_set_long_mode(ws_lbl_pass_title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ws_lbl_pass_title, SCR_W - 2 * MARGIN);
        lv_obj_set_style_text_align(ws_lbl_pass_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ws_lbl_pass_title, LV_ALIGN_TOP_MID, 0, 16);

        ws_ta_pass = lv_textarea_create(ws_view_pass);
        lv_obj_set_size(ws_ta_pass, SCR_W - 2 * MARGIN, 56);
        lv_obj_align(ws_ta_pass, LV_ALIGN_TOP_MID, 0, 58);
        lv_textarea_set_one_line(ws_ta_pass, true);
        lv_textarea_set_password_mode(ws_ta_pass, true);
        lv_textarea_set_placeholder_text(ws_ta_pass, "password");
        lv_obj_set_style_text_font(ws_ta_pass, &font_styrene_24, 0);
        lv_obj_set_style_bg_color(ws_ta_pass, COL_PANEL, 0);
        lv_obj_set_style_bg_opa(ws_ta_pass, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ws_ta_pass, 0, 0);
        lv_obj_set_style_text_color(ws_ta_pass, COL_TEXT, 0);

        ws_keyboard = lv_keyboard_create(ws_view_pass);
        lv_keyboard_set_textarea(ws_keyboard, ws_ta_pass);
        lv_obj_set_size(ws_keyboard, SCR_W, SCR_H - 130);
        lv_obj_align(ws_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ws_keyboard, ws_keyboard_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(ws_keyboard, ws_keyboard_cb, LV_EVENT_CANCEL, NULL);
        lv_obj_set_style_text_font(ws_keyboard, &font_styrene_24, 0);
    }

    // ---- Connecting view ----
    ws_view_connecting = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_connecting, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_connecting, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_connecting, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_connecting, 0, 0);
    lv_obj_set_style_pad_all(ws_view_connecting, 0, 0);
    lv_obj_clear_flag(ws_view_connecting, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(ws_view_connecting);
        lv_label_set_text(title, "Connecting");
        lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
        lv_obj_set_style_text_color(title, COL_TEXT, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

        lv_obj_t* spin = lv_spinner_create(ws_view_connecting);
        lv_obj_set_size(spin, 80, 80);
        lv_obj_align(spin, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_arc_color(spin, COL_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_color(spin, COL_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);

        ws_lbl_connecting = lv_label_create(ws_view_connecting);
        lv_label_set_text(ws_lbl_connecting, "…");
        lv_obj_set_style_text_font(ws_lbl_connecting, &font_styrene_24, 0);
        lv_obj_set_style_text_color(ws_lbl_connecting, COL_DIM, 0);
        lv_label_set_long_mode(ws_lbl_connecting, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ws_lbl_connecting, SCR_W - 2 * MARGIN);
        lv_obj_set_style_text_align(ws_lbl_connecting, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ws_lbl_connecting, LV_ALIGN_CENTER, 0, 80);
    }

    // ---- Failed view ----
    ws_view_failed = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_failed, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_failed, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_failed, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_failed, 0, 0);
    lv_obj_set_style_pad_all(ws_view_failed, 0, 0);
    lv_obj_clear_flag(ws_view_failed, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(ws_view_failed);
        lv_label_set_text(title, "Couldn't connect");
        lv_obj_set_style_text_font(title, &font_styrene_28, 0);
        lv_obj_set_style_text_color(title, COL_RED, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

        ws_lbl_failed = lv_label_create(ws_view_failed);
        lv_label_set_text(ws_lbl_failed, "");
        lv_obj_set_style_text_font(ws_lbl_failed, &font_styrene_24, 0);
        lv_obj_set_style_text_color(ws_lbl_failed, COL_DIM, 0);
        lv_label_set_long_mode(ws_lbl_failed, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ws_lbl_failed, SCR_W - 2 * MARGIN);
        lv_obj_set_style_text_align(ws_lbl_failed, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ws_lbl_failed, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t* back = ws_make_text_button(ws_view_failed, "Back",
                                             ws_back_cb, COL_TEXT);
        lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, MARGIN, -30);
        lv_obj_t* retry = ws_make_text_button(ws_view_failed, "Retry",
                                              ws_retry_cb, COL_ACCENT);
        lv_obj_align(retry, LV_ALIGN_BOTTOM_RIGHT, -MARGIN, -30);
    }

    // ---- Done view ----
    ws_view_done = lv_obj_create(ws_container);
    lv_obj_set_size(ws_view_done, SCR_W, SCR_H);
    lv_obj_set_pos(ws_view_done, 0, 0);
    lv_obj_set_style_bg_opa(ws_view_done, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ws_view_done, 0, 0);
    lv_obj_set_style_pad_all(ws_view_done, 0, 0);
    lv_obj_clear_flag(ws_view_done, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(ws_view_done);
        lv_label_set_text(title, "Connected");
        lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
        lv_obj_set_style_text_color(title, COL_GREEN, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);
    }

    // Start hidden
    lv_obj_add_flag(ws_container, LV_OBJ_FLAG_HIDDEN);
    ws_show_only(ws_view_scan);
}

static void ws_rebuild_list(void) {
    lv_obj_clean(ws_list);
    int n = wifi_setup_scan_count();
    if (n == 0) {
        lv_obj_t* btn = lv_list_add_text(ws_list, "No networks found — Rescan");
        lv_obj_set_style_text_font(btn, &font_styrene_24, 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        char buf[64];
        int8_t rssi = wifi_setup_scan_rssi(i);
        const char* lock = wifi_setup_scan_open(i) ? "" : LV_SYMBOL_KEYBOARD " ";
        // Bars: 4 (>= -55), 3 (-65), 2 (-75), 1 (rest). Cheap signal indicator.
        int bars = (rssi >= -55) ? 4 : (rssi >= -65) ? 3 : (rssi >= -75) ? 2 : 1;
        const char* bar_str = (bars == 4) ? "||||" : (bars == 3) ? "||| " :
                              (bars == 2) ? "||  " : "|   ";
        snprintf(buf, sizeof(buf), "%s%s    %s", lock, wifi_setup_scan_ssid(i), bar_str);
        lv_obj_t* btn = lv_list_add_button(ws_list, NULL, buf);
        lv_obj_set_style_text_font(btn, &font_styrene_24, 0);
        lv_obj_set_style_text_color(btn, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, ws_list_item_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);
    }
}

void ui_wifi_setup_tick(void) {
    if (current_screen != SCREEN_WIFI_SETUP) return;
    wifi_setup_state_t s = wifi_setup_get_state();
    if (s == ws_last_state && s != WIFI_SETUP_DONE) {
        // Live-update the password-screen title even without state change,
        // because the SSID is only known after PICK and may have changed
        // since the screen was first composed.
        return;
    }
    ws_last_state = s;

    switch (s) {
    case WIFI_SETUP_SCAN_START:
    case WIFI_SETUP_SCANNING:
        ws_show_only(ws_view_scan);
        break;
    case WIFI_SETUP_PICK:
        ws_rebuild_list();
        ws_show_only(ws_view_pick);
        break;
    case WIFI_SETUP_ENTER_PASS: {
        char tbuf[48];
        snprintf(tbuf, sizeof(tbuf), "%s", wifi_setup_get_selected_ssid());
        lv_label_set_text(ws_lbl_pass_title, tbuf);
        lv_textarea_set_text(ws_ta_pass, "");
        ws_show_only(ws_view_pass);
        break;
    }
    case WIFI_SETUP_CONNECTING: {
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%s", wifi_setup_get_selected_ssid());
        lv_label_set_text(ws_lbl_connecting, tbuf);
        ws_show_only(ws_view_connecting);
        break;
    }
    case WIFI_SETUP_FAILED:
        lv_label_set_text(ws_lbl_failed, wifi_setup_get_error());
        ws_show_only(ws_view_failed);
        break;
    case WIFI_SETUP_DONE:
        ws_show_only(ws_view_done);
        if (ws_done_at_ms == 0) ws_done_at_ms = lv_tick_get();
        // After ~1.5s, exit to the usage screen — net.cpp is already managing
        // the real connection now and will populate it.
        if (lv_tick_get() - ws_done_at_ms > 1500) {
            ws_done_at_ms = 0;
            wifi_setup_cancel();
            ui_show_screen(SCREEN_USAGE);
        }
        break;
    default:
        break;
    }
}

void ui_enter_wifi_setup(void) {
    ws_last_state = WIFI_SETUP_IDLE;
    ws_done_at_ms = 0;
    wifi_setup_begin();
    ui_show_screen(SCREEN_WIFI_SETUP);
}

static void net_reconfig_cb(lv_event_t* e) {
    (void)e;
    Serial.println("ui: Reconfigure WiFi tapped — entering setup");
    ui_enter_wifi_setup();
}

// ======== Public API ========

void ui_init(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Logo (shared, always visible, on top of all containers)
    // Logo is RGB565A8 (planar: w*h RGB565 then w*h alpha) so it composites
    // cleanly against whatever bg is behind it.
    init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);

    // Initialize battery icon descriptors
    init_battery_icons();

    init_usage_screen(scr);
    init_network_screen(scr);
    init_wifi_setup_screen(scr);
    splash_init(scr);

    // Splash is touch-toggled — tap anywhere on the splash dismisses it
    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Logo on top of all containers (inset for rounded corners)
    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 55);  // center-top, inside donut hole

    // Battery indicator on top of all containers (upper-right, inset)
    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, SCR_W - 48 - MARGIN, TITLE_Y);
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;

    int s_pct = (int)(data->session_pct + 0.5f);
    int w_pct = (int)(data->weekly_pct + 0.5f);
    lv_color_t s_col = pct_color(data->session_pct);
    lv_color_t w_col = pct_color(data->weekly_pct);
    bool limited = (strncmp(data->status, "limited", 7) == 0);

    char sreset[48], wreset[48], weekly_line[48];
    format_reset_time(data->session_reset_mins, sreset, sizeof(sreset));
    format_reset_time(data->weekly_reset_mins, wreset, sizeof(wreset));
    snprintf(weekly_line, sizeof(weekly_line), "Weekly %d%%", w_pct);

    // ---- Layout B ----
    lv_arc_set_value(lb_arc_session, s_pct);
    lv_obj_set_style_arc_color(lb_arc_session, s_col, LV_PART_INDICATOR);
    lv_arc_set_value(lb_arc_weekly, w_pct);
    lv_obj_set_style_arc_color(lb_arc_weekly, w_col, LV_PART_INDICATOR);
    lv_label_set_text_fmt(lb_lbl_session, "%d%%", s_pct);
    lv_label_set_text(lb_lbl_sreset, sreset);
    lv_label_set_text_fmt(lb_lbl_weekly, "%d%%", w_pct);
    lv_label_set_text(lb_lbl_wreset, wreset);

    // ---- Layout C ----
    lv_arc_set_value(lc_arc_session, s_pct);
    lv_obj_set_style_arc_color(lc_arc_session, s_col, LV_PART_INDICATOR);
    lv_label_set_text_fmt(lc_lbl_session, "%d%%", s_pct);
    lv_label_set_text(lc_lbl_sreset, sreset);
    {
        char wpill[16];
        snprintf(wpill, sizeof(wpill), "W %d%%", w_pct);
        lv_label_set_text(lc_pill_weekly, wpill);
        lv_obj_set_style_bg_color(lc_pill_weekly, w_col, 0);
        lv_obj_set_style_text_color(lc_pill_weekly, COL_BG, 0);
    }
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms >= spinner_ms[anim_spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);

        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx],
                 anim_messages[anim_msg_idx]);
        lv_label_set_text(lbl_anim, buf);
    }
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
// Hide the battery indicator on the splash screen — the icon is visually
// noisy over the pixel-art creature animations.
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    // Battery icon hidden globally (was rendering as a small white dot top-right)
    lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

// LVGL handles click debouncing internally. Screen-level handler fires when
// no child consumed the event. The splash is the only touch-toggled screen;
// taps on Usage or Network screens go to splash.
static void global_click_cb(lv_event_t* e) {
    (void)e;
    ui_toggle_splash();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(net_container, LV_OBJ_FLAG_HIDDEN);
    if (ws_container) lv_obj_add_flag(ws_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:      splash_show(); break;
    case SCREEN_USAGE:       lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_NETWORK:     lv_obj_clear_flag(net_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_WIFI_SETUP:  lv_obj_clear_flag(ws_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    // Hide the logo on splash and network; show only on the usage screen
    if (logo_img) {
        if (screen == SCREEN_USAGE) lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                         lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH && screen != SCREEN_WIFI_SETUP) {
        prev_non_splash_screen = screen;
    }
    current_screen = screen;
    apply_battery_visibility();
}

void ui_cycle_screen(void) {
    // WiFi setup is modal — the user can only leave via the on-screen flow.
    // Button-cycling lands back on Usage so a stray press doesn't strand them.
    if (current_screen == SCREEN_WIFI_SETUP) {
        ui_show_screen(SCREEN_USAGE);
        return;
    }
    screen_t next = (current_screen == SCREEN_USAGE) ? SCREEN_NETWORK : SCREEN_USAGE;
    ui_show_screen(next);
}

void ui_cycle_usage_layout(void) {
    // Hide current layout root
    lv_obj_t* roots[USAGE_LAYOUT_COUNT] = { lb_root, lc_root };
    lv_obj_add_flag(roots[current_usage_layout], LV_OBJ_FLAG_HIDDEN);

    usage_layout_t prev = current_usage_layout;
    current_usage_layout = (usage_layout_t)((current_usage_layout + 1) % USAGE_LAYOUT_COUNT);

    lv_obj_clear_flag(roots[current_usage_layout], LV_OBJ_FLAG_HIDDEN);

    static const char* const names[] = { "B-halves", "C-dominant" };
    Serial.printf("usage layout: %s -> %s\n", names[prev], names[current_usage_layout]);
}

usage_layout_t ui_get_usage_layout(void) {
    return current_usage_layout;
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_net_status(net_state_t state, const char* ssid, const char* ip, int8_t rssi) {
    switch (state) {
    case NET_STATE_CONNECTED:
        lv_label_set_text(lbl_net_status, "Connected");
        lv_obj_set_style_text_color(lbl_net_status, COL_GREEN, 0);
        break;
    case NET_STATE_CONNECTING:
        lv_label_set_text(lbl_net_status, "Connecting...");
        lv_obj_set_style_text_color(lbl_net_status, COL_AMBER, 0);
        break;
    default:
        lv_label_set_text(lbl_net_status, "Disconnected");
        lv_obj_set_style_text_color(lbl_net_status, COL_RED, 0);
        break;
    }

    if (ssid) {
        static char sbuf[48];
        snprintf(sbuf, sizeof(sbuf), "SSID: %s", ssid);
        lv_label_set_text(lbl_net_ssid, sbuf);
    }
    if (ip) {
        static char ibuf[32];
        snprintf(ibuf, sizeof(ibuf), "IP: %s", ip);
        lv_label_set_text(lbl_net_ip, ibuf);
    }
    static char rbuf[16];
    if (state == NET_STATE_CONNECTED) {
        snprintf(rbuf, sizeof(rbuf), "RSSI: %d dBm", (int)rssi);
    } else {
        snprintf(rbuf, sizeof(rbuf), "RSSI: ---");
    }
    lv_label_set_text(lbl_net_rssi, rbuf);
}

void ui_update_battery(int percent, bool charging) {
    int idx;
    if (charging) {
        idx = 4;  // charging icon
    } else if (percent < 0) {
        idx = 0;  // no battery / unknown
    } else if (percent <= 10) {
        idx = 0;  // empty
    } else if (percent <= 35) {
        idx = 1;  // low
    } else if (percent <= 75) {
        idx = 2;  // medium
    } else {
        idx = 3;  // full
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}

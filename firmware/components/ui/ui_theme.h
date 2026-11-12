#pragma once
/* Layout for the 1280x720 landscape screen. */
#define UI_W 1280
#define UI_H 720
#define UI_BAR_H 7
#define UI_BTN 96

/* Lyrics layout: cover and controls in a left column, lyrics on the right. */
#define UI_L_MARGIN_X 44
#define UI_L_MARGIN_Y 43
#define UI_L_COVER 400
#define UI_L_LYRICS_X 500
#define UI_L_LYRICS_W (UI_W - UI_L_LYRICS_X - 44)
/* The lyrics scroll inside a band shorter than the screen: every scrolled frame redraws the
 * whole band, and this tablet is slow at that (see ui.c). */
#define UI_L_LYRICS_Y 70
#define UI_L_LYRICS_H 580

/* No-lyrics layout: large cover on the left, title and controls on the right. */
#define UI_N_COVER 560
#define UI_N_COVER_X 70
#define UI_N_PANEL_X (UI_N_COVER_X + UI_N_COVER + 70)
#define UI_N_PANEL_W (UI_W - UI_N_PANEL_X - 70)

#define UI_COVER_RADIUS 13
#define UI_OPA_VEIL 26          /* 10% black over the background */
#define UI_OPA_ARTIST 158       /* 62% */
#define UI_OPA_TIME 140         /* 55% */
#define UI_OPA_BAR_BG 56        /* 22% */
#define UI_OPA_BAR_FG 217       /* 85% */
#define UI_OPA_LYRIC_DIM 82     /* 32% */
#define UI_OPA_LYRIC_PLAIN 217  /* 85%: lyrics without timing are all equally readable */
#define UI_LYRIC_GAP 22
#define UI_MAX_LYRIC_LINES 300

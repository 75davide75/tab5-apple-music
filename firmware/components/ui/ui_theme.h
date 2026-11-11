#pragma once
/* Layout for the 1280x720 landscape screen (values from the approved mockup). */
#define UI_W 1280
#define UI_H 720
#define UI_MARGIN_X 44
#define UI_MARGIN_Y 43
#define UI_LEFT_W 400
#define UI_GAP 46
#define UI_RIGHT_X (UI_MARGIN_X + UI_LEFT_W + UI_GAP)
#define UI_RIGHT_W (UI_W - UI_MARGIN_X - UI_RIGHT_X)
#define UI_COVER_RADIUS 13
#define UI_BAR_H 7
#define UI_ROW_H 68
#define UI_BTN 96

#define UI_OPA_VEIL 71          /* 28% black over the background */
#define UI_OPA_ARTIST 158       /* 62% */
#define UI_OPA_TIME 140         /* 55% */
#define UI_OPA_BAR_BG 56        /* 22% */
#define UI_OPA_BAR_FG 217       /* 85% */
#define UI_OPA_SUB 153          /* 60% */
#define UI_OPA_ROW_NOW 36       /* 14% */
#define UI_OPA_ROW_PLAYED 102   /* 40% */
#define UI_OPA_PILL 51          /* 20% */

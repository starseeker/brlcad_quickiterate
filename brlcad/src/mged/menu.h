/*                           M E N U . H
 * BRL-CAD
 *
 * Copyright (c) 1985-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file mged/menu.h
 */

#ifndef MGED_MENU_H
#define MGED_MENU_H

#include "common.h"
#include "rt/edit.h"

__BEGIN_DECLS

/* Menu structures and defines
 *
 * Each active menu is installed by having a non-null entry in
 * menu_array[] which is a pointer to an array of menu items.  The
 * first ([0]) menu item is the title for the menu, and the remaining
 * items are individual menu entries.
 */

#define NMENU 3
#define MENU_L1 0 /* top-level solid-edit menu */
#define MENU_L2 1 /* second-level menu */
#define MENU_GEN 2 /* general features (mouse buttons) */

#define MENUXLIM        (-1250)         /* Value to set X lim to for menu */
#define MENUX           (-2048+115)     /* pixel position for menu, X */
#define MENUY           1780            /* pixel position for menu, Y */
#define SCROLLY         (2047)          /* starting Y pos for scroll area */
#define MENU_DY         (-104)          /* Distance between menu items */
#define SCROLL_DY       (-100)          /* Distance between scrollers */

#define TITLE_XBASE     (-2048)         /* pixel X of title line start pos */
#define TITLE_YBASE     (-1920)         /* pixel pos of last title line */
#define SOLID_XBASE     MENUXLIM        /* X to start display text */
#define SOLID_YBASE     (1920)          /* pixel pos of first solid line */
#define TEXT0_DY        (-60)           /* #pixels per line, Size 0 */
#define TEXT1_DY        (-90)           /* #pixels per line, Size 1 */

/* Legacy menu argument values - used in es_menu to identify
 * which pscale operation to perform.  These correspond to the
 * menu_arg fields of the primitive-specific menu items and will
 * be eliminated when pscale() is refactored into librt. */
#define MENU_TOR_R1		21
#define MENU_TOR_R2		22
#define MENU_TGC_ROT_H		23
#define MENU_TGC_ROT_AB 	24
#define MENU_TGC_MV_H		25
#define MENU_TGC_MV_HH		26
#define MENU_TGC_SCALE_H	27
#define MENU_TGC_SCALE_H_V	28
#define MENU_TGC_SCALE_A	29
#define MENU_TGC_SCALE_B	30
#define MENU_TGC_SCALE_C	31
#define MENU_TGC_SCALE_D	32
#define MENU_TGC_SCALE_AB	33
#define MENU_TGC_SCALE_CD	34
#define MENU_TGC_SCALE_ABCD	35
#define MENU_ARB_MV_EDGE	36
#define MENU_ARB_MV_FACE	37
#define MENU_ARB_ROT_FACE	38
#define MENU_ELL_SCALE_A	39
#define MENU_ELL_SCALE_B	40
#define MENU_ELL_SCALE_C	41
#define MENU_ELL_SCALE_ABC	42
#define MENU_RPC_B		43
#define MENU_RPC_H		44
#define MENU_RPC_R		45
#define MENU_RHC_B		46
#define MENU_RHC_H		47
#define MENU_RHC_R		48
#define MENU_RHC_C		49
#define MENU_EPA_H		50
#define MENU_EPA_R1		51
#define MENU_EPA_R2		52
#define MENU_EHY_H		53
#define MENU_EHY_R1		54
#define MENU_EHY_R2		55
#define MENU_EHY_C		56
#define MENU_ETO_R		57
#define MENU_ETO_RD		58
#define MENU_ETO_SCALE_C	59
#define MENU_ETO_ROT_C		60
#define MENU_PIPE_SELECT	61
#define MENU_PIPE_NEXT_PT	62
#define MENU_PIPE_PREV_PT	63
#define MENU_PIPE_SPLIT		64
#define MENU_PIPE_PT_OD		65
#define MENU_PIPE_PT_ID		66
#define MENU_PIPE_SCALE_OD	67
#define MENU_PIPE_SCALE_ID	68
#define MENU_PIPE_ADD_PT	69
#define MENU_PIPE_INS_PT	70
#define MENU_PIPE_DEL_PT	71
#define MENU_PIPE_MOV_PT	72
#define MENU_PIPE_PT_RADIUS	73
#define MENU_PIPE_SCALE_RADIUS	74
#define MENU_VOL_FNAME		75
#define MENU_VOL_FSIZE		76
#define MENU_VOL_CSIZE		77
#define MENU_VOL_THRESH_LO	78
#define MENU_VOL_THRESH_HI	79
#define MENU_EBM_FNAME		80
#define MENU_EBM_FSIZE		81
#define MENU_EBM_HEIGHT		82
#define MENU_DSP_FNAME		83
#define MENU_DSP_FSIZE		84	/* Not implemented yet */
#define MENU_DSP_SCALE_X	85
#define MENU_DSP_SCALE_Y	86
#define MENU_DSP_SCALE_ALT	87
#define MENU_PART_H		88
#define MENU_PART_v		89
#define MENU_PART_h		90
#define MENU_BOT_PICKV		91
#define MENU_BOT_PICKE		92
#define MENU_BOT_PICKT		93
#define MENU_BOT_MOVEV		94
#define MENU_BOT_MOVEE		95
#define MENU_BOT_MOVET		96
#define MENU_BOT_MODE		97
#define MENU_BOT_ORIENT		98
#define MENU_BOT_THICK		99
#define MENU_BOT_FMODE		100
#define MENU_BOT_DELETE_TRI	101
#define MENU_BOT_FLAGS		102
#define MENU_EXTR_SCALE_H	103
#define MENU_EXTR_MOV_H		104
#define MENU_EXTR_ROT_H		105
#define MENU_EXTR_SKT_NAME	106
#define MENU_CLINE_SCALE_H	107
#define MENU_CLINE_MOVE_H	108
#define MENU_CLINE_SCALE_R	109
#define MENU_CLINE_SCALE_T	110
#define MENU_TGC_SCALE_H_CD	111
#define MENU_TGC_SCALE_H_V_AB	112
#define MENU_SUPERELL_SCALE_A	113
#define MENU_SUPERELL_SCALE_B	114
#define MENU_SUPERELL_SCALE_C	115
#define MENU_SUPERELL_SCALE_ABC	116
#define MENU_METABALL_SET_THRESHOLD	117
#define MENU_METABALL_SET_METHOD	118
#define MENU_METABALL_PT_SET_GOO	119
#define MENU_METABALL_SELECT	120
#define MENU_METABALL_NEXT_PT	121
#define MENU_METABALL_PREV_PT	122
#define MENU_METABALL_MOV_PT	123
#define MENU_METABALL_PT_FLDSTR	124
#define MENU_METABALL_DEL_PT	125
#define MENU_METABALL_ADD_PT	126
#define MENU_HYP_H              127
#define MENU_HYP_SCALE_A        128
#define MENU_HYP_SCALE_B	129
#define MENU_HYP_C		130
#define MENU_HYP_ROT_H		131

/* Forward declare mged_state to avoid circular includes */
struct mged_state;

extern struct rt_edit_menu_item sed_menu[];
extern struct rt_edit_menu_item oed_menu[];

void btn_head_menu(struct rt_edit *s, int i, int menu, int item, void *data);
void chg_l2menu(struct mged_state *s, int i);

extern void mmenu_init(struct mged_state *s);
extern void mmenu_display(struct mged_state *s, int y_top);
extern void mmenu_set(struct mged_state *s, int idx, struct rt_edit_menu_item *value);
extern void mmenu_set_all(struct mged_state *s, int idx, struct rt_edit_menu_item *value);
extern void sedit_menu(struct mged_state *s);
extern int mmenu_select(struct mged_state *s, int pen_y, int do_func);

__END_DECLS

#endif  /* MGED_MENU_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */

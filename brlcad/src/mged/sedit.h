/*                         S E D I T . H
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
/** @file mged/sedit.h
 *
 * This header file contains the esolid structure definition,
 * which holds all the information necessary for solid editing.
 * Storage is actually allocated in edsol.c
 *
 */

#ifndef MGED_SEDIT_H
#define MGED_SEDIT_H

#include "rt/edit.h"
#include "mged.h"

#define MGED_SMALL_SCALE 1.0e-10

/* These EDIT_CLASS_ values go in es_edclass. */
#define EDIT_CLASS_NULL 0
#define EDIT_CLASS_TRAN 1
#define EDIT_CLASS_ROTATE 2
#define EDIT_CLASS_SCALE 3

/* These ECMD_ values go in MEDIT(s)->edit_flag.  Some names not changed yet */
/* These MGED values match librt's RT_EDIT_IDLE/RT_PARAMS_EDIT_* exactly.
 * They are kept as aliases so that sedit(), buttons.c, etc. can use
 * the familiar MGED names while being wire-compatible with librt. */
#define IDLE		RT_EDIT_IDLE		/* = 0, matches librt RT_EDIT_IDLE */
#define STRANS		RT_PARAMS_EDIT_TRANS	/* = 1, matches librt value */
#define SSCALE		RT_PARAMS_EDIT_SCALE	/* = 2, matches librt value */
#define SROT		RT_PARAMS_EDIT_ROT	/* = 3, matches librt value */

#define ECMD_TGC_MV_H		2005	/* move H to new position (edtgc.c) */
#define ECMD_TGC_MV_HH		2006	/* move H and HH to new position (edtgc.c) */
#define ECMD_TGC_ROT_H		2007	/* rotate H vector (edtgc.c) */
#define ECMD_TGC_ROT_AB		2008	/* rotate A,B vectors (edtgc.c) */

#define EARB		4009	/* chgmodel.c, edarb.c (edarb.c: #define EARB 4009) */
#define PTARB		4010	/* edarb.c (edarb.c: #define PTARB 4010) */
#define ECMD_ARB_MAIN_MENU	4011	/* (edarb.c) */
#define ECMD_ARB_SPECIFIC_MENU	4012	/* (edarb.c) */
#define ECMD_ARB_MOVE_FACE	4013	/* (edarb.c) */
#define ECMD_ARB_SETUP_ROTFACE	4014	/* (edarb.c) */
#define ECMD_ARB_ROTATE_FACE	4015	/* (edarb.c) */

#define ECMD_ETO_ROT_C		21016	/* rotate C vector (edeto.c) */

#define ECMD_VTRANS		9017	/* vertex translate (edbspline.c) */
#define ECMD_NMG_EPICK		11019	/* edge pick (ednmg.c) */
#define ECMD_NMG_EMOVE		11020	/* edge move (ednmg.c) */
#define ECMD_NMG_EDEBUG		11021	/* edge debug (ednmg.c) */
#define ECMD_NMG_FORW		11022	/* next eu (ednmg.c) */
#define ECMD_NMG_BACK		11023	/* prev eu (ednmg.c) */
#define ECMD_NMG_RADIAL		11024	/* radial+mate eu (ednmg.c) */
#define ECMD_NMG_ESPLIT		11025	/* split current edge (ednmg.c) */
#define ECMD_NMG_EKILL		11026	/* kill current edge (ednmg.c) */
#define ECMD_NMG_LEXTRU		11027	/* Extrude loop (ednmg.c) */

#define ECMD_PIPE_PICK		15028	/* Pick pipe point (edpipe.c ECMD_PIPE_SELECT) */
#define ECMD_PIPE_SPLIT		15029	/* Split a pipe segment into two (edpipe.c) */
#define ECMD_PIPE_PT_ADD	15030	/* Add a pipe point to end of pipe (edpipe.c) */
#define ECMD_PIPE_PT_INS	15031	/* Add a pipe point to start of pipe (edpipe.c) */
#define ECMD_PIPE_PT_DEL	15032	/* Delete a pipe point (edpipe.c) */
#define ECMD_PIPE_PT_MOVE	15033	/* Move a pipe point (edpipe.c) */
#define ECMD_PIPE_PT_OD		15065	/* scale OD of one pipe segment (edpipe.c) */
#define ECMD_PIPE_PT_ID		15066	/* scale ID of one pipe segment (edpipe.c) */
#define ECMD_PIPE_SCALE_OD	15067	/* scale entire pipe OD (edpipe.c) */
#define ECMD_PIPE_SCALE_ID	15068	/* scale entire pipe ID (edpipe.c) */
#define ECMD_PIPE_PT_RADIUS	15073	/* scale bend radius at selected point (edpipe.c) */
#define ECMD_PIPE_SCALE_RADIUS	15074	/* scale entire pipe bend radius (edpipe.c) */

#define ECMD_ARS_PICK		5034	/* select an ARS point (edars.c) */
#define ECMD_ARS_NEXT_PT	5035	/* select next ARS point in same curve (edars.c) */
#define ECMD_ARS_PREV_PT	5036	/* select previous ARS point in same curve (edars.c) */
#define ECMD_ARS_NEXT_CRV	5037	/* select corresponding ARS point in next curve (edars.c) */
#define ECMD_ARS_PREV_CRV	5038	/* select corresponding ARS point in previous curve (edars.c) */
#define ECMD_ARS_MOVE_PT	5039	/* translate an ARS point (edars.c) */
#define ECMD_ARS_DEL_CRV	5040	/* delete an ARS curve (edars.c) */
#define ECMD_ARS_DEL_COL	5041	/* delete all corresponding points in each curve (edars.c) */
#define ECMD_ARS_DUP_CRV	5042	/* duplicate an ARS curve (edars.c) */
#define ECMD_ARS_DUP_COL	5043	/* duplicate an ARS column (edars.c) */
#define ECMD_ARS_MOVE_CRV	5044	/* translate an ARS curve (edars.c) */
#define ECMD_ARS_MOVE_COL	5045	/* translate an ARS column (edars.c) */
#define ECMD_ARS_PICK_MENU	5046	/* display the ARS pick menu (edars.c) */
#define ECMD_ARS_EDIT_MENU	5047	/* display the ARS edit menu (edars.c) */

#define ECMD_VOL_CSIZE		13048	/* set voxel size (edvol.c) */
#define ECMD_VOL_FSIZE		13049	/* set VOL file dimensions (edvol.c) */
#define ECMD_VOL_THRESH_LO	13050	/* set VOL threshold (lo) (edvol.c) */
#define ECMD_VOL_THRESH_HI	13051	/* set VOL threshold (hi) (edvol.c) */
#define ECMD_VOL_FNAME		13052	/* set VOL file name (edvol.c) */

#define ECMD_EBM_FNAME		12053	/* set EBM file name (edebm.c) */
#define ECMD_EBM_FSIZE		12054	/* set EBM file size (edebm.c) */
#define ECMD_EBM_HEIGHT		12055	/* set EBM extrusion depth (edebm.c) */

#define ECMD_DSP_FNAME		25056	/* set DSP file name (eddsp.c) */
#define ECMD_DSP_FSIZE		25057	/* set DSP file size (eddsp.c) */
#define ECMD_DSP_SCALE_X	25058	/* Scale DSP x size (eddsp.c) */
#define ECMD_DSP_SCALE_Y	25059	/* Scale DSP y size (eddsp.c) */
#define ECMD_DSP_SCALE_ALT	25060	/* Scale DSP Altitude size (eddsp.c) */

#define ECMD_BOT_PICKV		30061	/* pick a BOT vertex (edbot.c) */
#define ECMD_BOT_PICKE		30062	/* pick a BOT edge (edbot.c) */
#define ECMD_BOT_PICKT		30063	/* pick a BOT triangle (edbot.c) */
#define ECMD_BOT_MOVEV		30064	/* move a BOT vertex (edbot.c) */
#define ECMD_BOT_MOVEE		30065	/* move a BOT edge (edbot.c) */
#define ECMD_BOT_MOVET		30066	/* move a BOT triangle (edbot.c) */
#define ECMD_BOT_MODE		30067	/* set BOT mode (edbot.c) */
#define ECMD_BOT_ORIENT		30068	/* set BOT face orientation (edbot.c) */
#define ECMD_BOT_THICK		30069	/* set face thickness (one or all) (edbot.c) */
#define ECMD_BOT_FMODE		30070	/* set face mode (one or all) (edbot.c) */
#define ECMD_BOT_FDEL		30071	/* delete current face (edbot.c) */
#define ECMD_BOT_FLAGS		30072	/* set BOT flags (edbot.c) */

#define ECMD_EXTR_SCALE_H	27073	/* scale extrusion vector (edextrude.c) */
#define ECMD_EXTR_MOV_H		27074	/* move end of extrusion vector (edextrude.c) */
#define ECMD_EXTR_ROT_H		27075	/* rotate extrusion vector (edextrude.c) */
#define ECMD_EXTR_SKT_NAME	27076	/* set sketch that the extrusion uses (edextrude.c) */

#define ECMD_CLINE_SCALE_H	29077	/* scale height vector (edcline.c) */
#define ECMD_CLINE_MOVE_H	29078	/* move end of height vector (edcline.c) */
#define ECMD_CLINE_SCALE_R	29079	/* scale radius (edcline.c) */
#define ECMD_CLINE_SCALE_T	29080	/* scale thickness (edcline.c) */
#define ECMD_TGC_MV_H_CD	2081	/* move H adjusting C,D (edtgc.c) */
#define ECMD_TGC_MV_H_V_AB	2082	/* move H+move V adjusting A,B (edtgc.c) */

#define ECMD_METABALL_SET_THRESHOLD	36083	/* overall metaball threshold value (edmetaball.c) */
#define ECMD_METABALL_SET_METHOD	36084	/* set the rendering method (edmetaball.c) */
#define ECMD_METABALL_PT_PICK		36085	/* pick a metaball control point (edmetaball.c) */
#define ECMD_METABALL_PT_MOV		36086	/* move a metaball control point (edmetaball.c) */
#define ECMD_METABALL_PT_FLDSTR		36087	/* set a metaball control point field strength (edmetaball.c) */
#define ECMD_METABALL_PT_DEL		36088	/* delete a metaball control point (edmetaball.c) */
#define ECMD_METABALL_PT_ADD		36089	/* add a metaball control point (edmetaball.c) */
#define ECMD_METABALL_RMET		36090	/* set the metaball render method (edmetaball.c) */
#define ECMD_METABALL_PT_SET_GOO	30119	/* set goo for a metaball control point (edmetaball.c)
					 * NOTE: uses ID_BOT(30)*1000 base - this is an upstream
					 * quirk in the librt metaball edit code, not a mistake here */

#define ECMD_HYP_ROT_H		38091	/* (edhyp.c) */
#define ECMD_HYP_ROT_A		38092	/* (edhyp.c) */

/* librt primitive ECMD values - these match the values defined in the librt primitive
 * edit source files (e.g. edtor.c, edell.c).  They are also collected at build time
 * into the generated rt/rt_ecmds.h header.  These are duplicated here so that MGED
 * can use them without a direct dependency on the build-generated header; keep in
 * sync with the corresponding #define in each primitive's ed*.c file. */
#define ECMD_TOR_R1	1021	/* set/scale TOR radius 1 (edtor.c) */
#define ECMD_TOR_R2	1022	/* set/scale TOR radius 2 (edtor.c) */
#define ECMD_ELL_SCALE_A	3039	/* scale ELL semiaxis A (edell.c) */
#define ECMD_ELL_SCALE_B	3040	/* scale ELL semiaxis B (edell.c) */
#define ECMD_ELL_SCALE_C	3041	/* scale ELL semiaxis C (edell.c) */
#define ECMD_ELL_SCALE_ABC	3042	/* scale ELL A,B,C uniformly (edell.c) */
#define ECMD_PART_H		16088	/* scale PART height (edpart.c) */
#define ECMD_PART_VRAD		16089	/* scale PART vertex radius (edpart.c) */
#define ECMD_PART_HRAD		16090	/* scale PART height-end radius (edpart.c) */
#define ECMD_RPC_B		17043	/* scale RPC breadth B (edrpc.c) */
#define ECMD_RPC_H		17044	/* scale RPC height H (edrpc.c) */
#define ECMD_RPC_R		17045	/* scale RPC half-width r (edrpc.c) */
#define ECMD_RHC_B		18046	/* scale RHC breadth B (edrhc.c) */
#define ECMD_RHC_H		18047	/* scale RHC height H (edrhc.c) */
#define ECMD_RHC_R		18048	/* scale RHC half-width r (edrhc.c) */
#define ECMD_RHC_C		18049	/* scale RHC dist-to-asymptotes c (edrhc.c) */
#define ECMD_EPA_H		19050	/* scale EPA height H (edepa.c) */
#define ECMD_EPA_R1		19051	/* scale EPA semi-major axis r1 (edepa.c) */
#define ECMD_EPA_R2		19052	/* scale EPA semi-minor axis r2 (edepa.c) */
#define ECMD_EHY_H		20053	/* scale EHY height H (edehy.c) */
#define ECMD_EHY_R1		20054	/* scale EHY semi-major axis r1 (edehy.c) */
#define ECMD_EHY_R2		20055	/* scale EHY semi-minor axis r2 (edehy.c) */
#define ECMD_EHY_C		20056	/* scale EHY dist-to-asymptotes c (edehy.c) */
#define ECMD_ETO_R		21057	/* scale ETO major radius r (edeto.c) */
#define ECMD_ETO_RD		21058	/* scale ETO minor radius rd (edeto.c) */
#define ECMD_ETO_SCALE_C	21059	/* scale ETO semi-minor axis C (edeto.c) */
#define ECMD_HYP_H		38127	/* scale HYP height H (edhyp.c) */
#define ECMD_HYP_SCALE_A	38128	/* scale HYP semi-major axis A (edhyp.c) */
#define ECMD_HYP_SCALE_B	38129	/* scale HYP semi-minor axis B (edhyp.c) */
#define ECMD_HYP_C		38130	/* scale HYP neck parameter c (edhyp.c) */
/* TGC scale operations (edtgc.c) - MV_H/HH/ROT_H/ROT_AB keep legacy MGED values */
#define ECMD_TGC_SCALE_H	2027
#define ECMD_TGC_SCALE_H_V	2028
#define ECMD_TGC_SCALE_A	2029
#define ECMD_TGC_SCALE_B	2030
#define ECMD_TGC_SCALE_C	2031
#define ECMD_TGC_SCALE_D	2032
#define ECMD_TGC_SCALE_AB	2033
#define ECMD_TGC_SCALE_CD	2034
#define ECMD_TGC_SCALE_ABCD	2035
/* TGC combined scale+move operations (librt 2111/2112, distinct from MV_H_CD=81/MV_H_V_AB=82) */
#define ECMD_TGC_S_H_CD		2111	/* scale H adjusting C,D (edtgc.c ECMD_TGC_SCALE_H_CD) */
#define ECMD_TGC_S_H_V_AB	2112	/* scale H+move V adjusting A,B (edtgc.c ECMD_TGC_SCALE_H_V_AB) */
#define ECMD_SUPERELL_SCALE_A	35113	/* scale SUPERELL semiaxis A (edsuperell.c) */
#define ECMD_SUPERELL_SCALE_B	35114	/* scale SUPERELL semiaxis B (edsuperell.c) */
#define ECMD_SUPERELL_SCALE_C	35115	/* scale SUPERELL semiaxis C (edsuperell.c) */
#define ECMD_SUPERELL_SCALE_ABC	35116	/* scale SUPERELL A,B,C uniformly (edsuperell.c) */

#define SEDIT_ROTATE (s->global_editing_state == ST_S_EDIT && \
		      (MEDIT(s)->edit_flag == SROT || \
		       MEDIT(s)->edit_flag == ECMD_TGC_ROT_H || \
		       MEDIT(s)->edit_flag ==  ECMD_TGC_ROT_AB || \
		       MEDIT(s)->edit_flag == ECMD_ARB_ROTATE_FACE || \
		       MEDIT(s)->edit_flag == ECMD_EXTR_ROT_H || \
		       MEDIT(s)->edit_flag == ECMD_ETO_ROT_C || \
		       /* librt generic & matrix rotate */ \
		       MEDIT(s)->edit_flag == RT_PARAMS_EDIT_ROT || \
		       MEDIT(s)->edit_flag == RT_MATRIX_EDIT_ROT))
#define OEDIT_ROTATE (s->global_editing_state == ST_O_EDIT && \
		      edobj == BE_O_ROTATE)
#define EDIT_ROTATE (SEDIT_ROTATE || OEDIT_ROTATE)

#define SEDIT_SCALE (s->global_editing_state == ST_S_EDIT && \
		     (MEDIT(s)->edit_flag == SSCALE || \
		      MEDIT(s)->edit_flag == ECMD_TOR_R1 || \
		      MEDIT(s)->edit_flag == ECMD_TOR_R2 || \
		      MEDIT(s)->edit_flag == ECMD_ELL_SCALE_A || \
		      MEDIT(s)->edit_flag == ECMD_ELL_SCALE_B || \
		      MEDIT(s)->edit_flag == ECMD_ELL_SCALE_C || \
		      MEDIT(s)->edit_flag == ECMD_ELL_SCALE_ABC || \
		      MEDIT(s)->edit_flag == ECMD_PART_H || \
		      MEDIT(s)->edit_flag == ECMD_PART_VRAD || \
		      MEDIT(s)->edit_flag == ECMD_PART_HRAD || \
		      MEDIT(s)->edit_flag == ECMD_RPC_B || \
		      MEDIT(s)->edit_flag == ECMD_RPC_H || \
		      MEDIT(s)->edit_flag == ECMD_RPC_R || \
		      MEDIT(s)->edit_flag == ECMD_RHC_B || \
		      MEDIT(s)->edit_flag == ECMD_RHC_H || \
		      MEDIT(s)->edit_flag == ECMD_RHC_R || \
		      MEDIT(s)->edit_flag == ECMD_RHC_C || \
		      MEDIT(s)->edit_flag == ECMD_EPA_H || \
		      MEDIT(s)->edit_flag == ECMD_EPA_R1 || \
		      MEDIT(s)->edit_flag == ECMD_EPA_R2 || \
		      MEDIT(s)->edit_flag == ECMD_EHY_H || \
		      MEDIT(s)->edit_flag == ECMD_EHY_R1 || \
		      MEDIT(s)->edit_flag == ECMD_EHY_R2 || \
		      MEDIT(s)->edit_flag == ECMD_EHY_C || \
		      MEDIT(s)->edit_flag == ECMD_ETO_R || \
		      MEDIT(s)->edit_flag == ECMD_ETO_RD || \
		      MEDIT(s)->edit_flag == ECMD_ETO_SCALE_C || \
		      MEDIT(s)->edit_flag == ECMD_HYP_H || \
		      MEDIT(s)->edit_flag == ECMD_HYP_SCALE_A || \
		      MEDIT(s)->edit_flag == ECMD_HYP_SCALE_B || \
		      MEDIT(s)->edit_flag == ECMD_HYP_C || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_H || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_H_V || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_A || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_B || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_C || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_D || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_AB || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_CD || \
		      MEDIT(s)->edit_flag == ECMD_TGC_SCALE_ABCD || \
		      MEDIT(s)->edit_flag == ECMD_TGC_S_H_CD || \
		      MEDIT(s)->edit_flag == ECMD_TGC_S_H_V_AB || \
		      MEDIT(s)->edit_flag == ECMD_SUPERELL_SCALE_A || \
		      MEDIT(s)->edit_flag == ECMD_SUPERELL_SCALE_B || \
		      MEDIT(s)->edit_flag == ECMD_SUPERELL_SCALE_C || \
		      MEDIT(s)->edit_flag == ECMD_SUPERELL_SCALE_ABC || \
		      MEDIT(s)->edit_flag == ECMD_VOL_THRESH_LO || \
		      MEDIT(s)->edit_flag == ECMD_VOL_THRESH_HI || \
		      MEDIT(s)->edit_flag == ECMD_VOL_CSIZE || \
		      MEDIT(s)->edit_flag == ECMD_DSP_SCALE_X || \
		      MEDIT(s)->edit_flag == ECMD_DSP_SCALE_Y || \
		      MEDIT(s)->edit_flag == ECMD_DSP_SCALE_ALT || \
		      MEDIT(s)->edit_flag == ECMD_EBM_HEIGHT || \
		      MEDIT(s)->edit_flag == ECMD_CLINE_SCALE_H || \
		      MEDIT(s)->edit_flag == ECMD_CLINE_SCALE_R || \
		      MEDIT(s)->edit_flag == ECMD_CLINE_SCALE_T || \
		      MEDIT(s)->edit_flag == ECMD_EXTR_SCALE_H  || \
		      /* PIPE and METABALL scale operations */ \
		      MEDIT(s)->edit_flag == ECMD_PIPE_PT_OD || \
		      MEDIT(s)->edit_flag == ECMD_PIPE_PT_ID || \
		      MEDIT(s)->edit_flag == ECMD_PIPE_PT_RADIUS || \
		      MEDIT(s)->edit_flag == ECMD_PIPE_SCALE_OD || \
		      MEDIT(s)->edit_flag == ECMD_PIPE_SCALE_ID || \
		      MEDIT(s)->edit_flag == ECMD_PIPE_SCALE_RADIUS || \
		      MEDIT(s)->edit_flag == ECMD_METABALL_SET_THRESHOLD || \
		      MEDIT(s)->edit_flag == ECMD_METABALL_SET_METHOD || \
		      MEDIT(s)->edit_flag == ECMD_METABALL_PT_FLDSTR || \
		      /* librt generic & matrix scales */ \
		      MEDIT(s)->edit_flag == RT_PARAMS_EDIT_SCALE || \
		      MEDIT(s)->edit_flag == RT_MATRIX_EDIT_SCALE || \
		      MEDIT(s)->edit_flag == RT_MATRIX_EDIT_SCALE_X || \
		      MEDIT(s)->edit_flag == RT_MATRIX_EDIT_SCALE_Y || \
		      MEDIT(s)->edit_flag == RT_MATRIX_EDIT_SCALE_Z))
#define OEDIT_SCALE (s->global_editing_state == ST_O_EDIT && \
		     (edobj == BE_O_XSCALE || \
		      edobj == BE_O_YSCALE || \
		      edobj == BE_O_ZSCALE || \
		      edobj == BE_O_SCALE))
#define EDIT_SCALE (SEDIT_SCALE || OEDIT_SCALE)

#define SEDIT_TRAN (s->global_editing_state == ST_S_EDIT && \
		    (MEDIT(s)->edit_flag == STRANS || \
		     MEDIT(s)->edit_flag == ECMD_TGC_MV_H || \
		     MEDIT(s)->edit_flag == ECMD_TGC_MV_HH || \
		     MEDIT(s)->edit_flag == EARB || \
		     MEDIT(s)->edit_flag == PTARB || \
		     MEDIT(s)->edit_flag == ECMD_ARB_MOVE_FACE || \
		     MEDIT(s)->edit_flag == ECMD_VTRANS || \
		     MEDIT(s)->edit_flag == ECMD_NMG_EMOVE || \
		     MEDIT(s)->edit_flag == ECMD_NMG_ESPLIT || \
		     MEDIT(s)->edit_flag == ECMD_NMG_LEXTRU || \
		     MEDIT(s)->edit_flag == ECMD_PIPE_PT_MOVE || \
		     MEDIT(s)->edit_flag == ECMD_PIPE_SPLIT || \
		     MEDIT(s)->edit_flag == ECMD_PIPE_PT_ADD || \
		     MEDIT(s)->edit_flag == ECMD_PIPE_PT_INS || \
		     MEDIT(s)->edit_flag == ECMD_ARS_MOVE_PT || \
		     MEDIT(s)->edit_flag == ECMD_ARS_MOVE_CRV || \
		     MEDIT(s)->edit_flag == ECMD_ARS_MOVE_COL || \
		     MEDIT(s)->edit_flag == ECMD_BOT_MOVEV || \
		     MEDIT(s)->edit_flag == ECMD_BOT_MOVEE || \
		     MEDIT(s)->edit_flag == ECMD_BOT_MOVET || \
		     MEDIT(s)->edit_flag == ECMD_CLINE_MOVE_H || \
		     MEDIT(s)->edit_flag == ECMD_EXTR_MOV_H  || \
		     /* librt generic & matrix translations */ \
		     MEDIT(s)->edit_flag == RT_PARAMS_EDIT_TRANS || \
		     MEDIT(s)->edit_flag == RT_MATRIX_EDIT_TRANS_VIEW_XY || \
		     MEDIT(s)->edit_flag == RT_MATRIX_EDIT_TRANS_VIEW_X || \
		     MEDIT(s)->edit_flag == RT_MATRIX_EDIT_TRANS_VIEW_Y))
#define OEDIT_TRAN (s->global_editing_state == ST_O_EDIT && \
		    (edobj == BE_O_X || \
		     edobj == BE_O_Y || \
		     edobj == BE_O_XY))
#define EDIT_TRAN (SEDIT_TRAN || OEDIT_TRAN)

#define SEDIT_PICK (s->global_editing_state == ST_S_EDIT && \
		    (MEDIT(s)->edit_flag == ECMD_NMG_EPICK || \
		     MEDIT(s)->edit_flag == ECMD_PIPE_PICK || \
		     MEDIT(s)->edit_flag == ECMD_ARS_PICK || \
		     MEDIT(s)->edit_flag == ECMD_BOT_PICKV || \
		     MEDIT(s)->edit_flag == ECMD_BOT_PICKE || \
		     MEDIT(s)->edit_flag == ECMD_BOT_PICKT || \
		     MEDIT(s)->edit_flag == ECMD_METABALL_PT_PICK))


extern fastf_t es_peqn[7][4];	/* ARBs defining plane equations */
extern int es_menu;		/* item/edit_mode selected from menu */

// NMG editing vars
extern struct edgeuse *es_eu;
extern struct loopuse *lu_copy;
extern point_t lu_keypoint;
extern plane_t lu_pl;
extern struct shell *es_s;

extern struct wdb_pipe_pnt *es_pipe_pnt;
extern struct wdb_metaball_pnt *es_metaball_pnt;

extern mat_t es_mat;		/* accumulated matrix of path */
extern mat_t es_invmat;		/* inverse of es_mat KAA */

extern point_t es_keypoint;	/* center of editing xforms */
extern char *es_keytag;		/* string identifying the keypoint */
extern point_t curr_e_axes_pos;	/* center of editing xforms */

extern void
get_solid_keypoint(struct mged_state *s, fastf_t *pt, const char **strp, struct rt_db_internal *ip, fastf_t *mat);

extern void set_e_axes_pos(struct mged_state *s, int both);


#endif /* MGED_SEDIT_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */

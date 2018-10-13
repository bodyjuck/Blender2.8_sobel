/*
 * Copyright 2018, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Kinouti Takahiro
 *
 */

/** \file eevee_sobel.c
 *  \ingroup draw_engine
 *
 * depth and normal sobel line rendering post process effect.
 */

#include "DRW_render.h"

#include "BLI_dynstr.h"
#include "BLI_rand.h"

#include "DNA_anim_types.h"
#include "DNA_camera_types.h"
#include "DNA_object_force_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"

#include "BKE_global.h" /* for G.debug_value */
#include "BKE_camera.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_animsys.h"
#include "BKE_screen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "eevee_private.h"
#include "GPU_extensions.h"
#include "GPU_framebuffer.h"
#include "GPU_texture.h"

#include "ED_screen.h"

static struct {
	/* Sobel */
	struct GPUShader *sobel_sh;
} e_data = {NULL}; /* Engine data */

extern char datatoc_effect_sobel_frag_glsl[];

static void eevee_create_shader_sobel(void)
{
	e_data.sobel_sh = DRW_shader_create_fullscreen(
	        datatoc_effect_sobel_frag_glsl, "");
}

int EEVEE_sobel_init(EEVEE_ViewLayerData *sldata, EEVEE_Data *vedata, Object *camera)
{
	EEVEE_StorageList *stl = vedata->stl;
	EEVEE_FramebufferList *fbl = vedata->fbl;
	EEVEE_EffectsInfo *effects = stl->effects;
	EEVEE_CommonUniformBuffer *common_data = &sldata->common_data;

	const DRWContextState *draw_ctx = DRW_context_state_get();
	const Scene *scene_eval = DEG_get_evaluated_scene(draw_ctx->depsgraph);

	if (scene_eval->eevee.flag & SCE_EEVEE_SOBEL_ENABLED) {
		const float *viewport_size = DRW_viewport_size_get();
		const float clip_start = fabs(common_data->view_vecs[0][2]);
		const float clip_end = fabs(clip_start + common_data->view_vecs[1][2]);

		if (!e_data.sobel_sh) {
			eevee_create_shader_sobel();
		}

		/* Parameters */
		effects->uniform_texel_size[0] = 1.f/viewport_size[0];
		effects->uniform_texel_size[1] = 1.f/viewport_size[1];
		effects->clip_start = clip_start;
		effects->clip_end   = clip_end;
		effects->sobel_normal_threshold = scene_eval->eevee.sobel_normal_threshold;
		effects->sobel_normal_strength = scene_eval->eevee.sobel_normal_strength;
		effects->sobel_normal_depth_decay = scene_eval->eevee.sobel_normal_depth_decay;
		effects->sobel_depth_threshold = scene_eval->eevee.sobel_depth_threshold;
		effects->sobel_depth_strength = scene_eval->eevee.sobel_depth_strength;
		effects->sobel_line_thickness = scene_eval->eevee.sobel_line_thickness;
		memcpy(effects->sobel_line_color, scene_eval->eevee.sobel_line_color, sizeof(effects->sobel_line_color));

		return EFFECT_SOBEL | EFFECT_NORMAL_BUFFER |  EFFECT_POST_BUFFER;
	}

	return 0;
}

void EEVEE_sobel_cache_init(EEVEE_ViewLayerData *UNUSED(sldata), EEVEE_Data *vedata)
{
	EEVEE_PassList *psl = vedata->psl;
	EEVEE_StorageList *stl = vedata->stl;
	EEVEE_EffectsInfo *effects = stl->effects;
	DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

	if ((effects->enabled_effects & EFFECT_SOBEL) != 0) {

		DRWShadingGroup *grp;
		struct GPUBatch *quad = DRW_cache_fullscreen_quad_get();

		psl->sobel = DRW_pass_create("Sobel", DRW_STATE_WRITE_COLOR);
		grp = DRW_shgroup_create(e_data.sobel_sh, psl->sobel);
		DRW_shgroup_uniform_texture_ref(grp, "tex_depth", &dtxl->depth);
		DRW_shgroup_uniform_texture_ref(grp, "tex_color", &effects->source_buffer);
		DRW_shgroup_uniform_texture_ref(grp, "tex_normal", &effects->ssr_normal_input);
		DRW_shgroup_uniform_vec2(grp, "offset", effects->uniform_texel_size, 1);
		DRW_shgroup_uniform_float(grp, "z_near", &effects->clip_start, 1);
		DRW_shgroup_uniform_float(grp, "z_far", &effects->clip_end, 1);
		DRW_shgroup_uniform_float(grp, "normal_threshold", &effects->sobel_normal_threshold, 1);
		DRW_shgroup_uniform_float(grp, "normal_strength", &effects->sobel_normal_strength, 1);
		DRW_shgroup_uniform_float(grp, "normal_depth_decay", &effects->sobel_normal_depth_decay, 1);
		DRW_shgroup_uniform_float(grp, "depth_threshold", &effects->sobel_depth_threshold, 1);
		DRW_shgroup_uniform_float(grp, "depth_strength", &effects->sobel_depth_strength, 1);
		DRW_shgroup_uniform_float(grp, "line_thickness", &effects->sobel_line_thickness, 1);
		DRW_shgroup_uniform_vec3(grp, "line_color", effects->sobel_line_color, 1);

		DRW_shgroup_call_add(grp, quad, NULL);

		return;
	}
}

void EEVEE_sobel_draw(EEVEE_Data *vedata)
{
	EEVEE_PassList *psl = vedata->psl;
	EEVEE_TextureList *txl = vedata->txl;
	EEVEE_FramebufferList *fbl = vedata->fbl;
	EEVEE_StorageList *stl = vedata->stl;
	EEVEE_EffectsInfo *effects = stl->effects;

	/* Sobel */
	if ((effects->enabled_effects & EFFECT_SOBEL) != 0) {
		GPU_framebuffer_bind(effects->target_buffer);
		DRW_draw_pass(psl->sobel);
		SWAP_BUFFERS();
	}
}

void EEVEE_sobel_free(void)
{
	DRW_SHADER_FREE_SAFE(e_data.sobel_sh);
}

#include "shared-texture-filter.h"

#ifdef DEBUG
#include <string>
#endif;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(OBS_PLUGIN, OBS_PLUGIN_LANG)

static const char *filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text(OBS_UI_SETTING_FILTER_NAME);
}

static obs_properties_t *filter_properties(void *data)
{
	UNUSED_PARAMETER(data);

	auto props = obs_properties_create();

	obs_properties_add_text(props, OBS_UI_SETTING_DESC_NAME,
				obs_module_text(OBS_UI_SETTING_DESC_NAME),
				OBS_TEXT_DEFAULT);

	return props;
}

static void filter_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

namespace SharedTexture {

static void texrender_reset_textures(void *data, uint32_t width,
				     uint32_t height)
{
	auto filter = (struct filter *)data;

	gs_texrender_reset(filter->texrender_current_ptr);
	if (gs_texrender_begin(filter->texrender_current_ptr, width, height)) {
		gs_texrender_end(filter->texrender_current_ptr);
	}

	gs_texrender_reset(filter->texrender_previous_ptr);
	if (gs_texrender_begin(filter->texrender_previous_ptr, width, height)) {
		gs_texrender_end(filter->texrender_previous_ptr);
	}
}

static void texrender_update_pointers(void *data)
{
	auto filter = (struct filter *)data;

	filter->d3d11_current_ptr = nullptr;
	filter->d3d11_current_ptr =
		(ID3D11Texture2D*)gs_texture_get_obj(
		gs_texrender_get_texture(filter->texrender_current_ptr));

	filter->d3d11_previous_ptr = nullptr;
	filter->d3d11_previous_ptr =
		(ID3D11Texture2D *)gs_texture_get_obj(
		gs_texrender_get_texture(filter->texrender_previous_ptr));

	filter->d3d11_shared_ptr = nullptr;
	filter->d3d11_shared_ptr =
		(ID3D11Texture2D *)gs_texture_get_obj(filter->texture_shared_ptr);
}

static void create_d3d11_context(void* data)
{
	auto filter = (struct filter *)data;
	ID3D11Device *d3d11_device = (ID3D11Device *)gs_get_device_obj();
	d3d11_device->GetImmediateContext(&filter->d3d11_context_ptr);
	d3d11_device = nullptr;
}

static void shared_texture_create(void *data, uint32_t cx, uint32_t cy)
{
	auto filter = (struct filter *)data;

	// Should NEVER have a valid shared_ptr here
	if (filter->texture_shared_ptr) { // should not be here
		warn("shared_texture_create warning :: shared texture not empty");
		gs_texture_destroy(filter->texture_shared_ptr);
		filter->texture_shared_ptr = nullptr;
		filter->d3d11_shared_ptr = nullptr;
	}

	// Actually create the texture
	filter->texture_shared_ptr = gs_texture_create(cx, cy, OBS_PLUGIN_COLOR_SPACE, 1, NULL, GS_SHARED_TEX);

	// Update the shared texture handles
	shared_texture_update_shared_handle(filter);

	// Create out d3d11 device context if it is empty
	if (!filter->d3d11_context_ptr)
		create_d3d11_context(filter);

	// Reset texrender textures
	texrender_reset_textures(filter, cx, cy);

	// Update various texture pointers (d3d11, texrender)
	texrender_update_pointers(filter);
}

static void shared_texture_update_shared_handle(void *data)
{
	auto filter = (struct filter *)data;
	auto handle = gs_texture_get_shared_handle(filter->texture_shared_ptr);
#ifdef DEBUG
	auto ws = "\r\n\r\n\r\n<<<===>>> SHARED TEXTURE HANDLE : " + std::to_string(handle) + "\r\n\r\n\r\n";
#endif;
	blog(LOG_INFO, ws.c_str());
}

static void render_shared_texture(void *data, obs_source_t *target, uint32_t cx, uint32_t cy)
{
	auto filter = (struct filter *)data;	

	gs_texrender_t *buffer_texrender = filter->render_swap
						   ? filter->texrender_current_ptr
						   : filter->texrender_previous_ptr;

	gs_texrender_reset(buffer_texrender);
	if (gs_texrender_begin(buffer_texrender, cx, cy)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();
		gs_texrender_end(buffer_texrender);
	}

	buffer_texrender = nullptr;
}

static void shared_texture_copy_resources(void *data)
{
	auto filter = (struct filter *)data;

	if (filter->d3d11_context_ptr) {

		if (filter->render_swap) {
			filter->d3d11_context_ptr->CopyResource(
				filter->d3d11_shared_ptr,
				filter->d3d11_current_ptr);
		} else {
			filter->d3d11_context_ptr->CopyResource(
				filter->d3d11_shared_ptr,
				filter->d3d11_previous_ptr);
		}

		if (filter->render_flush)
			filter->d3d11_context_ptr->Flush();
	}
}

static void filter_render_callback(void *data, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);

	if (!data)
		return;

	auto filter = (struct filter *)data;

	if (!filter->context)
		return;

	auto target = obs_filter_get_parent(filter->context);

	if (!target)
		return;

	auto target_width = obs_source_get_base_width(target);
	auto target_height = obs_source_get_base_height(target);

	auto size_changed = filter->texture_shared_width != target_width ||
			    filter->texture_shared_height != target_height;

	// update shared sizes if changed
	if (filter->texture_shared_width != target_width) {
		filter->texture_shared_width = target_width;
	}
	if (filter->texture_shared_height != target_height) {
		filter->texture_shared_height = target_height;
	}
	
	if (target_width == 0 || target_height == 0)
		return;

	if (size_changed && filter->texture_shared_ptr)
	{			
		gs_texture_destroy(filter->texture_shared_ptr);
		filter->texture_shared_ptr = nullptr;
	}

	// create shared texture
	if (!filter->texture_shared_ptr)
	{		
		shared_texture_create(filter, target_width, target_height);


		return;
	}
	
	render_shared_texture(filter, target, target_width, target_height);
	shared_texture_copy_resources(filter);
		
	filter->render_swap = !filter->render_swap;
}

static void filter_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);

	auto filter = (struct filter *)data;

	obs_remove_main_render_callback(filter_render_callback, filter);

	// do some thing??

	obs_add_main_render_callback(filter_render_callback, filter);
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{	
	auto filter = (struct filter *)bzalloc(sizeof(SharedTexture::filter));

	filter->context = source;

	filter->render_flush = true;
		
	filter->texrender_previous_ptr =
		gs_texrender_create(OBS_PLUGIN_COLOR_SPACE, GS_ZS_NONE);

	filter->texrender_current_ptr =
		gs_texrender_create(OBS_PLUGIN_COLOR_SPACE, GS_ZS_NONE);

	// force an update
	filter_update(filter, settings);

	return filter;
}

static void filter_destroy(void *data)
{
	auto filter = (struct filter *)data;
	
	if (filter)
	{
		obs_remove_main_render_callback(filter_render_callback, filter);

		obs_enter_graphics();

		filter->d3d11_shared_ptr = nullptr;
		filter->d3d11_current_ptr = nullptr;
		filter->d3d11_previous_ptr = nullptr;
		filter->d3d11_context_ptr = nullptr;

		gs_texrender_destroy(filter->texrender_current_ptr);		
		gs_texrender_destroy(filter->texrender_previous_ptr);

		if (filter->texture_shared_ptr)
		{
			gs_texture_destroy(filter->texture_shared_ptr);
		}

		obs_leave_graphics();
		bfree(filter);
	}
}

static void filter_video_render(void* data, gs_effect_t* effect)
{	
	UNUSED_PARAMETER(effect);

	auto filter = (struct filter *)data;

	if (!filter->context)
		return;

	obs_source_skip_video_filter(filter->context);
}

} // namespace SharedTexture

static void debug_report_version()
{
	info("you can haz shared-texture tooz (Version: %s)", OBS_PLUGIN_VERSION_STRING);	
}

bool obs_module_load(void)
{
	auto filter_info = SharedTexture::create_filter_info();

	obs_register_source(&filter_info);
	
	debug_report_version();	

	return true;
}

void obs_module_unload()
{
}

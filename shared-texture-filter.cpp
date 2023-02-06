#include <string>
#include <obs-module.h>
#include <graphics/graphics.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#define OBS_SHARED_TEXTURE_PLUGIN "shared-texture-filter"
#define OBS_SHARED_TEXTURE_VERSION_MAJOR 0
#define OBS_SHARED_TEXTURE_VERSION_MINOR 0
#define OBS_SHARED_TEXTURE_VERSION_RELEASE 1
#define OBS_SHARED_TEXTURE_VERSION_STRING "0.0.1"
#define OBS_SHARED_TEXTURE_LANG "en-US"
#define OBS_SHARED_TEXTURE_COLOR_SPACE GS_RGBA16F

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(OBS_SHARED_TEXTURE_PLUGIN, OBS_SHARED_TEXTURE_LANG)

#define do_log(level, format, ...) blog(level, "[shared-texture-filter] " format, ##__VA_ARGS__)

#define error(format, ...) do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG, format, ##__VA_ARGS__)

#define OBS_UI_SETTING_FILTER_NAME "mahgu.sharedtexture.ui.filter_title"
#define OBS_UI_SETTING_DESC_NAME "mahgu.sharedtexture.ui.name_desc"

struct shared_texture_filter {
	obs_source_t *context;

	gs_texrender_t *texrender_current;
	gs_texrender_t *texrender_previous;
	
	gs_texture_t *texture_shared;

	uint32_t shared_width;
	uint32_t shared_height;

	Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_contextPtr;

	ID3D11Texture2D *d3d11_texture_shared_ptr;
	ID3D11Texture2D *d3d11_texture_current_ptr;
	ID3D11Texture2D *d3d11_texture_previous_ptr;

	bool render_swap;
	bool render_flush;
};

static const char *shared_texture_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);	
	return obs_module_text(OBS_UI_SETTING_FILTER_NAME);
}

static obs_properties_t *shared_texture_filter_properties(void *data)
{
	UNUSED_PARAMETER(data);

	auto props = obs_properties_create();

	obs_properties_add_text(props, OBS_UI_SETTING_DESC_NAME, obs_module_text(OBS_UI_SETTING_DESC_NAME), OBS_TEXT_DEFAULT);

	return props;
}

static void shared_texture_filter_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

static void shared_texture_filter_initialize_texrender(void* data, uint32_t width, uint32_t height)
{
	auto filter = (struct shared_texture_filter *)data;
	
	gs_texrender_reset(filter->texrender_current);
	if (gs_texrender_begin(filter->texrender_current,width,height)) {
		gs_texrender_end(filter->texrender_current);
	}	

	gs_texrender_reset(filter->texrender_previous);
	if (gs_texrender_begin(filter->texrender_previous, width, height)) {
		gs_texrender_end(filter->texrender_previous);
	}	
}

static void shared_texture_filter_update_texture_pointers(void *data)
{
	auto filter = (struct shared_texture_filter *)data;	

	filter->d3d11_texture_current_ptr = nullptr;
	filter->d3d11_texture_current_ptr =
		(ID3D11Texture2D*)gs_texture_get_obj(
		gs_texrender_get_texture(filter->texrender_current));

	filter->d3d11_texture_previous_ptr = nullptr;
	filter->d3d11_texture_previous_ptr =
		(ID3D11Texture2D *)gs_texture_get_obj(
		gs_texrender_get_texture(filter->texrender_previous));

	filter->d3d11_texture_shared_ptr = nullptr;
	filter->d3d11_texture_shared_ptr =
		(ID3D11Texture2D *)gs_texture_get_obj(filter->texture_shared);
}

static void shared_texture_filter_create_d3d11_context(void* data)
{
	auto filter = (struct shared_texture_filter *)data;
	Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device = (ID3D11Device *)gs_get_device_obj();
	d3d11_device->GetImmediateContext(&filter->d3d11_contextPtr);
	d3d11_device = nullptr;
}

static void shared_texture_filter_create_shared_texture(void* data, uint32_t cx, uint32_t cy)
{
	auto filter = (struct shared_texture_filter *)data;
	filter->texture_shared = gs_texture_create(cx, cy, OBS_SHARED_TEXTURE_COLOR_SPACE, 1, NULL, GS_SHARED_TEX);
}

static void shared_texture_filter_update_shared_texture_handle(void* data)
{
	auto filter = (struct shared_texture_filter *)data;
	auto handle = gs_texture_get_shared_handle(filter->texture_shared);
	auto ws = "\r\n\r\n\r\nSHARED TEXTURE HANDLE : " + std::to_string(handle) + "\r\n\r\n\r\n";
	blog(LOG_INFO, ws.c_str());
}

static void shared_texture_fitler_render_texture(void *data, obs_source_t *target, uint32_t cx, uint32_t cy)
{
	auto filter = (struct shared_texture_filter *)data;	

	gs_texrender_t *buffer_texrender = filter->render_swap
						   ? filter->texrender_current
						   : filter->texrender_previous;

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

static void shared_texture_filter_copy_texture_resources(void* data)
{
	auto filter = (struct shared_texture_filter *)data;

	if (filter->d3d11_contextPtr) {

		if (filter->render_swap) {
			filter->d3d11_contextPtr->CopyResource(
				filter->d3d11_texture_shared_ptr,
				filter->d3d11_texture_current_ptr);
		} else {
			filter->d3d11_contextPtr->CopyResource(
				filter->d3d11_texture_shared_ptr,
				filter->d3d11_texture_previous_ptr);
		}

		if (filter->render_flush)
			filter->d3d11_contextPtr->Flush();
	}
}

static void shared_texture_filter_render_callback(void *data, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);

	if (!data)
		return;

	auto filter = (struct shared_texture_filter *)data;

	if (!filter->context)
		return;

	auto target = obs_filter_get_parent(filter->context);

	if (!target)
		return;

	auto target_width = obs_source_get_base_width(target);
	auto target_height = obs_source_get_base_height(target);

	auto size_changed = filter->shared_width != target_width ||
			    filter->shared_height != target_height;

	// update shared sizes if changed
	if (filter->shared_width != target_width) {
		filter->shared_width = target_width;
	}
	if (filter->shared_height != target_height) {
		filter->shared_height = target_height;
	}
	
	if (target_width == 0 || target_height == 0)
		return;

	if (size_changed)
	{
		if (filter->texture_shared)
		{
			blog(LOG_INFO, "\r\n\r\nTEXTURE DESTROYED\r\n\r\n");
			gs_texture_destroy(filter->texture_shared);
			filter->texture_shared = nullptr;
		}
	}

	// create shared texture
	if (!filter->texture_shared)
	{		
		shared_texture_filter_create_shared_texture(filter, target_width, target_height);
		shared_texture_filter_update_shared_texture_handle(filter);
		
		if (!filter->d3d11_contextPtr)
			shared_texture_filter_create_d3d11_context(filter);
		
		shared_texture_filter_initialize_texrender(filter, target_width, target_height);
		shared_texture_filter_update_texture_pointers(filter);

		return;
	}
	
	shared_texture_fitler_render_texture(filter, target, target_width, target_height);
	shared_texture_filter_copy_texture_resources(filter);
		
	filter->render_swap = !filter->render_swap;
}

static void shared_texture_filter_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);

	auto filter = (struct shared_texture_filter *)data;

	obs_remove_main_render_callback(shared_texture_filter_render_callback, filter);

	// do some thing??

	obs_add_main_render_callback(shared_texture_filter_render_callback, filter);	
}

static void *shared_texture_filter_create(obs_data_t *settings, obs_source_t *source)
{	
	auto filter = (struct shared_texture_filter *)bzalloc(sizeof(shared_texture_filter));

	filter->context = source;

	filter->render_flush = true;
		
	filter->texrender_previous =
		gs_texrender_create(OBS_SHARED_TEXTURE_COLOR_SPACE, GS_ZS_NONE);

	filter->texrender_current =
		gs_texrender_create(OBS_SHARED_TEXTURE_COLOR_SPACE, GS_ZS_NONE);
		
	shared_texture_filter_update(filter, settings);	

	return filter;
}

static void shared_texture_filter_destroy(void *data)
{
	auto filter = (struct shared_texture_filter *)data;	
	
	if (filter)
	{

		obs_remove_main_render_callback(
			shared_texture_filter_render_callback, filter);

		obs_enter_graphics();

		filter->d3d11_texture_shared_ptr = nullptr;
		filter->d3d11_texture_current_ptr = nullptr;
		filter->d3d11_texture_previous_ptr = nullptr;

		filter->d3d11_contextPtr = nullptr;

		gs_texrender_destroy(filter->texrender_current);		
		gs_texrender_destroy(filter->texrender_previous);

		if (filter->texture_shared)
		{
			gs_texture_destroy(filter->texture_shared);
		}

		obs_leave_graphics();
		bfree(filter);
	}
}

static void shared_texture_filter_video_render(void* data, gs_effect_t* effect)
{	
	UNUSED_PARAMETER(effect);

	auto* filter = (struct shared_texture_filter *)data;

	if (!filter->context)
		return;

	obs_source_skip_video_filter(filter->context);
}

struct obs_source_info create_filter_info()
{
	struct obs_source_info shared_texture_filter_info = {};

	shared_texture_filter_info.id = "shared_texture_filter";

	shared_texture_filter_info.type = OBS_SOURCE_TYPE_FILTER;	
	shared_texture_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;

	shared_texture_filter_info.get_name = shared_texture_filter_get_name;

	shared_texture_filter_info.get_properties = shared_texture_filter_properties;
	shared_texture_filter_info.get_defaults = shared_texture_filter_defaults;

	shared_texture_filter_info.create = shared_texture_filter_create;
	shared_texture_filter_info.destroy = shared_texture_filter_destroy;
		
	shared_texture_filter_info.video_render = shared_texture_filter_video_render;

	shared_texture_filter_info.update = shared_texture_filter_update;

	return shared_texture_filter_info;
}

static void debug_report_version()
{
	info("you can haz shared-texture tooz (Version: %s)", OBS_SHARED_TEXTURE_VERSION_STRING);	
}

bool obs_module_load(void)
{
	auto filter_info = create_filter_info();

	obs_register_source(&filter_info);
	
	debug_report_version();	

	return true;
}

void obs_module_unload()
{
}

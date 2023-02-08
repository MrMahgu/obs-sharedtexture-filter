#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#define OBS_PLUGIN "shared-texture-filter"
#define OBS_PLUGIN_ "shared_texture_filter"
#define OBS_PLUGIN_VERSION_MAJOR 0
#define OBS_PLUGIN_VERSION_MINOR 0
#define OBS_PLUGIN_VERSION_RELEASE 1
#define OBS_PLUGIN_VERSION_STRING "0.0.1"
#define OBS_PLUGIN_LANG "en-US"
#define OBS_PLUGIN_COLOR_SPACE GS_BGRA

#define OBS_UI_SETTING_FILTER_NAME "mahgu.sharedtexture.ui.filter_title"
#define OBS_UI_SETTING_DESC_NAME "mahgu.sharedtexture.ui.name_desc"

#define obs_log(level, format, ...) \
	blog(level, "[shared-texture-filter] " format, ##__VA_ARGS__)

#define error(format, ...) obs_log(LOG_ERROR, format, ##__VA_ARGS__)
#define warn(format, ...) obs_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) obs_log(LOG_INFO, format, ##__VA_ARGS__)
#define debug(format, ...) obs_log(LOG_DEBUG, format, ##__VA_ARGS__)

static const char *filter_get_name(void *unused);
static obs_properties_t *filter_properties(void *data);
static void filter_defaults(obs_data_t *settings);

bool obs_module_load(void);
void obs_module_unload();

static void debug_report_version();

namespace SharedTexture {

// OBS plugin stuff
static void *filter_create(obs_data_t *settings, obs_source_t *source);
static void filter_destroy(void *data);
static void filter_render_callback(void *data, uint32_t cx, uint32_t cy);
static void filter_update(void *data, obs_data_t *settings);
static void filter_video_render(void *data, gs_effect_t *effect);

// Additional shared-texture stuff
static void create_d3d11_context(void *data);
static void initialize_texrenders(void *data, uint32_t width, uint32_t height);
static void update_texrender_pointers(void *data);
static void copy_shared_texture_resources(void *data);
static void create_shared_texture(void *data, uint32_t cx, uint32_t cy);
static void render_shared_texture(void *data, obs_source_t *target, uint32_t cx, uint32_t cy);
static void update_shared_texture_handle(void *data);

// Filter data struct
struct filter {
	obs_source_t *context;

	gs_texrender_t *texrender_current_ptr;
	gs_texrender_t *texrender_previous_ptr;

	gs_texture_t *texture_shared_ptr;

	uint32_t texture_shared_width;
	uint32_t texture_shared_height;

	ID3D11Texture2D *d3d11_shared_ptr;
	ID3D11Texture2D *d3d11_current_ptr;
	ID3D11Texture2D *d3d11_previous_ptr;

	bool render_swap;
	bool render_flush;

	Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_context_ptr;
};

struct obs_source_info create_filter_info()
{
	struct obs_source_info filter_info = {};

	filter_info.id = OBS_PLUGIN_;
	filter_info.type = OBS_SOURCE_TYPE_FILTER;
	filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;

	filter_info.get_name = filter_get_name;
	filter_info.get_properties = filter_properties;
	filter_info.get_defaults = filter_defaults;
	filter_info.create = filter_create;
	filter_info.destroy = filter_destroy;
	filter_info.video_render = filter_video_render;
	filter_info.update = filter_update;

	return filter_info;
};

} // namespace SharedTexture

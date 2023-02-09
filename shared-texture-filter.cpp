#include "shared-texture-filter.h"

#ifdef DEBUG
#include <string>
#endif;

// Until I figure something out, this lets me know that the plugin is using
// some custom OBS changes

// OBS has been customized to support shared texrender
// (well, just the textures)
#define PATCH_TEXRENDER_SHARED

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(OBS_PLUGIN, OBS_PLUGIN_LANG)

namespace SharedTexture {

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

namespace Texrender {


#ifdef DEBUG
static void debug_report_shared_handle2(void *data)
{
	auto filter = (struct filter *)data;

	auto handle = gs_texture_get_shared_handle(filter->texture_current_ptr);

	auto ws = "\r\n\r\n\r\n<<<===>>> POSSIBLE TEXTURE HANDLE : " +
		  std::to_string(handle) + "\r\n\r\n\r\n";

	blog(LOG_INFO, ws.c_str());
}
#endif

// Makes sure the underlying gs_texture_t is created and
// updates the pointer reference
static void reset_texture(void *data, uint32_t width, uint32_t height)
{
	auto filter = (struct filter *)data;	

	gs_texrender_reset(filter->texrender_current_ptr);
	if (gs_texrender_begin(filter->texrender_current_ptr, width, height)) {
		gs_texrender_end(filter->texrender_current_ptr);
	}
}

static void update_pointer(void* data)
{
	auto filter = (struct filter *)data;

	filter->texture_current_ptr =
		gs_texrender_get_texture(filter->texrender_current_ptr);

	debug_report_shared_handle2(filter);
}

// Renders the current OBS filter ?? to one of our buffer textures
static void render(void *data, obs_source_t *target, uint32_t cx, uint32_t cy)
{
	auto filter = (struct filter *)data;

	// Render OBS source texture
	gs_texrender_reset(filter->texrender_current_ptr);
	if (gs_texrender_begin(filter->texrender_current_ptr, cx, cy)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();
		gs_texrender_end(filter->texrender_current_ptr);
	}
}

} // namespace Texrender

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

	// Store a size changed state for later
	auto size_changed = filter->texture_width != target_width ||
			    filter->texture_height != target_height;

	// update shared sizes if changed
	if (filter->texture_width != target_width) {
		filter->texture_width = target_width;
	}
	if (filter->texture_height != target_height) {
		filter->texture_height = target_height;
	}

	// return if invalid dimensions
	if (target_width == 0 || target_height == 0)
		return;

	// Check if size has changed and reset out textures/texture pointers
	if (size_changed) {
		Texrender::reset_texture(filter, target_width, target_height);
		Texrender::update_pointer(filter);
		return;
	}

	// Render and copy the latest frame to our shared texture
	Texrender::render(filter, target, target_width, target_height);
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

	// Baseline everything
	filter->texrender_current_ptr = nullptr;
	filter->texture_current_ptr = nullptr;
	filter->texture_width = 0;
	filter->texture_height = 0;

	// Setup the obs context
	filter->context = source;

	// Create our shared texture
	filter->texrender_current_ptr =
		gs_texrender_create2(OBS_PLUGIN_COLOR_SPACE, GS_ZS_NONE);

	// force an update
	filter_update(filter, settings);

	return filter;
}

static void filter_destroy(void *data)
{
	auto filter = (struct filter *)data;

	if (filter) {
		obs_remove_main_render_callback(filter_render_callback, filter);

		obs_enter_graphics();

		filter->texture_current_ptr = nullptr;

		gs_texrender_destroy(filter->texrender_current_ptr);

		filter->texrender_current_ptr = nullptr;
		
		obs_leave_graphics();

		bfree(filter);
	}
}

static void filter_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto filter = (struct filter *)data;

	if (!filter->context)
		return;

	obs_source_skip_video_filter(filter->context);
}

// Writes a simple log entry to OBS
void report_version()
{
#ifdef DEBUG
	info("you can haz shared-texture tooz (Version: %s)",
	     OBS_PLUGIN_VERSION_STRING);
#else
	info("obs-sharedtexture-filter [mrmahgu] - version %s",
	     OBS_PLUGIN_VERSION_STRING);
#endif
}

} // namespace SharedTexture

bool obs_module_load(void)
{
	auto filter_info = SharedTexture::create_filter_info();

	obs_register_source(&filter_info);

	SharedTexture::report_version();

	return true;
}

void obs_module_unload() {}

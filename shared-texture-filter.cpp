#include "shared-texture-filter.h"

#ifdef DEBUG
#include <string>
#endif;

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

// This is an odd way for me to force OBS to rebuild the texrender_t internal textures, which in our case
// have just gone through a size change
static void reset_textures(void *data, uint32_t width, uint32_t height)
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

// Updates our internal references to the texrender_t texture_t pointers
static void update_pointers(void *data)
{
	auto filter = (struct filter *)data;

	filter->texture_current_ptr =
		gs_texrender_get_texture(filter->texrender_current_ptr);

	filter->texture_previous_ptr =
		gs_texrender_get_texture(filter->texrender_previous_ptr);
}

} // namespace Texrender

namespace Texture {

#ifdef DEBUG
static void debug_report_shared_handle(void *data)
{
	auto filter = (struct filter *)data;
	auto handle = gs_texture_get_shared_handle(filter->texture_shared_ptr);
	auto ws = "\r\n\r\n\r\n<<<===>>> SHARED TEXTURE HANDLE : " +
		  std::to_string(handle) + "\r\n\r\n\r\n";
	blog(LOG_INFO, ws.c_str());
}
#endif

// Creates (or replaces) the actual shared texture on the device
static void create(void *data, uint32_t cx, uint32_t cy)
{
	auto filter = (struct filter *)data;

	if (filter->texture_shared_ptr)
		return;

	filter->texture_shared_ptr = gs_texture_create(
		cx, cy, OBS_PLUGIN_COLOR_SPACE, 1, NULL, GS_SHARED_TEX);

#ifdef DEBUG
	debug_report_shared_handle(filter);
#endif

	Texrender::reset_textures(filter, cx, cy);
	Texrender::update_pointers(filter);
}

// Destroys the shared texture from the device
static void destroy(void *data)
{
	auto filter = (struct filter *)data;

	gs_texture_destroy(filter->texture_shared_ptr);
	filter->texture_shared_ptr = nullptr;
}

// Renders the current OBS filter ?? to one of our buffer textures
static void render(void *data, obs_source_t *target, uint32_t cx, uint32_t cy)
{
	auto filter = (struct filter *)data;

	// Choose which texrender to write to
	gs_texrender_t *buffer_texrender =
		filter->render_swap ? filter->texrender_current_ptr
				    : filter->texrender_previous_ptr;

	// Render OBS source texture
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

// Copies the one of the current buffers to the shared texture on the device
static void copy(void *data)
{
	auto filter = (struct filter *)data;

	if (!filter->texture_shared_ptr)
		return;

	gs_texture_t *source_texture = filter->render_swap
					       ? filter->texture_current_ptr
					       : filter->texture_previous_ptr;

	gs_copy_texture(filter->texture_shared_ptr, source_texture);

	source_texture = nullptr;

	if (filter->render_flush)
		gs_flush();
}

} // namespace Texture

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
	auto size_changed = filter->texture_shared_width != target_width ||
			    filter->texture_shared_height != target_height;

	// update shared sizes if changed
	if (filter->texture_shared_width != target_width) {
		filter->texture_shared_width = target_width;
	}
	if (filter->texture_shared_height != target_height) {
		filter->texture_shared_height = target_height;
	}

	// return if invalid dimensions
	if (target_width == 0 || target_height == 0)
		return;

	// Check to see if we need to destroy the current texture
	// This happens if the source texture size changes from above
	if (size_changed && filter->texture_shared_ptr)
		Texture::destroy(filter);

	// create shared texture (if needed) and return
	if (!filter->texture_shared_ptr) {
		Texture::create(filter, target_width, target_height);
		return;
	}

	// Render and copy the latest frame to our shared texture
	Texture::render(filter, target, target_width, target_height);
	Texture::copy(filter);

	// Swap which internal texture to use next frame
	// Not sure if any of this swapping texture stuff is really usefull or not
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

	if (filter) {
		obs_remove_main_render_callback(filter_render_callback, filter);

		obs_enter_graphics();

		// de-reference some pointers
		filter->texture_current_ptr = nullptr;
		filter->texture_previous_ptr = nullptr;

		gs_texrender_destroy(filter->texrender_current_ptr);
		gs_texrender_destroy(filter->texrender_previous_ptr);

		filter->texrender_current_ptr = nullptr;
		filter->texrender_previous_ptr = nullptr;

		if (filter->texture_shared_ptr) {
			gs_texture_destroy(filter->texture_shared_ptr);
			filter->texture_shared_ptr = nullptr;
		}

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

} // namespace SharedTexture

#ifdef DEBUG
static void debug_report_version()
{
	info("you can haz shared-texture tooz (Version: %s)",
	     OBS_PLUGIN_VERSION_STRING);
}
#else
static void report_version()
{
	info("obs-sharedtexture-filter [mrmahgu] - version %s",
	     OBS_PLUGIN_vERSION_STRING);
}
#endif

bool obs_module_load(void)
{
	auto filter_info = SharedTexture::create_filter_info();

	obs_register_source(&filter_info);

#ifdef DEBUG
	debug_report_version();
#else
	report_version();
#endif

	return true;
}

void obs_module_unload() {}

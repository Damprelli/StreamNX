#include "api/mpv_player.hpp"

#include <mpv/client.h>

#if defined(BOREALIS_USE_DEKO3D)
#include <mpv/render_dk3d.h>
#include <borealis/platforms/switch/switch_video.hpp>
#else
#include <mpv/render_gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace
{

void* getProcAddress(void*, const char* name)
{
    return (void*)glfwGetProcAddress(name);
}

} // namespace
#endif

MpvPlayer& MpvPlayer::instance()
{
    static MpvPlayer player;
    return player;
}

MpvPlayer::MpvPlayer() { }

MpvPlayer::~MpvPlayer()
{
    if (this->renderCtx)
        mpv_render_context_free(this->renderCtx);
    if (this->mpv)
        mpv_terminate_destroy(this->mpv);
}

void MpvPlayer::setCallbacks(
    std::function<void()> loaded, std::function<void(std::string)> err, std::function<void()> endOfFile)
{
    this->onLoaded    = loaded;
    this->onError     = err;
    this->onEndOfFile = endOfFile;
}

void MpvPlayer::clearCallbacks()
{
    this->onLoaded    = nullptr;
    this->onError     = nullptr;
    this->onEndOfFile = nullptr;
}

void MpvPlayer::onWakeup(void* self)
{
    auto* player = static_cast<MpvPlayer*>(self);
    brls::sync([player]() { player->pumpEvents(); });
}

void MpvPlayer::onRenderUpdate(void* self)
{
    auto* player = static_cast<MpvPlayer*>(self);
    brls::sync([player]() {
        if (player->renderCtx)
            mpv_render_context_update(player->renderCtx);
    });
}

void MpvPlayer::ensureInitialized()
{
    if (this->mpv)
        return;

    this->mpv = mpv_create();
    if (!this->mpv)
        return;

    mpv_set_option_string(this->mpv, "vo", "libmpv");
#if defined(__SWITCH__)
    // The Tegra X1's 4 Cortex-A57 cores can't software-decode anything past
    // ~1080p in real time (this is what "streams above 1080p stutter/the
    // player can't keep up" actually is -- CPU-bound decode, not a render
    // bug). The ffmpeg built for this app includes StreamNX-main's own
    // "nvtegra" patches (--enable-nvtegra), which add a hwaccel that
    // decodes on the Tegra's dedicated video engine instead of the CPU;
    // "auto" is the same value StreamNX-main itself uses for hwdec on
    // Switch, and lets mpv/ffmpeg pick it automatically for a compatible
    // stream instead of naming it explicitly.
    mpv_set_option_string(this->mpv, "hwdec", "auto");
#else
    // Desktop keeps software decode: a generic desktop hwdec (VAAPI/DXVA2/
    // whatever the driver offers) hands back textures/formats this app's
    // FBO blit doesn't necessarily expect, which just renders nothing,
    // silently, and desktop CPUs decode 1080p/4K fine anyway.
    mpv_set_option_string(this->mpv, "hwdec", "no");
#endif
    mpv_set_option_string(this->mpv, "osd-level", "0");
    mpv_set_option_string(this->mpv, "ytdl", "no");
    mpv_set_option_string(this->mpv, "terminal", "no");
    mpv_set_option_string(this->mpv, "msg-level", "all=warn");
    // Long-lived HTTP(S) streams (whole movies/episodes) get their
    // connection throttled or dropped mid-file by some CDNs; without this,
    // ffmpeg's reader just stalls forever, which looks like endless
    // buffering instead of a recoverable hiccup.
    mpv_set_option_string(
        this->mpv, "stream-lavf-o", "reconnect=1,reconnect_streamed=1,reconnect_delay_max=7,rw_timeout=15000000");
    // ffmpeg's default HTTP user agent ("Lavf/...") is an obvious tell for
    // a non-browser client -- some stream hosts throttle/cut those
    // connections aggressively (repeated "stream ends prematurely, will
    // reconnect" is the usual symptom), which a normal-looking UA often
    // avoids.
    mpv_set_option_string(this->mpv, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");

#if defined(__SWITCH__)
    // Straight from StreamNX-main, which sets these specifically for
    // Switch: direct rendering + a hard cap on lavc decode threads (the
    // Switch has neither the cores nor the RAM to let ffmpeg pick its own
    // thread count the way desktop can). "opengl-glfinish" is carried over
    // verbatim too -- StreamNX-main's own comment on it is "This should fix
    // random crash, but I don't know why", which matches the exact symptom
    // reported here (crashes starting playback); mpv accepts the option
    // regardless of which VO is actually driving the render API, so it's
    // harmless to set even though this build renders via deko3d, not GL.
    mpv_set_option_string(this->mpv, "vd-lavc-dr", "yes");
    mpv_set_option_string(this->mpv, "vd-lavc-threads", "3");
    mpv_set_option_string(this->mpv, "opengl-glfinish", "yes");
#endif

    if (mpv_initialize(this->mpv) < 0)
    {
        mpv_terminate_destroy(this->mpv);
        this->mpv = nullptr;
        return;
    }

#if defined(BOREALIS_USE_DEKO3D)
    auto* videoContext = dynamic_cast<brls::SwitchVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    if (!videoContext)
    {
        brls::Logger::error("MpvPlayer: no SwitchVideoContext available, cannot init deko3d render context");
        mpv_terminate_destroy(this->mpv);
        this->mpv = nullptr;
        return;
    }
    mpv_deko3d_init_params dkInitParams { videoContext->getDeko3dDevice() };
    mpv_render_param initParams[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_DEKO3D) },
        { MPV_RENDER_PARAM_DEKO3D_INIT_PARAMS, &dkInitParams },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };
#else
    mpv_opengl_init_params glInitParams { getProcAddress, nullptr };
    mpv_render_param initParams[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };
#endif

    int createResult = mpv_render_context_create(&this->renderCtx, this->mpv, initParams);
    if (createResult < 0)
    {
        brls::Logger::error("MpvPlayer: render context creation failed: {}", mpv_error_string(createResult));
        mpv_terminate_destroy(this->mpv);
        this->mpv = nullptr;
        return;
    }

    mpv_set_wakeup_callback(this->mpv, &MpvPlayer::onWakeup, this);
    mpv_render_context_set_update_callback(this->renderCtx, &MpvPlayer::onRenderUpdate, this);

    mpv_observe_property(this->mpv, 1, "pause", MPV_FORMAT_FLAG);
    mpv_request_log_messages(this->mpv, "warn");
}

void MpvPlayer::pumpEvents()
{
    while (this->mpv)
    {
        mpv_event* event = mpv_wait_event(this->mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            return;

        switch (event->event_id)
        {
            case MPV_EVENT_LOG_MESSAGE:
            {
                auto* log = (mpv_event_log_message*)event->data;
                if (log->log_level <= MPV_LOG_LEVEL_ERROR)
                    brls::Logger::error("mpv: {}: {}", log->prefix, log->text);
                else
                    brls::Logger::warning("mpv: {}: {}", log->prefix, log->text);
                break;
            }

            case MPV_EVENT_FILE_LOADED:
                if (this->onLoaded)
                    this->onLoaded();
                break;

            case MPV_EVENT_END_FILE:
            {
                auto* data = (mpv_event_end_file*)event->data;
                if (data->reason == MPV_END_FILE_REASON_ERROR)
                {
                    brls::Logger::error("MpvPlayer: playback error: {}", mpv_error_string(data->error));
                    if (this->onError)
                        this->onError(mpv_error_string(data->error));
                }
                else if (data->reason == MPV_END_FILE_REASON_EOF && this->onEndOfFile)
                {
                    this->onEndOfFile();
                }
                break;
            }

            case MPV_EVENT_PROPERTY_CHANGE:
            {
                auto* prop = (mpv_event_property*)event->data;
                if (event->reply_userdata == 1 && prop->format == MPV_FORMAT_FLAG)
                    this->paused = *(int*)prop->data != 0;
                break;
            }

            default:
                break;
        }
    }
}

void MpvPlayer::loadUrl(const std::string& url)
{
    this->ensureInitialized();
    if (!this->mpv)
    {
        if (this->onError)
            this->onError("Nao foi possivel iniciar o mpv.");
        return;
    }

    // StreamNX-main's VideoView re-affirms this every time it opens
    // (MPVCore::enableVO(true)) rather than trusting it to have stuck from
    // whenever mpv was first created -- mpv can fall back away from a vo
    // driver on its own (e.g. after certain errors), and since this is a
    // process-lifetime singleton reused across every playback session,
    // nothing else would ever set it back.
    mpv_set_option_string(this->mpv, "vo", "libmpv");

    this->paused = false;
    const char* cmd[] = { "loadfile", url.c_str(), "replace", nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

void MpvPlayer::togglePause()
{
    if (!this->mpv)
        return;
    const char* cmd[] = { "cycle", "pause", nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

void MpvPlayer::seekRelative(int seconds)
{
    if (!this->mpv)
        return;
    std::string amount = std::to_string(seconds);
    const char* cmd[]  = { "seek", amount.c_str(), "relative", nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

void MpvPlayer::stop()
{
    if (!this->mpv)
        return;
    const char* cmd[] = { "stop", nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

std::vector<MpvPlayer::TrackInfo> MpvPlayer::getTracks(const std::string& type)
{
    std::vector<TrackInfo> tracks;
    if (!this->mpv)
        return tracks;

    mpv_node node;
    if (mpv_get_property(this->mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return tracks;

    if (node.format == MPV_FORMAT_NODE_ARRAY)
    {
        for (int i = 0; i < node.u.list->num; i++)
        {
            mpv_node& track = node.u.list->values[i];
            if (track.format != MPV_FORMAT_NODE_MAP)
                continue;

            TrackInfo info;
            bool matches        = false;
            mpv_node_list* map  = track.u.list;
            for (int j = 0; j < map->num; j++)
            {
                std::string key = map->keys[j];
                mpv_node& value = map->values[j];
                if (key == "type" && value.format == MPV_FORMAT_STRING)
                    matches = (type == value.u.string);
                else if (key == "id" && value.format == MPV_FORMAT_INT64)
                    info.id = value.u.int64;
                else if (key == "lang" && value.format == MPV_FORMAT_STRING)
                    info.lang = value.u.string;
                else if (key == "title" && value.format == MPV_FORMAT_STRING)
                    info.title = value.u.string;
                else if (key == "selected" && value.format == MPV_FORMAT_FLAG)
                    info.selected = value.u.flag != 0;
            }
            if (matches)
                tracks.push_back(info);
        }
    }

    mpv_free_node_contents(&node);
    return tracks;
}

int64_t MpvPlayer::getCurrentTrack(const std::string& type)
{
    if (!this->mpv)
        return 0;
    int64_t value = 0;
    mpv_get_property(this->mpv, type == "audio" ? "aid" : "sid", MPV_FORMAT_INT64, &value);
    return value;
}

void MpvPlayer::setTrack(const std::string& type, int64_t id)
{
    if (!this->mpv)
        return;
    const char* prop  = type == "audio" ? "aid" : "sid";
    std::string value = id <= 0 ? "no" : std::to_string(id);
    const char* cmd[] = { "set", prop, value.c_str(), nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

double MpvPlayer::getSpeed()
{
    if (!this->mpv)
        return 1.0;
    double speed = 1.0;
    mpv_get_property(this->mpv, "speed", MPV_FORMAT_DOUBLE, &speed);
    return speed;
}

void MpvPlayer::setSpeed(double value)
{
    if (!this->mpv)
        return;
    std::string s     = std::to_string(value);
    const char* cmd[] = { "set", "speed", s.c_str(), nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

double MpvPlayer::getSubDelay()
{
    if (!this->mpv)
        return 0.0;
    double delay = 0.0;
    mpv_get_property(this->mpv, "sub-delay", MPV_FORMAT_DOUBLE, &delay);
    return delay;
}

void MpvPlayer::setSubDelay(double value)
{
    if (!this->mpv)
        return;
    std::string s     = std::to_string(value);
    const char* cmd[] = { "set", "sub-delay", s.c_str(), nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

double MpvPlayer::getPosition()
{
    if (!this->mpv)
        return 0.0;
    double value = 0.0;
    mpv_get_property(this->mpv, "time-pos", MPV_FORMAT_DOUBLE, &value);
    return value;
}

double MpvPlayer::getDuration()
{
    if (!this->mpv)
        return 0.0;
    double value = 0.0;
    mpv_get_property(this->mpv, "duration", MPV_FORMAT_DOUBLE, &value);
    return value;
}

int MpvPlayer::getVolume()
{
    if (!this->mpv)
        return 100;
    int64_t value = 100;
    mpv_get_property(this->mpv, "volume", MPV_FORMAT_INT64, &value);
    return (int)value;
}

void MpvPlayer::setVolume(int value)
{
    if (!this->mpv)
        return;
    int64_t v         = value;
    mpv_set_property(this->mpv, "volume", MPV_FORMAT_INT64, &v);
}

void MpvPlayer::enableVO(bool enabled)
{
    if (!this->mpv)
        return;
    mpv_set_option_string(this->mpv, "vo", enabled ? "libmpv" : "null");
}

void MpvPlayer::seekAbsolutePercent(double percent)
{
    if (!this->mpv)
        return;
    std::string value = std::to_string(percent);
    const char* cmd[]  = { "seek", value.c_str(), "absolute-percent", nullptr };
    mpv_command_async(this->mpv, 0, cmd);
}

#if defined(BOREALIS_USE_DEKO3D)

void MpvPlayer::draw()
{
    if (!this->renderCtx)
        return;

    // Same idle-throttle reasoning as the desktop path below -- a
    // continuously-updating view has to keep marking itself active or
    // borealis' main loop can stall between input events.
    brls::Application::setActiveEvent(true);

    auto* videoContext = dynamic_cast<brls::SwitchVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    if (!videoContext)
        return;

    mpv_deko3d_fbo fbo {};
    fbo.tex         = videoContext->getFramebuffer();
    fbo.ready_fence = &this->readyFence;
    fbo.done_fence  = &this->doneFence;
    fbo.w           = (int)brls::Application::windowWidth;
    fbo.h           = (int)brls::Application::windowHeight;
    fbo.format      = DkImageFormat_RGBA8_Unorm;

    // Unlike the OpenGL path below, StreamNX-main's own deko3d render
    // params never include MPV_RENDER_PARAM_FLIP_Y -- deko3d's coordinate
    // convention doesn't need it the way GL's bottom-left origin does, and
    // the deko3d.patch backend (ra_dk.c) isn't written expecting it.
    mpv_render_param renderParams[] = {
        { MPV_RENDER_PARAM_DEKO3D_FBO, &fbo },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };

    // deko3d has no implicit ordering between separate command list
    // submissions the way a single GL context does -- these fences are
    // what tell the GPU "borealis is done writing this frame's target,
    // mpv's draw can start" (signal) and let borealis wait for mpv's own
    // draw to finish before it composites/presents that same image.
    videoContext->queueSignalFence(&this->readyFence);
    videoContext->queueFlush();

    int renderResult = mpv_render_context_render(this->renderCtx, renderParams);
    if (renderResult < 0 && renderResult != this->lastRenderError)
    {
        brls::Logger::error("MpvPlayer: render failed: {}", mpv_error_string(renderResult));
        this->lastRenderError = renderResult;
    }
    else if (renderResult >= 0)
    {
        this->lastRenderError = 0;
    }

    videoContext->queueWaitFence(&this->doneFence);

    mpv_render_context_report_swap(this->renderCtx);
}

#else

void MpvPlayer::draw()
{
    if (!this->renderCtx)
        return;

    // borealis throttles its own main loop down to a low idle framerate
    // (blocking on glfwWaitEventsTimeout) once nothing has marked itself
    // "active" for a while -- video playback never does that on its own,
    // so once the idle grace period lapses, the loop stalls and only
    // advances a single frame at a time, whenever a real input event
    // forces one through. Marking this active every frame is what a
    // continuously-updating view is supposed to do to keep the loop
    // running at full speed for as long as it's on screen.
    brls::Application::setActiveEvent(true);

    // Queried fresh every frame rather than cached once, so it's always
    // whatever's actually bound for the frame being drawn right now.
    GLint currentFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFbo);

    mpv_opengl_fbo fbo {};
    fbo.fbo = currentFbo;
    fbo.w   = (int)brls::Application::windowWidth;
    fbo.h   = (int)brls::Application::windowHeight;

    int flipY = 1;
    mpv_render_param renderParams[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flipY },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };

    // mpv's render API docs call out GL_SCISSOR_TEST as one of the few
    // bits of GL state it does NOT reset before drawing -- every screen in
    // this app clips scrolling content via nanovg's scissor, so whatever
    // the last screen (or widget) left the scissor rect set to could
    // otherwise clip the video draw away entirely.
    glDisable(GL_SCISSOR_TEST);

    // nanovg keeps its own VAO/VBO/EBO bound between draw calls; force a
    // known-clean vertex state before handing control to mpv so its
    // geometry can't end up using nanovg's stale bindings.
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    int renderResult = mpv_render_context_render(this->renderCtx, renderParams);
    if (renderResult < 0 && renderResult != this->lastRenderError)
    {
        // Logged only on change, not every frame -- a persistent failure
        // would otherwise flood the log at 60fps.
        brls::Logger::error("MpvPlayer: render failed: {}", mpv_error_string(renderResult));
        this->lastRenderError = renderResult;
    }
    else if (renderResult >= 0)
    {
        this->lastRenderError = 0;
    }

    // mpv leaves the GL framebuffer/viewport bindings it was given as-is;
    // put them back the way borealis expects them before it draws the rest
    // of the (non-video) UI on top this same frame.
    glBindFramebuffer(GL_FRAMEBUFFER, currentFbo);
    glViewport(0, 0, (GLsizei)brls::Application::windowWidth, (GLsizei)brls::Application::windowHeight);

    mpv_render_context_report_swap(this->renderCtx);
}

#endif

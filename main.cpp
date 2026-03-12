#define GLAD_GL_IMPLEMENTATION
#include <glad/gl43.h>

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_EXTERNAL_GL_LOADER
#include <sokol/sokol_gfx.h>
#include <sokol/util/sokol_gl.h>

#define RGFW_IMPLEMENTATION
#define RGFW_USE_XDL
#define RGFW_OPENGL
#include <RGFW/RGFW.h>

int main() {
    RGFW_glHints hints = {};
    hints.profile = RGFW_glCore;
    hints.major = 4;
    hints.minor = 3;
    RGFW_setGlobalHints_OpenGL(&hints);

    RGFW_window *window = RGFW_createWindow(
        "Kickstart", 0, 0, 800, 600, RGFW_windowCenter | RGFW_windowOpenGL);

    RGFW_window_setExitKey(window, RGFW_escape);
    RGFW_window_makeCurrentContext_OpenGL(window);

    gladLoadGL(RGFW_getProcAddress_OpenGL);

    sg_desc gfx_desc = {};
    gfx_desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    gfx_desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    gfx_desc.environment.defaults.sample_count = 1;
    sg_setup(&gfx_desc);

    sgl_desc_t gl_desc = {};
    sgl_setup(&gl_desc);

    while (RGFW_window_shouldClose(window) == RGFW_FALSE) {
        RGFW_event event;
        while (RGFW_window_checkEvent(window, &event)) {
            if (event.type == RGFW_quit)
                break;
        }

        sgl_defaults();
        sgl_begin_triangles();
        sgl_v2f_c3f(0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
        sgl_v2f_c3f(-0.5f, -0.5f, 0.0f, 1.0f, 0.0f);
        sgl_v2f_c3f(0.5f, -0.5f, 0.0f, 0.0f, 1.0f);
        sgl_end();

        sg_pass pass = {};
        pass.swapchain.width = window->w;
        pass.swapchain.height = window->h;
        pass.swapchain.sample_count = 1;
        pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
        pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        pass.swapchain.gl.framebuffer = 0;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.1f, 0.1f, 0.1f, 1.0f};

        sg_begin_pass(&pass);
        sgl_draw();
        sg_end_pass();
        sg_commit();

        RGFW_window_swapBuffers_OpenGL(window);
    }

    sgl_shutdown();
    sg_shutdown();
    RGFW_window_close(window);
    return 0;
}

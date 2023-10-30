#pragma once
#include "renderer/vkcommon.hh"
#include "stdafx.hh"
#include <cstdint>
#include <memory>
#include <sys/types.h>


class Platform {
public:
    Platform(std::string name, uint32_t width, uint32_t height);
    void create_window();
    bool pump_messages();
    bool create_vulkan_surface(VKCommonParameters &params);

    #ifdef Q_PLATFORM_LINUX
    // LINUX WINDOWING
    Display* display;
    Window handle;
    Atom wm_delete_window;
    bool should_quit;
    void handle_x11_event(XEvent& event);

    #elif defined(Q_PLATFORM_WINDOWS)
    // WINDOWS WINDOWING SHIT
    #endif // end platform specific

    std::string name;
    uint32_t width;
    uint32_t height;
};

#include "application.hh"
#include "core/events.hh"
// #include "renderer/vulkan/renderer.hh"
#include "renderer/renderer_frontend.hh"
#include <chrono>


static Settings settings = {};

struct ApplicationState {
    uint32_t width = 0;
    uint32_t height = 0;
    bool is_running = false;
    bool is_suspended = true;
    bool initialized = false;
};

static ApplicationState app_state = {};

Application::Application(std::string name, uint32_t width, uint32_t height, std::string assetPath)
    : m_name(name), 
    m_assetPath(assetPath), 
    m_timer{} {
    settings = {};

    app_state.width = width;
    app_state.height = height;

    // TODO: set this to be configurable
    settings.enableValidation = true;
    settings.enableVsync = false; // disable vsync for higher fps

    // Startup subsystems
    /* TODO: Logging startup */
    InputHandler::Startup();

    app_state.is_running = true;
    app_state.is_suspended = false;

    if (!EventHandler::Startup()) {
        std::cout << "Error: failed to initialize event handler" << std::endl;
        return;
    }
    std::cout << "Event System created..." << std::endl;

    // Register for events
    EventHandler::Register(EVENT_CODE_APPLICATION_QUIT, nullptr, [&, this](uint16_t code, void* sender, void* listener, EventContext data) -> bool {
            this->OnEvent(code, sender, listener, data);
            return true;});

    EventHandler::Register(EVENT_CODE_KEY_PRESSED, nullptr, [&, this](uint16_t code, void* sender, void* listener, EventContext data) -> bool {
        this->OnKey(code, sender, listener, data);
        return true;});
    
    EventHandler::Register(EVENT_CODE_RESIZED, nullptr, [&, this](uint16_t code, void* sender, void* listener, EventContext data) -> bool {
        this->OnResize(code, sender, listener, data);
        return true;});

    EventHandler::Register(EVENT_CODE_MOUSE_MOVED, nullptr, [&, this](uint16_t code, void* sender, void* listener, EventContext data) -> bool {
        this->OnMouseMove(code, sender, listener, data);
        return true;});

    EventHandler::Register(EVENT_CODE_KEY_RELEASED, nullptr, [&, this](uint16_t code, void* sender, void* listener, EventContext data) -> bool {
        this->OnKey(code, sender, listener, data);
        return true;});

    // Init the platform
    if (!Platform::Startup(name, width, height)) {
        std::cout << "Error: failed to initialize Platform Layer" << std::endl;
        exit(1);
    }
    std::cout << "Platform created" << std::endl;

    if (!Renderer::Initialize(name, assetPath, width, height, {true, false})) {
        std::cout << "Error: failed to initialize Renderer Subsystem" << std::endl;
        exit(1);
    }
    std::cout << "Renderer created" << std::endl;

    // m_renderer.OnInit();

    app_state.initialized = true;
}

Application::~Application() {
    
}

// Event loop of the application
bool
Application::run() {
    // Application Event loop
    while (app_state.is_running) {
        if (!Platform::pump_messages())
            app_state.is_running = false;

        if (!app_state.is_suspended) {
            // Update timer
            m_timer.Tick(nullptr);

            // Update FPS and framecount
            snprintf(m_lastFPS, static_cast<size_t>(32), "%u fps", m_timer.GetFPS());
            m_framecounter++;

            // Render a frame
            render_packet packet = {};
            Renderer::DrawFrame(packet);
        }
        
        if (m_framecounter % 300 == 0) {
            Platform::set_title(
                m_name + " - " + std::string(m_lastFPS)
            );
        }
    }

    std::cout << "Shutting down application" << std::endl;

    EventHandler::Unregister(EVENT_CODE_APPLICATION_QUIT, nullptr);
    EventHandler::Unregister(EVENT_CODE_KEY_PRESSED, nullptr);
    EventHandler::Unregister(EVENT_CODE_KEY_RELEASED, nullptr);
    EventHandler::Unregister(EVENT_CODE_RESIZED, nullptr);

    EventHandler::Shutdown();
    InputHandler::Shutdown();
    Renderer::Shutdown();
    Platform::Shutdown();
    std::cout << "Application shutdown successfully" << std::endl;
    return true; 
}

/////////////////////////////////////
// ---------- CALLBACKS ---------- //
/////////////////////////////////////

// Behavior for events
// FOR NOW: handle application shutdown signal
bool 
Application::OnEvent(uint16_t code, void* sender, void* listener, EventContext context) {
    (void)context;
    (void)listener;
    (void)sender;
    switch(code) {
        case EVENT_CODE_APPLICATION_QUIT: {
            std::cout << "EVENT_CODE_APPLICATION_QUIT received. Shutting down..." << std::endl;
            app_state.is_running = false;
            return true;
        }; break;
        default:
            break;
    }

    return false;
}

// TODO: register an event for when the mouse moves
//       print out the location of the mouse
bool 
Application::OnMouseMove(uint16_t code, void* sender, void* listener, EventContext context) {
    (void)context;
    (void)listener;
    (void)sender;
    switch(code) {
        case EVENT_CODE_MOUSE_MOVED: {
            printf("X: %i, Y: %i\n", context.u16[0], context.u16[1]);
            return true;
        }; break;
        default:
            break;
    }

    return false;
}

// Resize callback for resizing the window
bool 
Application::OnResize(uint16_t code, void* sender, void* listener, EventContext context) {
    (void)code;
    (void)context;
    (void)listener;
    (void)sender;
   
    if (code == EVENT_CODE_RESIZED) {
        uint32_t w = context.u32[0];
        uint32_t h = context.u32[1];

        // Check if either the width or height are different
        if (app_state.width != w || app_state.height != h) {
            printf("[%i, %i] != [%i, %i]\n", app_state.width, app_state.height, w, h);
            app_state.width = w;
            app_state.height = h;

            // Handle minimization
            if (w == 0 || h == 0) {
                std::cout << "Application minimized" << std::endl;
                // TODO: add suspended states to the application 
                app_state.is_suspended = true;
                return true;
            } else {
                // TODO: check if app is suspended and only act if not
                if (app_state.is_suspended) {
                    std::cout << "Window restored. Resuming application" << std::endl;
                    app_state.is_suspended = false;
                }
                Renderer::OnResize(w, h);
            }
        }
    }

    return false;
}

// Behavior for keyboard press events
bool
Application::OnKey(uint16_t code, void* sender, void* listener, EventContext context) {
    (void)context;
    (void)listener;
    (void)sender;
    if (code == EVENT_CODE_KEY_PRESSED) {
        uint16_t keycode = context.u16[0];
        if (keycode == KEY_ESCAPE) {
            EventContext data = {};
            EventHandler::Fire(EVENT_CODE_APPLICATION_QUIT, 0, data);

            return true;
        } else {
            printf("'%c' key pressed in window\n", static_cast<char>(keycode));
        }
    } else if (code == EVENT_CODE_KEY_RELEASED) {
        uint16_t keycode = context.u16[0];
        printf("'%c' key released in window\n", keycode);
    }

    return false;
}

#include "platform.hh"

#ifdef Q_PLATFORM_WINDOWS

Platform::Platform(std::string name, uint32_t width, uint32_t height, EventHandler &eh) 
	: name{name} , width{width} , height{height} , m_inputHandler{eh} {

	hInstance = GetModuleHandle(0);

	// Setup and register window class
	HICON icon = LoadIcon(hInstance, IDI_APPLICATION);
	WNDCLASSA wc { 0 };
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = Platform::WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = icon;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = "Pegasus Window Class";

	if (!RegisterClassA(&wc)) {
		MessageBoxA(
			0,
			"Window registration failed",
			"Error!",
			MB_ICONEXCLAMATION | MB_OK
		);
		return;
	}

}

std::chrono::time_point<std::chrono::high_resolution_clock>
Platform::get_current_time() {
	return std::chrono::high_resolution_clock::now();
}

void
Platform::set_title(std::string title) {

}

void
Platform::create_window() {
	// Create the window
	uint32_t client_x = 0;
	uint32_t client_y = 0;
	uint32_t client_width = width;
	uint32_t client_height = height;

	uint32_t window_x = client_x;
	uint32_t window_y = client_y;
  	uint32_t window_width = client_width;
  	uint32_t window_height = client_height;

	uint32_t window_style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION;
	uint32_t window_ex_style = WS_EX_APPWINDOW;

	window_style |= WS_MAXIMIZEBOX;
	window_style |= WS_MINIMIZEBOX;
	window_style |= WS_THICKFRAME;

	// Get size of the border
	RECT border_rect = {0,0,0,0};
	AdjustWindowRectEx(&border_rect, window_style, FALSE, window_ex_style);

	window_x += border_rect.left;
	window_y += border_rect.top;

	// Grow by the size of the OS border
	window_width += border_rect.right - border_rect.left;
	window_height += border_rect.bottom - border_rect.top;

	// Create the window
	hWindow = CreateWindowExA(
		window_ex_style,
		"Pegasus Window Class",
		name.c_str(),
		window_style,
		window_x,
		window_y,
		window_width,
		window_height,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	if (hWindow == nullptr) {
		MessageBoxA(NULL, "Window creation failed", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return;
	}

	// Show the window
	bool should_activate = true;
	int32_t show_window_command_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVATE;

	ShowWindow(hWindow, show_window_command_flags);

}

void
Platform::destroy_window() {

}

bool
Platform::pump_messages() {
	MSG message;

	// takes messages from the queue and pumps it to the application
	while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}

	return false;
}

bool
Platform::create_vulkan_surface(VKCommonParameters &params) {
		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.hinstance = hInstance;
		surfaceCreateInfo.hwnd = hWindow;
		
		VkResult err = vkCreateWin32SurfaceKHR(
			params.Instance, &surfaceCreateInfo, params.Allocator, &params.PresentationSurface);
		
		return (err == VK_SUCCESS) ? true : false;
}

LRESULT CALLBACK 
Platform::WindowProc(
	HWND hWnd,
	uint32_t code,
	WPARAM w_param,
	LPARAM l_param
) {
	switch (code) {
		case WM_ERASEBKGND:
			return 1;

		case WM_CLOSE:
			// Fire an event for the application to quit
			return 0;

		case WM_CREATE: 
		{
			
		}
		return 0;

		case WM_PAINT:
			break;
		case WM_KEYDOWN:
			break;
		case WM_KEYUP:
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_MOUSEMOVE:
			// Fire an event for mouse movement
			break;
		case WM_MOUSEWHEEL:
			// Fire an event for mouse movement
			break;

		case WM_LBUTTONUP:
		case WM_LBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDOWN: {

		} break;

		case WM_SIZE:
			break;
			
		case WM_GETMINMAXINFO:
			break;
		
		case WM_ENTERSIZEMOVE:
        	return 0;

        case WM_EXITSIZEMOVE:
        	return 0;
			
	}
	
	return DefWindowProc(hWnd, code, w_param, l_param);
}
#endif /* Q_PLATFORM_WINDOWS */
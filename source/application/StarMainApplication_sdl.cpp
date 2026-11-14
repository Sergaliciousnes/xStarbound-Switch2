#include "StarSwitch2WaitProfile.hpp"
#include "StarSwitch2IoProfile.hpp"
#include "StarSwitch2AllocationProfile.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#include "StarSwitch2SimulationProfile.hpp"
#ifdef STAR_SWITCH2_GAME_RPMALLOC
#include "StarSwitch2GameAllocator.hpp"
#endif
#include "StarMainApplication.hpp"
#include "StarSwitch2FrameProfile.hpp"
#include "StarLogging.hpp"
#include "StarSignalHandler.hpp"
#include "StarTickRateMonitor.hpp"
#include "StarSdlRenderBackend.hpp"
#ifdef STAR_ENABLE_VULKAN
#include "StarVulkanDiagnostic.hpp"
#endif
#include "StarTtlCache.hpp"
#include "StarImage.hpp"
#include "StarImageProcessing.hpp"
#include "StarSwitch2Diagnostics.hpp"

#include "SDL.h"
#include "StarPlatformServices_pc.hpp"

#ifdef __SWITCH2__
#include "StarSwitch2Controls.hpp"
#include "StarSwitch2Pointer.hpp"
#include "StarSwitch2SocketSession.hpp"
#include <switch.h>
#include <cstdio>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

// Necessary includes for Windows stuff.
#ifdef STAR_SYSTEM_WINDOWS
#include "SDL2/SDL_syswm.h"
#include <dwmapi.h>
#endif

// Include Tracy here to measure frame times.
#if defined TRACY_ENABLE
  #include "tracy/Tracy.hpp"
#endif

namespace Star {

#ifdef __SWITCH2__
// VI scales the buffer to the display. Keep the client buffer and UI at the
// same 16:9 size instead of applying desktop windowed resolutions.
#ifndef STAR_SWITCH2_RENDER_HEIGHT
#define STAR_SWITCH2_RENDER_HEIGHT 720
#endif
Vec2U const Switch2RenderSize{STAR_SWITCH2_RENDER_HEIGHT * 16 / 9, STAR_SWITCH2_RENDER_HEIGHT};
#endif

Maybe<Key> keyFromSdlKeyCode(SDL_Keycode sym) {
  static HashMap<int, Key> KeyCodeMap{
    {SDLK_BACKSPACE, Key::Backspace},
    {SDLK_TAB, Key::Tab},
    {SDLK_CLEAR, Key::Clear},
    {SDLK_RETURN, Key::Return},
    {SDLK_PAUSE, Key::Pause},
    {SDLK_ESCAPE, Key::Escape},
    {SDLK_SPACE, Key::Space},
    {SDLK_EXCLAIM, Key::Exclaim},
    {SDLK_QUOTEDBL, Key::QuotedBL},
    {SDLK_HASH, Key::Hash},
    {SDLK_DOLLAR, Key::Dollar},
    {SDLK_AMPERSAND, Key::Ampersand},
    {SDLK_QUOTE, Key::Quote},
    {SDLK_LEFTPAREN, Key::LeftParen},
    {SDLK_RIGHTPAREN, Key::RightParen},
    {SDLK_ASTERISK, Key::Asterisk},
    {SDLK_PLUS, Key::Plus},
    {SDLK_COMMA, Key::Comma},
    {SDLK_MINUS, Key::Minus},
    {SDLK_PERIOD, Key::Period},
    {SDLK_SLASH, Key::Slash},
    {SDLK_0, Key::Zero},
    {SDLK_1, Key::One},
    {SDLK_2, Key::Two},
    {SDLK_3, Key::Three},
    {SDLK_4, Key::Four},
    {SDLK_5, Key::Five},
    {SDLK_6, Key::Six},
    {SDLK_7, Key::Seven},
    {SDLK_8, Key::Eight},
    {SDLK_9, Key::Nine},
    {SDLK_COLON, Key::Colon},
    {SDLK_SEMICOLON, Key::Semicolon},
    {SDLK_LESS, Key::Less},
    {SDLK_EQUALS, Key::Equals},
    {SDLK_GREATER, Key::Greater},
    {SDLK_QUESTION, Key::Question},
    {SDLK_AT, Key::At},
    {SDLK_LEFTBRACKET, Key::LeftBracket},
    {SDLK_BACKSLASH, Key::Backslash},
    {SDLK_RIGHTBRACKET, Key::RightBracket},
    {SDLK_CARET, Key::Caret},
    {SDLK_UNDERSCORE, Key::Underscore},
    {SDLK_BACKQUOTE, Key::Backquote},
    {SDLK_a, Key::A},
    {SDLK_b, Key::B},
    {SDLK_c, Key::C},
    {SDLK_d, Key::D},
    {SDLK_e, Key::E},
    {SDLK_f, Key::F},
    {SDLK_g, Key::G},
    {SDLK_h, Key::H},
    {SDLK_i, Key::I},
    {SDLK_j, Key::J},
    {SDLK_k, Key::K},
    {SDLK_l, Key::L},
    {SDLK_m, Key::M},
    {SDLK_n, Key::N},
    {SDLK_o, Key::O},
    {SDLK_p, Key::P},
    {SDLK_q, Key::Q},
    {SDLK_r, Key::R},
    {SDLK_s, Key::S},
    {SDLK_t, Key::T},
    {SDLK_u, Key::U},
    {SDLK_v, Key::V},
    {SDLK_w, Key::W},
    {SDLK_x, Key::X},
    {SDLK_y, Key::Y},
    {SDLK_z, Key::Z},
    {SDLK_DELETE, Key::Delete},
    {SDLK_KP_0, Key::Kp0},
    {SDLK_KP_1, Key::Kp1},
    {SDLK_KP_2, Key::Kp2},
    {SDLK_KP_3, Key::Kp3},
    {SDLK_KP_4, Key::Kp4},
    {SDLK_KP_5, Key::Kp5},
    {SDLK_KP_6, Key::Kp6},
    {SDLK_KP_7, Key::Kp7},
    {SDLK_KP_8, Key::Kp8},
    {SDLK_KP_9, Key::Kp9},
    {SDLK_KP_PERIOD, Key::Kp_period},
    {SDLK_KP_DIVIDE, Key::Kp_divide},
    {SDLK_KP_MULTIPLY, Key::Kp_multiply},
    {SDLK_KP_MINUS, Key::Kp_minus},
    {SDLK_KP_PLUS, Key::Kp_plus},
    {SDLK_KP_ENTER, Key::Kp_enter},
    {SDLK_KP_EQUALS, Key::Kp_equals},
    {SDLK_UP, Key::Up},
    {SDLK_DOWN, Key::Down},
    {SDLK_RIGHT, Key::Right},
    {SDLK_LEFT, Key::Left},
    {SDLK_INSERT, Key::Insert},
    {SDLK_HOME, Key::Home},
    {SDLK_END, Key::End},
    {SDLK_PAGEUP, Key::PageUp},
    {SDLK_PAGEDOWN, Key::PageDown},
    {SDLK_F1, Key::F1},
    {SDLK_F2, Key::F2},
    {SDLK_F3, Key::F3},
    {SDLK_F4, Key::F4},
    {SDLK_F5, Key::F5},
    {SDLK_F6, Key::F6},
    {SDLK_F7, Key::F7},
    {SDLK_F8, Key::F8},
    {SDLK_F9, Key::F9},
    {SDLK_F10, Key::F10},
    {SDLK_F11, Key::F11},
    {SDLK_F12, Key::F12},
    {SDLK_F13, Key::F13},
    {SDLK_F14, Key::F14},
    {SDLK_F15, Key::F15},
    {SDLK_NUMLOCKCLEAR, Key::NumLock},
    {SDLK_CAPSLOCK, Key::CapsLock},
    {SDLK_SCROLLLOCK, Key::ScrollLock},
    {SDLK_RSHIFT, Key::RShift},
    {SDLK_LSHIFT, Key::LShift},
    {SDLK_RCTRL, Key::RCtrl},
    {SDLK_LCTRL, Key::LCtrl},
    {SDLK_RALT, Key::RAlt},
    {SDLK_LALT, Key::LAlt},
    {SDLK_RGUI, Key::RGui},
    {SDLK_LGUI, Key::LGui},
    {SDLK_MODE, Key::AltGr},
    {SDLK_APPLICATION, Key::Compose},
    {SDLK_HELP, Key::Help},
    {SDLK_PRINTSCREEN, Key::PrintScreen},
    {SDLK_SYSREQ, Key::SysReq},
    {SDLK_PAUSE, Key::Pause},
    {SDLK_MENU, Key::Menu},
    {SDLK_POWER, Key::Power}
  };

  return KeyCodeMap.maybe(sym);
}

KeyMod keyModsFromSdlKeyMods(uint16_t mod) {
  return static_cast<KeyMod>(mod);
}

MouseButton mouseButtonFromSdlMouseButton(uint8_t button) {
  switch (button) {
    case SDL_BUTTON_LEFT: return MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT: return MouseButton::Right;
    case SDL_BUTTON_X1: return MouseButton::FourthButton;
    default: return MouseButton::FifthButton;
  }
}

ControllerAxis controllerAxisFromSdlControllerAxis(uint8_t axis) {
  switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX: return ControllerAxis::LeftX;
    case SDL_CONTROLLER_AXIS_LEFTY: return ControllerAxis::LeftY;
    case SDL_CONTROLLER_AXIS_RIGHTX: return ControllerAxis::RightX;
    case SDL_CONTROLLER_AXIS_RIGHTY: return ControllerAxis::RightY;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return ControllerAxis::TriggerLeft;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return ControllerAxis::TriggerRight;
    default: return ControllerAxis::Invalid;
  }
}

ControllerButton controllerButtonFromSdlControllerButton(uint8_t button) {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return ControllerButton::A;
    case SDL_CONTROLLER_BUTTON_B: return ControllerButton::B;
    case SDL_CONTROLLER_BUTTON_X: return ControllerButton::X;
    case SDL_CONTROLLER_BUTTON_Y: return ControllerButton::Y;
    case SDL_CONTROLLER_BUTTON_BACK: return ControllerButton::Back;
    case SDL_CONTROLLER_BUTTON_GUIDE: return ControllerButton::Guide;
    case SDL_CONTROLLER_BUTTON_START: return ControllerButton::Start;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return ControllerButton::LeftStick;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return ControllerButton::RightStick;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ControllerButton::LeftShoulder;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ControllerButton::RightShoulder;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return ControllerButton::DPadUp;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ControllerButton::DPadDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ControllerButton::DPadLeft;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ControllerButton::DPadRight;
/* FezzedOne: This #if is here because the macOS SDL headers are outdated. */
#if SDL_VERSION_ATLEAST(2, 0, 14)
    case SDL_CONTROLLER_BUTTON_MISC1: return ControllerButton::Misc1;
    case SDL_CONTROLLER_BUTTON_PADDLE1: return ControllerButton::Paddle1;
    case SDL_CONTROLLER_BUTTON_PADDLE2: return ControllerButton::Paddle2;
    case SDL_CONTROLLER_BUTTON_PADDLE3: return ControllerButton::Paddle3;
    case SDL_CONTROLLER_BUTTON_PADDLE4: return ControllerButton::Paddle4;
    case SDL_CONTROLLER_BUTTON_TOUCHPAD: return ControllerButton::Touchpad;
#else
    case SDL_CONTROLLER_BUTTON_MAX: return ControllerButton::Misc1;
#endif
    default: return ControllerButton::Invalid;
  }
}

class SdlPlatform {
public:
  SdlPlatform(ApplicationUPtr application, StringList cmdLineArgs, bool vulkanMenus, bool vulkanGameplay) {
    m_application = std::move(application);

    // extract application path from command line args
    String applicationPath = cmdLineArgs.first();
    cmdLineArgs = cmdLineArgs.slice(1);

    StringList platformArguments;
    eraseWhere(cmdLineArgs, [&platformArguments](String& argument) {
        if (argument.beginsWith("+platform")) {
          platformArguments.append(std::move(argument));
          return true;
        }
        return false;
      });

    Logger::info("Application: Initializing SDL");
    if (SDL_Init(0))
      throw ApplicationException(strf("Couldn't initialize SDL: {}", SDL_GetError()));

#ifndef __SWITCH2__
    if (char* basePath = SDL_GetBasePath()) {
      File::changeDirectory(basePath);
      SDL_free(basePath);
    }
#endif

    m_signalHandler.setHandleInterrupt(true);
    m_signalHandler.setHandleFatal(true);

    try {
      Logger::info("Application: startup...");
      m_application->startup(cmdLineArgs);
    } catch (std::exception const& e) {
      throw ApplicationException("Application threw exception during startup", e);
    }

    Logger::info("Application: Initializing SDL Video");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
      throw ApplicationException(strf("Couldn't initialize SDL Video: {}", SDL_GetError()));

    Logger::info("Application: Initializing SDL Controller");
#ifdef __SWITCH2__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_switchPad);
    hidInitializeTouchScreen();
    Result pointerResult = m_switchPointer.initialize();
    if (R_SUCCEEDED(pointerResult))
      Logger::info("Switch 2 Joy-Con mouse: initialized (relative cursor movement)");
    else
      Logger::warn("Switch 2 Joy-Con mouse initialization failed: 0x{:08x}; stick and touch cursor remain available", pointerResult);
    // Native HID supplies controller/touch input. Do not also synthesize SDL
    // touch-mouse events or open all eight virtual controller slots.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    Logger::info("Switch 2 controls: left stick/D-pad WASD; right stick/touch cursor; ZR/tap left click; ZL right click; Y interact (E), hold >2s Left Alt; B ability (F); L/R hotbar; A jump; X Escape; Minus chat");
#else
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER))
      throw ApplicationException(strf("Couldn't initialize SDL Controller: {}", SDL_GetError()));
#endif

#ifdef STAR_SYSTEM_WINDOWS // Newer SDL is defaulting to xaudio2, which does not support audio capture
    SDL_setenv("SDL_AUDIODRIVER", "directsound", 1);
#endif

    Logger::info("Application: Initializing SDL Audio");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
      throw ApplicationException(strf("Couldn't initialize SDL Audio: {}", SDL_GetError()));

    Logger::info("Application: using Audio Driver '{}'", SDL_GetCurrentAudioDriver());

    SDL_JoystickEventState(SDL_ENABLE);

    m_platformServices = PcPlatformServices::create(applicationPath, platformArguments);
    if (!m_platformServices)
      Logger::info("Application: No platform services available");

    Logger::info("Application: Creating SDL Window");
    m_renderBackend = createSdlRenderBackend(vulkanMenus, vulkanGameplay);
#ifdef __SWITCH2__
    Uint32 windowFlags = m_renderBackend->windowFlags() | SDL_WINDOW_BORDERLESS;
#else
    Uint32 windowFlags = m_renderBackend->windowFlags() | SDL_WINDOW_RESIZABLE;
#endif
    m_sdlWindow = SDL_CreateWindow(m_windowTitle.utf8Ptr(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        m_windowSize[0], m_windowSize[1], windowFlags);
    if (!m_sdlWindow)
      throw ApplicationException::format("Application: Could not create SDL window: {}", SDL_GetError());

    SDL_ShowWindow(m_sdlWindow);
    SDL_RaiseWindow(m_sdlWindow);

    // Makes xStarbound respect Windows' dark mode setting (from OpenSB/Kae).
    // Code from: https://github.com/libsdl-org/SDL/commit/89948787#diff-f2ae5c36a8afc0a9a343a6664ab306da2963213e180af8cd97b12397dcbb9ae7R1478
#ifdef STAR_SYSTEM_WINDOWS
    if (void* handle = SDL_LoadObject("dwmapi.dll")) {
      if (auto DwmSetWindowAttributeFunc = (decltype(&DwmSetWindowAttribute))SDL_LoadFunction(handle, "DwmSetWindowAttribute")) {
        SDL_SysWMinfo wmInfo{};
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(m_sdlWindow, &wmInfo);
        DWORD type{}, value{}, count = sizeof(value);
        LSTATUS status = RegGetValue(HKEY_CURRENT_USER,
                             TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                             TEXT("AppsUseLightTheme"),
                             RRF_RT_REG_DWORD, &type, &value, &count);
        BOOL enabled = status == ERROR_SUCCESS && type == REG_DWORD && value == 0;
        // FezzedOne: The DWMWINDOWATTRIBUTE enum isn't available on MinGW, so the raw `int` (for DWMWA_USE_IMMERSIVE_DARK_MODE) is used here.
        // See: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
        DwmSetWindowAttributeFunc(wmInfo.info.win.window, 20, &enabled, sizeof(enabled));
      }
      SDL_UnloadObject(handle);
    }
#endif

    int width;
    int height;
    SDL_GetWindowSize(m_sdlWindow, &width, &height);
#ifdef __SWITCH2__
    m_windowSize = Switch2RenderSize;
    m_switchControls.setCursor(m_windowSize[0] * 0.5f, m_windowSize[1] * 0.5f);
    // Configure VI before Vulkan registers swapchain buffers. This operation
    // must not be used to resize an already-active swapchain.
    Result sizeResult = nwindowSetDimensions(nwindowGetDefault(), m_windowSize[0], m_windowSize[1]);
    if (R_FAILED(sizeResult))
      throw ApplicationException::format("Could not configure VI render size: 0x{:08x}", sizeResult);
    Logger::info("Switch 2: fixed render size {}x{} (SDL reported {}x{})", m_windowSize[0], m_windowSize[1], width, height);
#else
    m_windowSize = Vec2U(width, height);
#endif

    m_renderBackend->initialize(m_sdlWindow);
    m_renderBackend->present();
    setVSyncEnabled(m_windowVSync);

    SDL_StopTextInput();

    SDL_AudioSpec desired = {};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.samples = 1024;
    desired.channels = 2;
    desired.userdata = this;
    desired.callback = [](void* userdata, Uint8* stream, int len) {
      ((SdlPlatform*)(userdata))->getAudioData(stream, len);
    };

    SDL_AudioSpec obtained = {};
    m_sdlAudioOutputDevice = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (!m_sdlAudioOutputDevice) {
      Logger::error("Application: Could not open audio device, no sound available!");
    } else if (obtained.freq != desired.freq || obtained.channels != desired.channels || obtained.format != desired.format) {
      SDL_CloseAudioDevice(m_sdlAudioOutputDevice);
      Logger::error("Application: Could not open 44.1khz / 16 bit stereo audio device, no sound available!");
    } else {
      Logger::info("Application: Opened default audio device with 44.1khz / 16 bit stereo audio, {} sample size buffer", obtained.samples);
      SDL_PauseAudioDevice(m_sdlAudioOutputDevice, 0);
    }

    m_renderer = m_renderBackend->createRenderer();
    m_renderer->setScreenSize(m_windowSize);

    m_cursorCache.setTimeToLive(30000);
  }

  ~SdlPlatform() {

    if (m_sdlAudioOutputDevice)
      SDL_CloseAudioDevice(m_sdlAudioOutputDevice);

    closeAudioInputDevice();

    // Release application-owned textures and buffers while the context is live,
    // including when run() did not reach its normal cleanup path.
    m_application.reset();
    m_renderer.reset();
    m_renderBackend.reset();

    Logger::info("Application: Destroying SDL Window");
    SDL_DestroyWindow(m_sdlWindow);

#ifdef __SWITCH2__
    m_switchPointer.close();
#endif
    SDL_Quit();
  }

  bool openAudioInputDevice(const char* name, int freq, int channels, void* userdata, SDL_AudioCallback callback) {
    SDL_AudioSpec desired = {};
    desired.freq = freq;
    desired.format = AUDIO_S16SYS;
    desired.samples = 1024;
    desired.channels = channels;
    desired.userdata = userdata;
    desired.callback = callback;

    closeAudioInputDevice();

    SDL_AudioSpec obtained = {};
    m_sdlAudioInputDevice = SDL_OpenAudioDevice(name, 1, &desired, &obtained, 0);

    if (m_sdlAudioInputDevice) {
      if (name)
        Logger::info("Opened audio input device '{}'", name);
      else
        Logger::info("Opened default audio input device");
      SDL_PauseAudioDevice(m_sdlAudioInputDevice, 0);
    }
    else
      Logger::info("Failed to open audio input device: {}", SDL_GetError());

    return m_sdlAudioInputDevice != 0;
  }

  bool closeAudioInputDevice() {
    if (m_sdlAudioInputDevice) {
      Logger::info("Closing audio input device");
      SDL_CloseAudioDevice(m_sdlAudioInputDevice);
      m_sdlAudioInputDevice = 0;
      return true;
    }
    return false;
  }

  void cleanup() {
    m_cursorCache.ptr(m_currentCursor);
    m_cursorCache.cleanup();
  }

  void run() {
    try {
      Logger::info("Application: initialization...");
      m_application->applicationInit(make_shared<Controller>(this));

      Logger::info("Application: renderer initialization...");
      m_application->renderInit(m_renderer);

      Logger::info("Application: main update loop...");

      m_updateTicker.reset();
      m_renderTicker.reset();
#ifdef STAR_SYSTEM_SWITCH2
      Switch2FrameProfile::meter.reset(Switch2FrameProfile::nowUs());
#endif

#ifdef STAR_SWITCH2_WAIT_PROFILE
      Switch2WaitProfile::start();
      Switch2SimulationProfile::start();
      Logger::info("Switch 2 wait profile: buffered in RAM; detailed frames 1/8; writes sdmc:/xstarbound-waits.log on normal exit");
#ifdef STAR_SWITCH2_IO_PROFILE
      Logger::info("Switch 2 I/O profile: all-thread File counts/times, script and block cache counters; buffered until exit; sdmc:/xstarbound-io.log");
#endif
#endif
      bool quit = false;
      while (true) {
        Switch2WaitProfile::Frame waitFrame;
        Switch2WaitProfile::Scope eventScope(Switch2WaitProfile::Metric::Events);
#ifdef STAR_SYSTEM_SWITCH2
        Switch2FrameProfile::begin();
#endif
#ifdef __SWITCH2__
        if (!appletMainLoop()) break;
#endif
#if defined TRACY_ENABLE
        FrameMarkStart("SDL cursor cache cleanup");
#endif
        cleanup();
#if defined TRACY_ENABLE
        FrameMarkEnd("SDL cursor cache cleanup");
#endif

#if defined TRACY_ENABLE
        FrameMarkStart("Input processing");
#endif
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "input");
        for (auto const& event : processEvents())
          m_application->processInput(event);
#if defined TRACY_ENABLE
        FrameMarkEnd("Input processing");
#endif

#if defined TRACY_ENABLE
        FrameMarkStart("Steam/Discord API update");
#endif
        if (m_platformServices)
          m_platformServices->update();

        if (m_platformServices->overlayActive())
          SDL_ShowCursor(1);
        else
          SDL_ShowCursor(m_cursorVisible ? 1 : 0);
#if defined TRACY_ENABLE
        FrameMarkEnd("Steam/Discord API update");
#endif

#if defined TRACY_ENABLE
        FrameMarkStart("Game update loops");
#endif
        eventScope.stop();
        int updatesBehind = max<int>(round(m_updateTicker.ticksBehind()), 1);
        updatesBehind = min<int>(updatesBehind, m_maxFrameSkip + 1);
        for (int i = 0; i < updatesBehind; ++i) {
          Switch2FrameProfile::Zone updateZone(Switch2FrameProfile::Phase::Update);
          Switch2FrameProfile::count(Switch2FrameProfile::Counter::Updates, 1);
          Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "application update");
          Switch2WaitProfile::Scope updateWaits(Switch2WaitProfile::Metric::Update);
          m_application->update();
          m_updateRate = m_updateTicker.tick();
        }
#if defined TRACY_ENABLE
        FrameMarkEnd("Game update loops");
#endif

#if defined TRACY_ENABLE
        FrameMarkStart("Rendering");
#endif
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "start frame");
        m_renderer->startFrame();
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "application render");
        {
          Switch2WaitProfile::Scope renderWaits(Switch2WaitProfile::Metric::Render);
          m_application->render();
        }
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "finish frame");
        {
          Switch2FrameProfile::Zone finishZone(Switch2FrameProfile::Phase::Finish);
          Switch2WaitProfile::Scope finishWaits(Switch2WaitProfile::Metric::Finish);
          m_renderer->finishFrame();
        }
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "present");
        {
          Switch2WaitProfile::Scope presentWaits(Switch2WaitProfile::Metric::Present);
          m_renderBackend->present();
        }
        Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Main, "frame complete / idle");
#ifdef STAR_SYSTEM_SWITCH2
        Switch2FrameProfile::complete();
        m_renderRate = Switch2FrameProfile::meter.fps();
#else
        m_renderRate = m_renderTicker.tick();
#endif
#if defined TRACY_ENABLE
        FrameMarkEnd("Rendering");
        FrameMark;
#endif

        if (m_quitRequested) {
          Logger::info("Application: quit requested");
          quit = true;
        }

        if (m_signalHandler.interruptCaught()) {
          Logger::info("Application: Interrupt caught");
          quit = true;
        }

        if (quit) {
          Logger::info("Application: quitting...");
          break;
        }

#if defined TRACY_ENABLE
        FrameMarkStart("Free wait time");
#endif
        int64_t spareMilliseconds = round(m_updateTicker.spareTime() * 1000);
        if (spareMilliseconds > 0) {
          Switch2WaitProfile::Scope pacingWait(Switch2WaitProfile::Metric::Pacing);
          Thread::sleepPrecise(spareMilliseconds);
        }
#if defined TRACY_ENABLE
        FrameMarkEnd("Free wait time");
#endif
      }
    } catch (std::exception const& e) {
      Logger::error("Application: exception thrown, shutting down: {}", outputException(e, true));
    }

    Switch2WaitProfile::finishFrames();
#ifdef STAR_SWITCH2_EXPLORATION_PROFILE
    if(Switch2SimulationProfile::dump("sdmc:/xstarbound-simulation.log"))
      Logger::info("Switch 2 simulation profile: saved sdmc:/xstarbound-simulation.log");
    else if(Switch2SimulationProfile::dump("sdmc:/switch/xstarbound/storage/xstarbound-simulation.log"))
      Logger::info("Switch 2 simulation profile: saved storage fallback");
    else Logger::warn("Switch 2 simulation profile: writes failed, errno {}",errno);
    if(Switch2ExplorationProfile::dump("sdmc:/xstarbound-exploration.log"))
      Logger::info("Switch 2 exploration profile: saved sdmc:/xstarbound-exploration.log");
    else if(Switch2ExplorationProfile::dump("sdmc:/switch/xstarbound/storage/xstarbound-exploration.log"))
      Logger::info("Switch 2 exploration profile: saved storage fallback");
    else Logger::warn("Switch 2 exploration profile: writes failed, errno {}",errno);
#endif
#ifdef STAR_SWITCH2_RESOURCE_REPORTS
    Switch2GameAllocator::report(stdout,"before-teardown");
    Switch2GameAllocator::reportFile("before-teardown",true);
#endif
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
    if (Switch2AllocationProfile::dump("sdmc:/xstarbound-allocations.log")) {
      Logger::info("Switch 2 allocation callers: saved sdmc:/xstarbound-allocations.log");
    } else if (Switch2AllocationProfile::dump("sdmc:/switch/xstarbound/storage/xstarbound-allocations.log")) {
      Logger::info("Switch 2 allocation callers: saved storage fallback");
    } else {
      Logger::warn("Switch 2 allocation callers: report writes failed, errno {}",errno);
    }
#endif
#ifdef STAR_SWITCH2_IO_PROFILE
    Logger::info("Switch 2 I/O profile: saving buffered capture before teardown");
    if (Switch2IoProfile::dump("sdmc:/xstarbound-io.log")) {
      Logger::info("Switch 2 I/O profile: saved sdmc:/xstarbound-io.log");
    } else if (Switch2IoProfile::dump("sdmc:/switch/xstarbound/storage/xstarbound-io.log")) {
      Logger::info("Switch 2 I/O profile: saved storage fallback xstarbound-io.log");
    } else {
      Logger::warn("Switch 2 I/O profile: both writes failed, errno {} ({})", errno, std::strerror(errno));
    }
#endif
#ifdef STAR_SWITCH2_WAIT_PROFILE
    // Save while the SD mount and application are still alive. Root teardown
    // can stall or terminate before code after m_application.reset() is reached.
    Logger::info("Switch 2 wait profile: saving before application teardown");
    char const* reportPath = "sdmc:/xstarbound-waits.log";
    if (Switch2WaitProfile::dump(reportPath)) {
      Logger::info("Switch 2 wait profile: saved {}", reportPath);
    } else {
      int reportError = errno;
      Logger::warn("Switch 2 wait profile: could not write {}: errno {} ({})", reportPath, reportError, std::strerror(reportError));
      reportPath = "sdmc:/switch/xstarbound/storage/xstarbound-waits.log";
      if (Switch2WaitProfile::dump(reportPath)) {
        Logger::info("Switch 2 wait profile: saved fallback {}", reportPath);
      } else {
        reportError = errno;
        Logger::warn("Switch 2 wait profile: fallback write failed {}: errno {} ({})", reportPath, reportError, std::strerror(reportError));
      }
    }
#endif
    try {
      Logger::info("Application: shutdown...");
      m_application->shutdown();
    } catch (std::exception const& e) {
      Logger::error("Application: threw exception during shutdown: {}", outputException(e, true));
    }

    SDL_CloseAudioDevice(m_sdlAudioOutputDevice);
    m_SdlControllers.clear();

    SDL_SetCursor(NULL);
    m_cursorCache.clear();

    m_application.reset();
  }

private:
  struct Controller : public ApplicationController {
    Controller(SdlPlatform* parent)
      : parent(parent) {}

    Maybe<String> getClipboard() override {
      if (SDL_HasClipboardText()) {
        char* clipboardText = SDL_GetClipboardText();
        String clipboardTextStr = "";
        if (clipboardText != NULL && clipboardText[0] != '\0')
          clipboardTextStr = String(clipboardText);
        SDL_free(clipboardText);
        return Maybe<String>(clipboardTextStr);
      }
      return {};
    }

    void setClipboard(String text) override {
      SDL_SetClipboardText(text.utf8Ptr());
    }

    Maybe<String> maybeSetClipboard(String text) override {
      int check = SDL_SetClipboardText(text.utf8Ptr());
      if (check == 0) {
        return Maybe<String>{};
      } else {
        return String(SDL_GetError());
      }
    }

    void setTargetUpdateRate(float targetUpdateRate) override {
      parent->m_updateTicker.setTargetTickRate(targetUpdateRate);
    }

    void setUpdateTrackWindow(float updateTrackWindow) override {
      parent->m_updateTicker.setWindow(updateTrackWindow);
    }

    void setApplicationTitle(String title) override {
      parent->m_windowTitle = std::move(title);
      if (parent->m_sdlWindow)
        SDL_SetWindowTitle(parent->m_sdlWindow, parent->m_windowTitle.utf8Ptr());
    }

    void setFullscreenWindow(Vec2U fullScreenResolution) override {
#ifdef __SWITCH2__
      setConsoleWindow();
      return;
#endif
      if (parent->m_windowMode != WindowMode::Fullscreen || parent->m_windowSize != fullScreenResolution) {
        SDL_DisplayMode requestedDisplayMode = {SDL_PIXELFORMAT_RGB888, (int)fullScreenResolution[0], (int)fullScreenResolution[1], 0, 0};
        int currentDisplayIndex = SDL_GetWindowDisplayIndex(parent->m_sdlWindow);

        SDL_DisplayMode targetDisplayMode;
        if (SDL_GetClosestDisplayMode(currentDisplayIndex, &requestedDisplayMode, &targetDisplayMode) != NULL) {
          if (SDL_SetWindowDisplayMode(parent->m_sdlWindow, &requestedDisplayMode) == 0) {
            if (parent->m_windowMode == WindowMode::Fullscreen)
              SDL_SetWindowFullscreen(parent->m_sdlWindow, 0);
            else if (parent->m_windowMode == WindowMode::Borderless)
              SDL_SetWindowBordered(parent->m_sdlWindow, SDL_TRUE);
            else if (parent->m_windowMode == WindowMode::Maximized)
              SDL_RestoreWindow(parent->m_sdlWindow);
            
            parent->m_windowMode = WindowMode::Fullscreen;
            SDL_SetWindowFullscreen(parent->m_sdlWindow, SDL_WINDOW_FULLSCREEN);
          } else {
            Logger::warn("Failed to set resolution {}, {}", (unsigned)requestedDisplayMode.w, (unsigned)requestedDisplayMode.h);
          }
        } else {
          Logger::warn("Unable to set requested display resolution {}, {}", (int)fullScreenResolution[0], (int)fullScreenResolution[1]);
        }

        SDL_DisplayMode actualDisplayMode;
        if (SDL_GetWindowDisplayMode(parent->m_sdlWindow, &actualDisplayMode) == 0) {
          parent->m_windowSize = {(unsigned)actualDisplayMode.w, (unsigned)actualDisplayMode.h};

          // call these manually since no SDL_WindowEvent is triggered when changing between fullscreen resolutions for some reason
          parent->m_renderer->setScreenSize(parent->m_windowSize);
          parent->m_application->windowChanged(parent->m_windowMode, parent->m_windowSize);
        } else {
          Logger::error("Couldn't get window display mode!");
        }
      }
    }

    void setNormalWindow(Vec2U windowSize) override {
#ifdef __SWITCH2__
      setConsoleWindow();
      return;
#endif
      auto window = parent->m_sdlWindow;
      if (parent->m_windowMode != WindowMode::Normal || parent->m_windowSize != windowSize) {
        if (parent->m_windowMode == WindowMode::Fullscreen)
          SDL_SetWindowFullscreen(window, 0);
        else if (parent->m_windowMode == WindowMode::Borderless)
          SDL_SetWindowBordered(window, SDL_TRUE);
        else if (parent->m_windowMode == WindowMode::Maximized)
          SDL_RestoreWindow(window);

        SDL_SetWindowBordered(window, SDL_TRUE);
        SDL_SetWindowSize(window, windowSize[0], windowSize[1]);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        parent->m_windowMode = WindowMode::Normal;
        parent->m_windowSize = windowSize;
      }
    }

    void setMaximizedWindow() override {
#ifdef __SWITCH2__
      setConsoleWindow();
      return;
#endif
      if (parent->m_windowMode != WindowMode::Maximized) {
        if (parent->m_windowMode == WindowMode::Fullscreen)
          SDL_SetWindowFullscreen(parent->m_sdlWindow, 0);
        else if (parent->m_windowMode == WindowMode::Borderless)
          SDL_SetWindowBordered(parent->m_sdlWindow, SDL_TRUE);

        SDL_RestoreWindow(parent->m_sdlWindow);
        SDL_MaximizeWindow(parent->m_sdlWindow);
        parent->m_windowMode = WindowMode::Maximized;
      }
    }

    void setBorderlessWindow() override {
#ifdef __SWITCH2__
      setConsoleWindow();
      return;
#endif
      if (parent->m_windowMode != WindowMode::Borderless) {
        if (parent->m_windowMode == WindowMode::Fullscreen)
          SDL_SetWindowFullscreen(parent->m_sdlWindow, 0);
        else if (parent->m_windowMode == WindowMode::Maximized)
          SDL_RestoreWindow(parent->m_sdlWindow);

        SDL_SetWindowBordered(parent->m_sdlWindow, SDL_FALSE);
        parent->m_windowMode = WindowMode::Borderless;

        SDL_DisplayMode actualDisplayMode;
        if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(parent->m_sdlWindow), &actualDisplayMode) == 0) {
          parent->m_windowSize = {(unsigned)actualDisplayMode.w, (unsigned)actualDisplayMode.h};

          SDL_SetWindowPosition(parent->m_sdlWindow, 0, 0);
          SDL_SetWindowSize(parent->m_sdlWindow, parent->m_windowSize[0], parent->m_windowSize[1]);
          parent->m_renderer->setScreenSize(parent->m_windowSize);
          parent->m_application->windowChanged(parent->m_windowMode, parent->m_windowSize);
        } else {
          Logger::error("Couldn't get desktop display mode!");
        }
      }
    }

    void setVSyncEnabled(bool vSync) override {
      if (parent->m_windowVSync != vSync) {
        parent->setVSyncEnabled(vSync);
        parent->m_windowVSync = vSync;
      }
    }

#ifdef __SWITCH2__
    void setConsoleWindow() {
      if (parent->m_windowMode == WindowMode::Borderless && parent->m_windowSize == Switch2RenderSize)
        return;
      parent->m_windowMode = WindowMode::Borderless;
      parent->m_windowSize = Switch2RenderSize;
      parent->m_renderer->setScreenSize(parent->m_windowSize);
      parent->m_application->windowChanged(parent->m_windowMode, parent->m_windowSize);
    }
#endif

    void setMaxFrameSkip(unsigned maxFrameSkip) override {
      parent->m_maxFrameSkip = maxFrameSkip;
    }

    void setCursorVisible(bool cursorVisible) override {
      parent->m_cursorVisible = cursorVisible;
    }

    bool setCursorImage(const String& id, const ImageConstPtr& image, unsigned scale, const Vec2I& offset) override {
      return parent->setCursorImage(id, image, scale, offset);
    }

    void setAcceptingTextInput(bool acceptingTextInput) override {
      if (acceptingTextInput != parent->m_acceptingTextInput) {
#ifndef __SWITCH2__
        if (acceptingTextInput)
          SDL_StartTextInput();
        else
          SDL_StopTextInput();
#endif

        parent->m_acceptingTextInput = acceptingTextInput;
      }
    }

#ifdef __SWITCH2__
    Maybe<String> editText(String const& initial, bool hidden) override {
      SwkbdConfig keyboard{};
      Result result = swkbdCreate(&keyboard, 0);
      if (R_FAILED(result)) {
        Logger::warn("Switch 2 keyboard creation failed: 0x{:08x}", result);
        return {};
      }
      struct CloseKeyboard { SwkbdConfig* config; ~CloseKeyboard(){if(config)swkbdClose(config);} } close{&keyboard};
      if(hidden)swkbdConfigMakePresetPassword(&keyboard);
      else swkbdConfigMakePresetDefault(&keyboard);
      swkbdConfigSetInitialText(&keyboard, initial.utf8Ptr());
      swkbdConfigSetStringLenMax(&keyboard, 500);
      char output[4096]{};
      Logger::info("Switch 2 keyboard: opening explicit text editor");
      result = swkbdShow(&keyboard, output, sizeof(output));
      // Close the applet's resources before returning to game rendering, also
      // on Cancel. Do not use SDL's automatic/inline keyboard path here.
      swkbdClose(&keyboard); close.config=nullptr;
      padUpdate(&parent->m_switchPad);
      parent->m_switchBlockedButtons = padGetButtons(&parent->m_switchPad);
      parent->m_switchControls.suspend();
      parent->m_switchPointer.suspend();
      parent->m_switchInputTicks=SDL_GetTicks();
      parent->m_switchFocused=appletGetFocusState()==AppletFocusState_InFocus;
      Logger::info("Switch 2 keyboard: closed, result 0x{:08x}", result);
      if (R_FAILED(result))return {};
      return String(output);
    }
#endif

    AudioFormat enableAudio() override {
      parent->m_audioEnabled = true;
      SDL_PauseAudio(false);
      return AudioFormat{44100, 2};
    }

    void disableAudio() override {
      parent->m_audioEnabled = false;
      SDL_PauseAudio(true);
    }

    bool openAudioInputDevice(const char* name, int freq, int channels, void* userdata, AudioCallback callback) override {
      return parent->openAudioInputDevice(name, freq, channels, userdata, callback);
    };

    bool closeAudioInputDevice() override {
      return parent->closeAudioInputDevice();
    };

    float updateRate() const override {
      return parent->m_updateRate;
    }

    float renderFps() const override {
      return parent->m_renderRate;
    }

    StatisticsServicePtr statisticsService() const override {
      if (parent->m_platformServices)
        return parent->m_platformServices->statisticsService();
      return {};
    }

    P2PNetworkingServicePtr p2pNetworkingService() const override {
      if (parent->m_platformServices)
        return parent->m_platformServices->p2pNetworkingService();
      return {};
    }

    UserGeneratedContentServicePtr userGeneratedContentService() const override {
      if (parent->m_platformServices)
        return parent->m_platformServices->userGeneratedContentService();
      return {};
    }

    DesktopServicePtr desktopService() const override {
      if (parent->m_platformServices)
        return parent->m_platformServices->desktopService();
      return {};
    }

    void quit() override {
      parent->m_quitRequested = true;
    }

    SdlPlatform* parent;
  };

  List<InputEvent> processEvents() {
    List<InputEvent> inputEvents;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      Maybe<InputEvent> starEvent;
      switch (event.type) {
      case SDL_WINDOWEVENT:
#ifdef __SWITCH2__
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          m_switchFocused = false;
          m_switchPointer.suspend();
        }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) m_switchFocused = true;
#endif
        if (event.window.event == SDL_WINDOWEVENT_MAXIMIZED || event.window.event == SDL_WINDOWEVENT_RESTORED) {
#ifdef __SWITCH2__
          m_windowMode = WindowMode::Borderless;
#else
          auto windowFlags = SDL_GetWindowFlags(m_sdlWindow);

          if (windowFlags & SDL_WINDOW_MAXIMIZED)
            m_windowMode = WindowMode::Maximized;
          else if (windowFlags & SDL_WINDOW_FULLSCREEN)
            m_windowMode = WindowMode::Fullscreen;
          else if (windowFlags & SDL_WINDOW_BORDERLESS)
            m_windowMode = WindowMode::Borderless;
          else
            m_windowMode = WindowMode::Normal;
#endif

          m_application->windowChanged(m_windowMode, m_windowSize);

        } else if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
#ifdef __SWITCH2__
          // Desktop-size notifications must not replace the console UI size.
          m_windowSize = Switch2RenderSize;
#else
          m_windowSize = Vec2U(event.window.data1, event.window.data2);
#endif
          m_renderer->setScreenSize(m_windowSize);
          m_application->windowChanged(m_windowMode, m_windowSize);
        }
        break;
      case SDL_KEYDOWN:
        if (!event.key.repeat) {
          if (auto key = keyFromSdlKeyCode(event.key.keysym.sym))
            starEvent.set(KeyDownEvent{*key, keyModsFromSdlKeyMods(event.key.keysym.mod)});
        }
        break;
      case SDL_KEYUP:
        if (auto key = keyFromSdlKeyCode(event.key.keysym.sym))
          starEvent.set(KeyUpEvent{*key});
        break;
      case SDL_TEXTINPUT:
        starEvent.set(TextInputEvent{String(event.text.text)});
        break;
      case SDL_MOUSEMOTION:
#ifdef __SWITCH2__
        if (event.motion.which == SDL_TOUCH_MOUSEID) break;
        m_switchControls.setCursor(event.motion.x, (int)m_windowSize[1] - event.motion.y);
#endif
        starEvent.set(MouseMoveEvent{
            {event.motion.xrel, -event.motion.yrel}, {event.motion.x, (int)m_windowSize[1] - event.motion.y}});
        break;
      case SDL_MOUSEBUTTONDOWN:
#ifdef __SWITCH2__
        if (event.button.which == SDL_TOUCH_MOUSEID) break;
#endif
        starEvent.set(MouseButtonDownEvent{mouseButtonFromSdlMouseButton(event.button.button),
            {event.button.x, (int)m_windowSize[1] - event.button.y}});
        break;
      case SDL_MOUSEBUTTONUP:
#ifdef __SWITCH2__
        if (event.button.which == SDL_TOUCH_MOUSEID) break;
#endif
        starEvent.set(MouseButtonUpEvent{mouseButtonFromSdlMouseButton(event.button.button),
            {event.button.x, (int)m_windowSize[1] - event.button.y}});
        break;
      case SDL_MOUSEWHEEL:
        int x, y;
        SDL_GetMouseState(&x, &y);
        starEvent.set(MouseWheelEvent{event.wheel.y < 0 ? MouseWheel::Down : MouseWheel::Up, {x, (int)m_windowSize[1] - y}});
        break;
#ifndef __SWITCH2__
      case SDL_CONTROLLERAXISMOTION:
        starEvent.set(ControllerAxisEvent{
          (ControllerId)event.caxis.which,
          controllerAxisFromSdlControllerAxis(event.caxis.axis),
          (float)event.caxis.value / 32768.0f
        });
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        starEvent.set(ControllerButtonDownEvent{ (ControllerId)event.cbutton.which, controllerButtonFromSdlControllerButton(event.cbutton.button) });
        break;
      case SDL_CONTROLLERBUTTONUP:
        starEvent.set(ControllerButtonUpEvent{ (ControllerId)event.cbutton.which, controllerButtonFromSdlControllerButton(event.cbutton.button) });
        break;
      case SDL_CONTROLLERDEVICEADDED:
        {
          auto insertion = m_SdlControllers.insert_or_assign(event.cdevice.which, SDLGameControllerUPtr(SDL_GameControllerOpen(event.cdevice.which), SDL_GameControllerClose));
          if (SDL_GameController* controller = insertion.first->second.get())
            Logger::info("Controller device '{}' added", SDL_GameControllerName(controller));
        }
        break;
      case SDL_CONTROLLERDEVICEREMOVED:
        {
          auto find = m_SdlControllers.find(event.cdevice.which);
          if (find != m_SdlControllers.end()) {
            if (SDL_GameController* controller = find->second.get())
              Logger::info("Controller device '{}' removed", SDL_GameControllerName(controller));
            m_SdlControllers.erase(event.cdevice.which);
          }
        }
        break;
#endif
      case SDL_QUIT:
        m_quitRequested = true;
        starEvent.reset();
        break;
      }

      if (starEvent)
        inputEvents.append(starEvent.take());
    }

#ifdef __SWITCH2__
    appendSwitch2Input(inputEvents);
#endif
    return inputEvents;
  }

#ifdef __SWITCH2__
  void appendSwitch2Input(List<InputEvent>& events) {
    padUpdate(&m_switchPad);
    auto rawButtons = padGetButtons(&m_switchPad);
    m_switchBlockedButtons &= rawButtons;
    auto buttons = rawButtons & ~m_switchBlockedButtons;
    auto left = padGetStickPos(&m_switchPad, 0);
    auto right = padGetStickPos(&m_switchPad, 1);
    Switch2Controls::Sample sample;
    sample.focused = m_switchFocused && appletGetFocusState() == AppletFocusState_InFocus;
    sample.connected = padIsConnected(&m_switchPad);
    m_switchPointer.update(m_switchPad, sample.focused && sample.connected);
    sample.pointerActive = m_switchPointer.active();
    sample.pointerX = static_cast<float>(m_switchPointer.deltaX());
    // Native pointer Y grows downwards; the game uses a bottom-left origin.
    sample.pointerY = -static_cast<float>(m_switchPointer.deltaY());
    sample.leftX = left.x / 32768.0f;
    sample.leftY = left.y / 32768.0f;
    sample.rightX = right.x / 32768.0f;
    sample.rightY = right.y / 32768.0f;
    sample.jump = (buttons & HidNpadButton_A) != 0;
    sample.pause = (buttons & HidNpadButton_X) != 0;
    sample.primary = (buttons & HidNpadButton_ZR) != 0;
    sample.secondary = (buttons & HidNpadButton_ZL) != 0;
    sample.interact = (buttons & HidNpadButton_Y) != 0;
    sample.ability = (buttons & HidNpadButton_B) != 0;
    sample.chat = (buttons & HidNpadButton_Minus) != 0;
    sample.dpadLeft = (buttons & HidNpadButton_Left) != 0;
    sample.dpadRight = (buttons & HidNpadButton_Right) != 0;
    sample.dpadUp = (buttons & HidNpadButton_Up) != 0;
    sample.dpadDown = (buttons & HidNpadButton_Down) != 0;
    sample.previous = (buttons & HidNpadButton_L) != 0;
    sample.next = (buttons & HidNpadButton_R) != 0;
    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0) {
      sample.touching = true;
      sample.multipleTouches = touch.count > 1;
      sample.touchId = touch.touches[0].finger_id;
      // HID coordinates remain in the 1280x720 touch space. Normalize here;
      // Switch2Controls maps them to the selected render size, including 1080p.
      sample.touchX = touch.touches[0].x / 1279.0f;
      sample.touchY = 1.0f - touch.touches[0].y / 719.0f;
    }
    Uint32 now = SDL_GetTicks();
    float seconds = m_switchInputTicks ? (now - m_switchInputTicks) / 1000.0f : 0.0f;
    m_switchInputTicks = now;
    auto state = m_switchControls.update(sample, seconds, m_windowSize[0], m_windowSize[1]);
    if (state.chatPressed) {
      // Deliver releases before the blocking applet request. Preserve the edge
      // latch and cursor, but do not leave movement/modifiers held in the game.
      m_switchControls.suspend();
      float x = state.cursorX, y = state.cursorY;
      state = {}; state.cursorX = x; state.cursorY = y; state.chatPressed = true;
    }
    Vec2I position{(int)state.cursorX, (int)state.cursorY};
    // Send position before clicks/wheel events, including the initial center.
    if (!m_switchCursorInitialized || position != m_switchCursor) {
      events.append(MouseMoveEvent{position - m_switchCursor, position});
      m_switchCursor = position;
      m_switchCursorInitialized = true;
    }
    KeyMod modifiers = KeyMod::NoMod;
    if (state.alt) modifiers |= KeyMod::LAlt;
    if (state.shift) modifiers |= KeyMod::LShift;
    auto key = [&](bool held, bool wasHeld, Key value) {
      if (held && !wasHeld) events.append(KeyDownEvent{value, modifiers});
      else if (!held && wasHeld) events.append(KeyUpEvent{value});
    };
    // Modifier transitions precede other keys; bindings also receive the
    // modifier bits on keys pressed while long-Y or D-pad Down is active.
    key(state.alt, m_switchState.alt, Key::LAlt);
    key(state.shift, m_switchState.shift, Key::LShift);
    key(state.left, m_switchState.left, Key::A);
    key(state.right, m_switchState.right, Key::D);
    key(state.up, m_switchState.up, Key::W);
    key(state.down, m_switchState.down, Key::S);
    key(state.jump, m_switchState.jump, Key::Space);
    key(state.pause, m_switchState.pause, Key::Escape);
    key(state.interact, m_switchState.interact, Key::E);
    key(state.ability, m_switchState.ability, Key::F);
    if (state.primary && !m_switchState.primary) events.append(MouseButtonDownEvent{MouseButton::Left, position});
    else if (!state.primary && m_switchState.primary) events.append(MouseButtonUpEvent{MouseButton::Left, position});
    if (state.secondary && !m_switchState.secondary) events.append(MouseButtonDownEvent{MouseButton::Right, position});
    else if (!state.secondary && m_switchState.secondary) events.append(MouseButtonUpEvent{MouseButton::Right, position});
    if (state.previousPressed) events.append(MouseWheelEvent{MouseWheel::Up, position});
    if (state.nextPressed) events.append(MouseWheelEvent{MouseWheel::Down, position});
    if (state.chatPressed) {
      events.append(ControllerButtonDownEvent{0, ControllerButton::Back});
      events.append(ControllerButtonUpEvent{0, ControllerButton::Back});
    }
    m_switchState = state;
  }
#endif

  void getAudioData(Uint8* stream, int len) {
    if (m_audioEnabled) {
      m_application->getAudioData((int16_t*)stream, len / 4);
    } else {
      for (int i = 0; i < len; ++i)
        stream[i] = 0;
    }
  }

  void setVSyncEnabled(bool vsyncEnabled) {
    m_renderBackend->setVSyncEnabled(vsyncEnabled);
  }

  inline static constexpr size_t MaximumCursorDimensions = 128;
  inline static constexpr size_t MaximumCursorPixelCount = MaximumCursorDimensions * MaximumCursorDimensions;
  bool setCursorImage(const String& id, const ImageConstPtr& image, unsigned scale, const Vec2I& offset) {
#ifdef __SWITCH2__
    // Returning false selects the game's software cursor, which follows our
    // native-HID mouse events in menus and gameplay.
    return false;
#endif
    auto imageSize = image->size().piecewiseMultiply(Vec2U::filled(scale));
    if (!scale || imageSize.max() > MaximumCursorDimensions || ((size_t)imageSize[0] * (size_t)imageSize[1]) > MaximumCursorPixelCount)
      return m_cursorVisible = false;

    auto& entry = m_cursorCache.get(m_currentCursor = { scale, offset, id }, [&](auto const&) {
      auto entry = std::make_shared<CursorEntry>();
      List<ImageOperation> operations;
      if (scale != 1)
        operations = {
          FlipImageOperation{ FlipImageOperation::Mode::FlipY }, // SDL wants an Australian cursor.
          BorderImageOperation{ 1, Vec4B(), Vec4B(), false, false }, // Nearest scaling fucks up and clips half off the edges, work around this with border+crop for now.
          ScaleImageOperation{ ScaleImageOperation::Mode::Nearest, Vec2F::filled(scale), Vec2F::filled(scale) },
          CropImageOperation{ RectI::withSize(Vec2I::filled(ceilf((float)scale / 2)), Vec2I(imageSize)) }
        };
      else
        operations = { FlipImageOperation{ FlipImageOperation::Mode::FlipY } };

      auto newImage = std::make_shared<Image>(std::move(processImageOperations(operations, *image)));
      // Fix fully transparent pixels inverting the underlying display pixel on Windows (allowing this could be made configurable per cursor later!)
      newImage->forEachPixel([](unsigned x, unsigned y, Vec4B& pixel) { if (!pixel[3]) pixel[0] = pixel[1] = pixel[2] = 0; });
      entry->image = std::move(newImage);
      

      auto size = entry->image->size();
      uint32_t pixelFormat;
      switch (entry->image->pixelFormat()) {
        case PixelFormat::RGB24: // I know this conversion looks wrong, but it's correct. I'm confused too.
          pixelFormat = SDL_PIXELFORMAT_BGR888;
          break;
        case PixelFormat::RGBA32:
          pixelFormat = SDL_PIXELFORMAT_ABGR8888;
          break;
        case PixelFormat::BGR24:
          pixelFormat = SDL_PIXELFORMAT_RGB888;
          break;
        case PixelFormat::BGRA32:
          pixelFormat = SDL_PIXELFORMAT_ARGB8888;
          break;
        default:
          pixelFormat = SDL_PIXELFORMAT_UNKNOWN;
      }

      entry->sdlSurface.reset(SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)entry->image->data(),
        size[0], size[1],
        entry->image->bitsPerPixel(),
        entry->image->bytesPerPixel() * size[0],
        pixelFormat)
      );
      entry->sdlCursor.reset(SDL_CreateColorCursor(entry->sdlSurface.get(), offset[0] * scale, offset[1] * scale));

      return entry;
    });

    SDL_SetCursor(entry->sdlCursor.get());
    return m_cursorVisible = true;
  }

  SignalHandler m_signalHandler;

  TickRateApproacher m_updateTicker = TickRateApproacher(60.0f, 1.0f);
  float m_updateRate = 0.0f;
  TickRateMonitor m_renderTicker = TickRateMonitor(1.0f);
  float m_renderRate = 0.0f;

  SDL_Window* m_sdlWindow = nullptr;
  unique_ptr<SdlRenderBackend> m_renderBackend;
  SDL_AudioDeviceID m_sdlAudioOutputDevice = 0;
  SDL_AudioDeviceID m_sdlAudioInputDevice = 0;

  typedef std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)> SDLGameControllerUPtr;
  StableHashMap<int, SDLGameControllerUPtr> m_SdlControllers;

  typedef std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> SDLSurfaceUPtr;
  typedef std::unique_ptr<SDL_Cursor, decltype(&SDL_FreeCursor)> SDLCursorUPtr;
  struct CursorEntry {
    ImageConstPtr image = nullptr;
    SDLSurfaceUPtr sdlSurface;
    SDLCursorUPtr sdlCursor;

    CursorEntry() : image(nullptr), sdlSurface(nullptr, SDL_FreeSurface), sdlCursor(nullptr, SDL_FreeCursor) {};
  };

  typedef tuple<unsigned, Vec2I, String> CursorDescriptor;

  HashTtlCache<CursorDescriptor, std::shared_ptr<CursorEntry>> m_cursorCache;
  CursorDescriptor m_currentCursor;

#ifdef __SWITCH2__
  Vec2U m_windowSize = Switch2RenderSize;
  PadState m_switchPad{};
  Switch2Pointer m_switchPointer;
  uint64_t m_switchBlockedButtons = 0;
  Switch2Controls m_switchControls;
  Switch2Controls::State m_switchState;
  Vec2I m_switchCursor{640, 360};
  Uint32 m_switchInputTicks = 0;
  bool m_switchFocused = true, m_switchCursorInitialized = false;
#else
  Vec2U m_windowSize = {800, 600};
#endif
  WindowMode m_windowMode = WindowMode::Normal;

  String m_windowTitle = "Starbound";
  bool m_windowVSync = true;
  unsigned m_maxFrameSkip = 5;
  bool m_cursorVisible = true;
  bool m_acceptingTextInput = false;
  bool m_audioEnabled = false;
  bool m_quitRequested = false;

  RendererPtr m_renderer;
  ApplicationUPtr m_application;
  PcPlatformServicesUPtr m_platformServices;
};

int runMainApplication(ApplicationUPtr application, StringList cmdLineArgs) {
  try {
#ifdef __SWITCH2__
    std::freopen("sdmc:/xstarbound-startup.log", "w", stdout);
    std::freopen("sdmc:/xstarbound-stderr.log", "w", stderr);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef STAR_SWITCH2_RESOURCE_REPORTS
    Switch2GameAllocator::report(stdout,"startup");
    Switch2GameAllocator::reportFile("startup",false);
#endif
    Logger::refreshLoggable();
    // Outlive SdlPlatform, its application and networking threads. Linux's
    // implicit BSD sockets are not available until Horizon's driver is opened.
    Switch2SocketSession socketSession;
    if (R_SUCCEEDED(socketSession.result()))
      Logger::info("Network: Horizon socket service initialized");
    else
      Logger::warn("Network: Horizon socket initialization failed (Result 0x{:08x}); multiplayer unavailable, single-player remains available", socketSession.result());
    Logger::info("Switch 2: build profile {}", STAR_BUILD_PROFILE);
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
    Logger::info("Switch 2 allocation profiling: enabled (caller timings and SDK heap-lock sampling)");
#else
    Logger::info("Switch 2 allocation profiling: compiled out (rpmalloc ownership routing retained)");
#endif
#ifdef STAR_SWITCH2_DIAGNOSTICS
    Logger::info("Switch 2 diagnostics: enabled (watchdog, CPU profiling, debug HUD)");
#else
    Logger::info("Switch 2 diagnostics: compiled out (FPS overlay retained)");
#endif
    Logger::info("Switch 2 performance: {} native sector workers; 256 MiB parallax idle cache; 32 MiB world block cache; 4096 world index nodes; FPS overlay enabled", Thread::numberOfProcessors());
    u64 memoryLimit = 0;
    if (R_SUCCEEDED(svcGetInfo(&memoryLimit, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
      Logger::info("Switch 2: process memory limit {} MiB", memoryLimit / (1024 * 1024));
    // libounce's default homebrew startup mounts sdmc before main.
    if (::chdir("sdmc:/switch/xstarbound") != 0)
      throw ApplicationException::format("Could not open sdmc:/switch/xstarbound: {}", std::strerror(errno));
#endif
    bool vulkan = false, vulkanMenus = false, vulkanGameplay = false, opengl = false;
    eraseWhere(cmdLineArgs, [&](String& argument) {
      if (argument == "--renderer=vulkan") {
        vulkan = true;
        return true;
      }
      if (argument == "--renderer=vulkan-menus") {
        vulkanMenus = true;
        return true;
      }
      if (argument == "--renderer=vulkan-gameplay") {
        vulkanGameplay = true;
        return true;
      }
      if (argument == "--renderer=opengl") {
        opengl = true;
        return true;
      }
      return false;
    });
    if ((int)vulkan + (int)vulkanMenus + (int)vulkanGameplay + (int)opengl > 1) throw ApplicationException("Select one rendering mode");
#ifndef STAR_ENABLE_OPENGL
    if (opengl) throw ApplicationException("OpenGL support was not built; launch without --renderer=opengl to use Vulkan");
    if (!vulkan && !vulkanMenus && !vulkanGameplay) vulkanGameplay = true;
#endif
    if (vulkan) {
#ifdef STAR_ENABLE_VULKAN
      return runVulkanDiagnostic(cmdLineArgs);
#else
      throw ApplicationException("Vulkan support was not built. Configure with -DSTAR_ENABLE_VULKAN=ON.");
#endif
    }
    {
      SdlPlatform platform(std::move(application), std::move(cmdLineArgs), vulkanMenus || vulkanGameplay, vulkanGameplay);
      platform.run();
    }
    Logger::info("Application: stopped gracefully");
    return 0;
  } catch (std::exception const& e) {
    fatalException(e, true);
  } catch (...) {
    fatalError("Unknown Exception", true);
  }
  return 1;
}

}

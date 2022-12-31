#ifndef WINDOW_H_
#define WINDOW_H_

#include <cassert>
#include <concepts>
#include <optional>
#include <string>
#include <SDL2/SDL.h>
#include "opengl.h"

class MouseState {
public:
  int x, y;
  bool left, mid, right, relative = true;
};

class Window {
public:
  Window(Window const&) = delete;
  Window& operator=(Window const&) = delete;

  SDL_Window* handle() const { return mWindow; }
  SDL_GLContext context() const { return mContext; }
  OpenGL& gl() { return mGL.value(); }

  std::string title() const { return mTitle; }
  void setTitle(std::string const& title) {
    mTitle = title;
    SDL_SetWindowTitle(mWindow, title.c_str());
  }

  size_t width() const { return mWidth; }
  size_t height() const { return mHeight; }
  bool shouldQuit() const { return mShouldQuit; }

  MouseState mousePosition() const {
    if (mMouse.relative) assert(false); // Cursor locked, use mouseMotion() instead!
    return mMouse;
  }

  MouseState mouseMotion() const {
    if (mMouse.relative) return mMouse;
    MouseState res = mMouse;
    res.x -= mPrevMouse.x;
    res.y -= mPrevMouse.y;
    return res;
  }

  bool mouseLocked() const { return mMouseLocked; }
  void setMouseLocked(bool locked) {
    mMouseLocked = locked;
    SDL_SetRelativeMouseMode(locked ? SDL_TRUE : SDL_FALSE);
  }

  bool fullscreen() const { return mFullscreen; }
  void setFullscreen(bool enabled) {
    mFullscreen = enabled;
    SDL_SetWindowFullscreen(mWindow, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }

  void makeCurrent() const { SDL_GL_MakeCurrent(mWindow, mContext); }
  void swapBuffers() const { SDL_GL_SwapWindow(mWindow); }
  void pollEvents();

  static Uint8 const* keyboardState() { return SDL_GetKeyboardState(nullptr); }
  static bool isKeyPressed(Uint8 c) { return keyboardState()[c] != 0; }

  static Window& singleton(
    std::string const& title = "",
    size_t width = 0,
    size_t height = 0,
    size_t multisample = 0,
    bool forceMinimumVersion = false,
    bool debugContext = false
  ) {
    static auto res = Window(title, width, height, multisample, forceMinimumVersion, debugContext);
    return res;
  }

private:
  SDL_Window* mWindow = nullptr;
  SDL_GLContext mContext;
  std::optional<OpenGL> mGL; // late

  std::string mTitle;
  size_t mWidth;
  size_t mHeight;

  bool mShouldQuit = false;
  MouseState mMouse;
  MouseState mPrevMouse;
  bool mMouseLocked = false;
  bool mFullscreen = false;

  Window(
    std::string const& title,
    size_t width,
    size_t height,
    size_t multisample,
    bool forceMinimumVersion,
    bool debugContext
  );
  ~Window();
};

static_assert(!std::move_constructible<Window>);
static_assert(!std::assignable_from<Window&, Window&&>);

#endif // WINDOW_H_

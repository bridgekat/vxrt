#ifndef WINDOW_H_
#define WINDOW_H_

#include <string>
#include <SDL2/SDL.h>
#include "debug.h"

class MouseState {
public:
	int x, y;
	bool left, mid, right, relative = true;
};

class Window {
public:
	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	void makeCurrentDraw() const { SDL_GL_MakeCurrent(mWindow, mContext); }
	void swapBuffers() const { SDL_GL_SwapWindow(mWindow); }

	static const Uint8* getKeyBoardState() { return SDL_GetKeyboardState(nullptr); }
	static bool isKeyPressed(Uint8 c) { return getKeyBoardState()[c] != 0; }

	int getWidth() const { return mWidth; }
	int getHeight() const { return mHeight; }

	MouseState getMousePosition() const {
		if (mMouse.relative) {
			// Cursor locked, use getMouseMotion() instead!
			Assert(false);
		}
		return mMouse;
	}

	MouseState getMouseMotion() const {
		if (mMouse.relative) return mMouse;
		MouseState res = mMouse;
		res.x -= mPrevMouse.x;
		res.y -= mPrevMouse.y;
		return res;
	}

	void lockCursor() const { SDL_SetRelativeMouseMode(SDL_TRUE); }
	void unlockCursor() const { SDL_SetRelativeMouseMode(SDL_FALSE); }
	void setTitle(const std::string& title) { SDL_SetWindowTitle(mWindow, title.c_str()); }
	void setFullscreen(bool f) {
		if (f) SDL_SetWindowFullscreen(mWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		else SDL_SetWindowFullscreen(mWindow, 0);
	}

	void pollEvents();

	static Window& getDefaultWindow(const std::string& title = "", int width = 0, int height = 0) {
		static Window win(title, width, height);
		return win;
	}

	bool shouldQuit() const { return mShouldQuit; }

private:
	SDL_Window* mWindow = nullptr;
	std::string mTitle;
	int mWidth, mHeight;
	MouseState mMouse, mPrevMouse;
	bool mShouldQuit = false;

	Window(const std::string& title, int width, int height);
	~Window();

	SDL_GLContext mContext;
};

#endif


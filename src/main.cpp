#include <stdlib.h>
#include "config.h"
#include "window.h"
#include "renderer.h"
#include "vertexarray.h"
#include "camera.h"
#include "shaderbuffer.h"
#include "tree.h"
#include "framebuffer.h"
#include "updatescheduler.h"
#include "bitmap.h"
#include "texture.h"

const float Gamma = 2.2f;

FrameBuffer fbo[2];
ShaderProgram presenter;
int patchSize, fbWidthDefault = 0, fbHeightDefault = 0;
int fbWidth, fbHeight, rasterChunks;
bool preInitFBO;

void enablePathTracing(Window& win) {
	Renderer::waitForComplete();
	if (fbWidthDefault <= 0 || fbHeightDefault <= 0) {
		fbWidth = win.getWidth();
		fbHeight = win.getHeight();
	} else {
		fbWidth = fbWidthDefault;
		fbHeight = fbHeightDefault;
	}
	if (!preInitFBO) {
		fbo[0].create(fbWidth, fbHeight, 1, false);
		fbo[1].create(fbWidth, fbHeight, 1, false);
	}
	Renderer::enableTexture2D();
	win.unlockCursor();
}

void disablePathTracing(Window& win) {
	Renderer::waitForComplete();
	Renderer::disableTexture2D();
	win.lockCursor();
}

TextureID loadNoiseMipmaps(const TextureImage& image, bool maximal) {
	TextureID id = 0;
	int maxLevels = (int)log2(image.width());
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, maxLevels);
//	glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, 0.0f);
	int scale = 1;
	for (int i = 0; i <= maxLevels; i++) {
		TextureImage curr(image.width() / scale, image.height() / scale, image.bytesPerPixel());
		for (int i = 0; i < image.height() / scale; i++)
			for (int j = 0; j < image.width() / scale; j++)
				for (int k = 0; k < image.bytesPerPixel(); k++) {
					int sum = maximal? 0 : 255;
					for (int i1 = 0; i1 < scale; i1++) for (int j1 = 0; j1 < scale; j1++) {
						int c = image.color(i * scale + i1, j * scale + j1, k);
						sum = maximal? std::max(sum, c) : std::min(sum, c);
					}
					curr.color(i, j, k) = sum;
				}
		glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA, curr.width(), curr.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, curr.data());
		scale *= 2;
	}
	return id;
}

int main(){
	Config::load();

	Window& win = Window::getDefaultWindow("vxrt", 852, 480);
	
	std::stringstream ss;
	ss.str(""); ss << "Renderer: " << glGetString(GL_RENDERER) << " [" << glGetString(GL_VENDOR) << "]";
	LogInfo(ss.str());
	ss.str(""); ss << "OpenGL version: " << glGetString(GL_VERSION);
	LogInfo(ss.str());

	Renderer::init();
	presenter.loadShadersFromFile(std::string(ShaderPath) + "Present.vsh", std::string(ShaderPath) + "Present.fsh");
	patchSize = Config::getInt("PathTracing.PatchSize", 8);
	fbWidthDefault = Config::getInt("PathTracing.DefaultRenderWidth", 0);
	fbHeightDefault = Config::getInt("PathTracing.DefaultRenderHeight", 0);
	rasterChunks = Config::getInt("PathTracing.RasterChunks", 1);
	if (Config::getInt("PathTracing.PreInitFBOs", 0) == 1) {
		if (fbWidthDefault > 0 && fbHeightDefault > 0) {
			preInitFBO = true;
			LogInfo("Pre-initializing FBOs...");
			fbo[0].create(fbWidthDefault, fbHeightDefault, 1, false);
			fbo[1].create(fbWidthDefault, fbHeightDefault, 1, false);
		}
	}
	
	Shader mainShader;
	mainShader.loadFromFile(GL_COMPUTE_SHADER, std::string(ShaderPath) + "Main.csh");
	ShaderProgram mainProgram;
	mainProgram.create();
	mainProgram.attach(mainShader);
	mainProgram.link();

	// Init voxels
//	Tree tree(Config::getInt("World.Size", 256), Config::getInt("World.Height", 256));
//	tree.generate();

//	ShaderBuffer ssbo(mainProgram, "TreeData", 0);
//	tree.upload(ssbo);
	
	// Init noise
	const int NoiseTextureSize = 256, OffsetX = 37, OffsetY = 17;
	TextureImage noiseImage(NoiseTextureSize, NoiseTextureSize, 4);
	srand(2333);
	for (int x = 0; x < NoiseTextureSize; x++) for (int y = 0; y < NoiseTextureSize; y++) {
		int x1 = (x + OffsetX) % NoiseTextureSize, y1 = (y + OffsetY) % NoiseTextureSize;
		noiseImage.color(x, y, 2) = noiseImage.color(x1, y1, 0) = rand() % 256;
		noiseImage.color(x, y, 3) = noiseImage.color(x1, y1, 1) = rand() % 256;
	}
	Texture noiseTexture(noiseImage, true, 0);
	int noiseTextureIndex = 7;
	noiseTexture.bind(noiseTextureIndex);
	
	// Init noise bounds
	TextureImage maxImage(NoiseTextureSize, NoiseTextureSize, 4);
	TextureImage minImage(NoiseTextureSize, NoiseTextureSize, 4);
	for (int x = 0; x < NoiseTextureSize; x++) for (int y = 0; y < NoiseTextureSize; y++) {
		int x1 = (x + 1) % NoiseTextureSize, y1 = (y + 1) % NoiseTextureSize;
		maxImage.color(x, y, 0) = std::max({noiseImage.color(x, y, 0), noiseImage.color(x1, y, 0), noiseImage.color(x, y1, 0), noiseImage.color(x1, y1, 0)});
		minImage.color(x, y, 0) = std::min({noiseImage.color(x, y, 0), noiseImage.color(x1, y, 0), noiseImage.color(x, y1, 0), noiseImage.color(x1, y1, 0)});
	}
	Texture maxTexture(loadNoiseMipmaps(maxImage, true));
	Texture minTexture(loadNoiseMipmaps(minImage, false));
	int maxTextureIndex = 6, minTextureIndex = 5;
	maxTexture.bind(maxTextureIndex);
	minTexture.bind(minTextureIndex);
	
	Camera camera;
	win.lockCursor();

	UpdateScheduler frameCounterScheduler(1);
	int frameCounter = 0;
	double startTime = UpdateScheduler::timeFromEpoch();

	bool pathTracing = false;

	while (!win.shouldQuit()) {
		Renderer::waitForComplete();
		Renderer::checkError();
		win.swapBuffers();

		// Init rendering
		int curr = frameCounter & 1;
		if (!pathTracing) {
			fbWidth = win.getWidth(), fbHeight = win.getHeight();
			fbo[curr].create(fbWidth, fbHeight, 1, false);
		}
		Renderer::setRenderArea(0, 0, fbWidth, fbHeight);
//		fbo[curr].bind();
		fbo[curr ^ 1].bindColorTextures(0);
		Renderer::clear();

		mainProgram.bind();
//		Renderer::setProjection(camera.getProjectionMatrix());
//		Renderer::setModelview(camera.getModelViewMatrix());
		mainProgram.setUniformMatrix4fv("ProjectionMatrix", camera.getProjectionMatrix().getTranspose().data);
		mainProgram.setUniformMatrix4fv("ModelViewMatrix", camera.getModelViewMatrix().getTranspose().data);
		mainProgram.setUniformMatrix4fv("ProjectionInverse", camera.getProjectionMatrix().getInverse().getTranspose().data);
		mainProgram.setUniformMatrix4fv("ModelViewInverse", camera.getModelViewMatrix().getInverse().getTranspose().data);

		// Init shaders
		noiseTexture.bind(noiseTextureIndex);
//		mainProgram.setUniform1i("RootSize", tree.size());
		Vec3f pos = camera.position();
		mainProgram.setUniform3f("CameraPosition", pos.x, pos.y, pos.z);
		mainProgram.setUniform1f("RandomSeed", float(rand() * (RAND_MAX + 1.0f) + rand()) / (RAND_MAX + 1.0f) / (RAND_MAX + 1.0f));
		mainProgram.setUniform1i("NoiseTexture", noiseTextureIndex);
		mainProgram.setUniform1i("MaxTexture", maxTextureIndex);
		mainProgram.setUniform1i("MinTexture", minTextureIndex);
		mainProgram.setUniform1f("NoiseTextureSize", NoiseTextureSize);
		mainProgram.setUniform2f("NoiseOffset", OffsetX, OffsetY);
		mainProgram.setUniform1i("PathTracing", int(pathTracing));
		mainProgram.setUniform1f("Time", float(UpdateScheduler::timeFromEpoch() - startTime));
		mainProgram.setUniform1i("PrevFrame", 0);
		mainProgram.setUniform1i("SampleCount", frameCounter);
		mainProgram.setUniform1i("FrameWidth", fbWidth);
		mainProgram.setUniform1i("FrameHeight", fbHeight);
		mainProgram.setUniform1i("FrameBufferSize", fbo[curr].size());

		// Render scene (sample once for each pixel)
		int outimageIndex = 0;
		mainProgram.setUniform1i("FrameBuffer", outimageIndex);
		glBindImageTexture(outimageIndex, fbo[curr].colorTextures()[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute((fbWidth - 1) / patchSize + 1, (fbHeight - 1) / patchSize + 1, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

		// Present to screen
//		fbo[curr].unbind();
		fbo[curr].bindColorTextures(0);
		presenter.bind();
		Renderer::clear();
		Renderer::setRenderArea(0, 0, win.getWidth(), win.getHeight());
		presenter.setUniform1i("Texture", 0);
		presenter.setUniform1i("FrameWidth", fbWidth);
		presenter.setUniform1i("FrameHeight", fbHeight);
		presenter.setUniform1i("FrameBufferSize", fbo[curr].size());
		VertexArray va(6, VertexFormat(0, 0, 0, 2));
		va.addVertex({-1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f,-1.0f});
		VertexBuffer(va, false).render();
		presenter.unbind();

		// Update FPS
		frameCounter++;
		frameCounterScheduler.refresh();
		while (!frameCounterScheduler.inSync()) {
			std::stringstream ss;
			if (!pathTracing) {
				ss << "Voxel Raycasting Test (Static SVO, " << frameCounter << " fps)";
				frameCounter = 0;
			} else {
				ss << "Voxel Pathtracing Test (Static SVO, " << frameCounter << " samples per pixel)";
			}
			Window::getDefaultWindow().setTitle(ss.str());
			frameCounterScheduler.increase();
		}

		win.pollEvents();
		
		// Screenshot
		static bool opressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_O)) {
			if (!opressed) {
				if (pathTracing) {
					// Read data
					fbo[curr].bindBufferRead(0);
					Bitmap bmp(fbWidth, fbHeight, Vec3i(0, 0, 0));
					glReadPixels(0, 0, fbWidth, fbHeight, GL_RGB, GL_UNSIGNED_BYTE, bmp.data);
					fbo[curr].unbindRead();
					// Gamma correction
					for (int i = 0; i < bmp.h; i++) for (int j = 0; j < bmp.w; j++) {
						Vec3f col = Vec3f(bmp.getPixel(j, i)) / 255.0f;
						col.x = pow(col.x, 1.0f / Gamma);
						col.y = pow(col.y, 1.0f / Gamma);
						col.z = pow(col.z, 1.0f / Gamma);
						bmp.setPixel(j, i, Vec3i(col * 255.0f));
					}
					// Save file
					std::stringstream ss, sss;
					ss << ScreenshotPath << fbWidth << "x" << fbHeight << "-" << frameCounter << "spp-" << UpdateScheduler::timeFromEpoch() - startTime << "s.bmp";
					bmp.save(ss.str());
					sss << "Saved screenshot " << ss.str();
					LogInfo(sss.str());
				}
			}
			opressed = true;
		} else opressed = false;

		// Switches
		static bool lpressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_L)) {
			if (!lpressed) {
				static bool locked = true;
				locked = !locked;
				if (locked) win.lockCursor();
				else win.unlockCursor();
			}
			lpressed = true;
		} else lpressed = false;

		static bool upressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_U)) {
			if (!upressed) {
				static bool fullscreen = false;
				fullscreen = !fullscreen;
				if (fullscreen) win.setFullscreen(true);
				else win.setFullscreen(false);
			}
			upressed = true;
		} else upressed = false;

		static bool ppressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_P)) {
			if (!ppressed) {
				pathTracing = !pathTracing;
				if (pathTracing) enablePathTracing(win);
				else disablePathTracing(win);
				frameCounter = 0;
			}
			ppressed = true;
		} else ppressed = false;

		// Camera parameters
		if (!pathTracing) {
			camera.setPerspective(70.0f, float(win.getWidth()) / float(win.getHeight()), 0.1f, 256.0f);
			camera.update(win);
		} else {
			camera.setPerspective(70.0f, float(fbWidth) / float(fbHeight), 0.1f, 256.0f);
		}

		if (Window::isKeyPressed(SDL_SCANCODE_ESCAPE)) break;
	}

	win.unlockCursor();

	Config::save();
	return 0;
}


#include <array>
#include <cstdint>
#include <optional>
#include "bitmap.h"
#include "camera.h"
#include "config.h"
#include "framebuffer.h"
#include "shaderstoragebuffer.h"
#include "texture.h"
#include "tree.h"
#include "updatescheduler.h"
#include "vertexarray.h"
#include "window.h"

double const gamma = 2.2;
size_t const prevFrameTextureIndex = 0, noiseTextureIndex = 1, maxTextureIndex = 2, minTextureIndex = 3,
             beamTextureIndex = 4;
size_t const outImageIndex = 0;

size_t patchWidth, patchHeight;
size_t renderWidth = 0, renderHeight = 0;

void enablePathTracing(Window& window, std::array<FrameBuffer, 3>& fbo) {
  if (renderWidth > 0 && renderHeight > 0) {
    fbo[0] = FrameBuffer(renderWidth, renderHeight, 1, false);
    fbo[1] = FrameBuffer(renderWidth, renderHeight, 1, false);
    fbo[2] = FrameBuffer(renderWidth, renderHeight, 1, false);
  }
  window.setMouseLocked(false);
}

void disablePathTracing(Window& window, std::array<FrameBuffer, 3>& fbo) {
  if (renderWidth > 0 && renderHeight > 0) {
    fbo[0] = FrameBuffer(window.width(), window.height(), 1, false);
    fbo[1] = FrameBuffer(window.width(), window.height(), 1, false);
    fbo[2] = FrameBuffer(window.width(), window.height(), 1, false);
  }
  window.setMouseLocked(true);
}

OpenGL::Object loadNoiseMipmaps(Bitmap const& image, bool maximal) {
  size_t size = image.width();
  size_t levels = ceilLog2(size);
  OpenGL::Object res;
  glGenTextures(1, &res);
  glBindTexture(GL_TEXTURE_2D, res);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, static_cast<GLint>(levels));
  for (size_t level = 0, scale = 1; level <= levels; level++, scale *= 2) {
    assert(scale <= size);
    Bitmap curr(size / scale, size / scale, image.bytesPerPixel());
    for (size_t i = 0; i < size / scale; i++)
      for (size_t j = 0; j < size / scale; j++)
        for (size_t k = 0; k < image.bytesPerPixel(); k++) {
          uint8_t sum = maximal ? 0 : 255;
          for (size_t i1 = 0; i1 < scale; i1++)
            for (size_t j1 = 0; j1 < scale; j1++) {
              uint8_t c = image.at(j * scale + j1, i * scale + i1, k);
              sum = maximal ? std::max(sum, c) : std::min(sum, c);
            }
          curr.at(j, i, k) = sum;
        }
    glTexImage2D(
      GL_TEXTURE_2D,
      static_cast<GLint>(level),
      OpenGL::internalFormatRGBA,
      static_cast<GLsizei>(curr.width()),
      static_cast<GLsizei>(curr.height()),
      0,
      OpenGL::formatRGBA,
      GL_UNSIGNED_BYTE,
      curr.data()
    );
  }
  return res;
}

int main() {
  Config config;
  config.load();

  size_t multisample = config.getOr("OpenGL.Multisamples", 0);
  bool forceMinimumVersion = config.getOr("OpenGL.ForceMinimumVersion", 0) != 0;
  bool debugContext = config.getOr("OpenGL.Debugging", 0) != 0;

  Window& window = Window::singleton("", 852, 480, multisample, forceMinimumVersion, debugContext);
  OpenGL& gl = window.gl();

  std::stringstream ss;
  ss.str("");
  ss << "Renderer: " << glGetString(GL_RENDERER) << " [" << glGetString(GL_VENDOR) << "]";
  Log::info(ss.str());
  ss.str("");
  ss << "OpenGL version: " << glGetString(GL_VERSION);
  Log::info(ss.str());

  patchWidth = config.getOr("PathTracing.PatchWidth", 8);
  patchHeight = config.getOr("PathTracing.PatchHeight", 8);
  renderWidth = config.getOr("PathTracing.RenderWidth", 0);
  renderHeight = config.getOr("PathTracing.RenderHeight", 0);

  ShaderProgram basicShader({
    ShaderStage(OpenGL::vertexShader, std::string(ShaderPath) + "Basic.vsh"),
    ShaderStage(OpenGL::fragmentShader, std::string(ShaderPath) + "Basic.fsh"),
  });
  ShaderProgram mainShader({
    ShaderStage(OpenGL::computeShader, std::string(ShaderPath) + "Main.csh"),
  });

  // Init voxels.
  bool dynamicMode = config.getOr("World.Dynamic", 0) != 0;
  size_t maxNodes = config.getOr("World.Dynamic.MaxNodes", 268435454);
  size_t maxHeight = config.getOr("World.Static.MaxHeight", 256);
  size_t worldLevels = config.getOr("World.MaxLevels", 8);
  size_t noiseLevels = config.getOr("World.Dynamic.NoiseLevels", 8);
  size_t partialLevels = config.getOr("World.Dynamic.PartialLevels", 4);
  size_t noiseSize = 1u << noiseLevels;

  ShaderStorageBuffer ssbo(mainShader, "TreeData", 1);
  if (dynamicMode) {
    uint32_t initialHeader = 1;
    ssbo.allocateImmutable(sizeof(initialHeader) + sizeof(uint32_t) * maxNodes, true);
    ssbo.uploadSubData(0, sizeof(initialHeader), &initialHeader);
  } else {
    maxNodes = 0;
    Tree tree(1u << worldLevels, maxHeight);
    tree.generate();
    tree.upload(ssbo, true);
  }

  ShaderStorageBuffer outBuffer(mainShader, "OutputData", 2);
  uint32_t initialData = 0;
  outBuffer.allocateImmutable(sizeof(initialData), true);
  outBuffer.uploadSubData(0, sizeof(initialData), &initialData);

  // Init noise.
  Bitmap noiseImage(noiseSize, noiseSize, 4);
  for (size_t x = 0; x < noiseSize; x++)
    for (size_t y = 0; y < noiseSize; y++) {
      noiseImage.at(x, y, 2) = noiseImage.at(x, y, 0) = rand() % 256;
      noiseImage.at(x, y, 3) = noiseImage.at(x, y, 1) = rand() % 256;
    }
  Texture noiseTexture(noiseImage, 0);

  // Init noise bounds.
  Bitmap maxImage(noiseSize, noiseSize, 4);
  Bitmap minImage(noiseSize, noiseSize, 4);
  for (size_t x = 0; x < noiseSize; x++)
    for (size_t y = 0; y < noiseSize; y++) {
      size_t x1 = (x + 1) % noiseSize, y1 = (y + 1) % noiseSize;
      maxImage.at(x, y, 0) = std::max({
        noiseImage.at(x, y, 0),
        noiseImage.at(x1, y, 0),
        noiseImage.at(x, y1, 0),
        noiseImage.at(x1, y1, 0),
      });
      minImage.at(x, y, 0) = std::min({
        noiseImage.at(x, y, 0),
        noiseImage.at(x1, y, 0),
        noiseImage.at(x, y1, 0),
        noiseImage.at(x1, y1, 0),
      });
    }
  Texture maxTexture = loadNoiseMipmaps(maxImage, true);
  Texture minTexture = loadNoiseMipmaps(minImage, false);

  // Init framebuffers.
  std::array<FrameBuffer, 3> fbo = {
    FrameBuffer(window.width(), window.height(), 1, false),
    FrameBuffer(window.width(), window.height(), 1, false),
    FrameBuffer(window.width(), window.height(), 1, false),
  };

  Camera camera;
  window.setMouseLocked(true);

  UpdateScheduler frameCounterScheduler(1.0);
  size_t frameCounter = 0;
  double startTime = UpdateScheduler::timeFromEpoch();

  bool pathTracing = false;

  while (!window.shouldQuit()) {
    window.swapBuffers();
    gl.checkError();

    // Swap and resize framebuffers.
    if (pathTracing) {
      std::swap(fbo[0], fbo[1]);
      fbo[1].bindColorTextureAt(0, prevFrameTextureIndex);
    } else {
      auto const width = window.width(), height = window.height();
      if (fbo[0].width() != width || fbo[0].height() != height) {
        fbo[0] = FrameBuffer(width, height, 1, false);
        fbo[1] = FrameBuffer(width, height, 1, false);
        fbo[2] = FrameBuffer(width, height, 1, false);
      }
    }
    auto const width = fbo[0].width(), height = fbo[0].height();

    // Init shaders.
    noiseTexture.bindAt(noiseTextureIndex);
    maxTexture.bindAt(maxTextureIndex);
    minTexture.bindAt(minTextureIndex);
    mainShader.use();
    // See: https://en.cppreference.com/w/cpp/language/lifetime
    // mainShader.uniformMat4("ProjectionMatrix", camera.getProjectionMatrix().data());
    // mainShader.uniformMat4("ModelViewMatrix", camera.getModelViewMatrix().data());
    mainShader.uniformMat4("ProjectionInverse", camera.getProjectionMatrix().inverted().data());
    mainShader.uniformMat4("ModelViewInverse", camera.getModelViewMatrix().inverted().data());
    mainShader.uniformVec3("CameraPosition", camera.position().x, camera.position().y, camera.position().z);
    mainShader.uniformFloat("RandomSeed", static_cast<float>(UpdateScheduler::timeFromEpoch() - startTime));
    mainShader.uniformBool("PathTracing", pathTracing);
    mainShader.uniformBool("ProfilerOn", Window::isKeyPressed(SDL_SCANCODE_M));
    // mainShader.uniformFloat("Time", static_cast<float>(UpdateScheduler::timeFromEpoch() - startTime));
    mainShader.uniformSampler("PrevFrame", prevFrameTextureIndex);
    mainShader.uniformSampler("NoiseTexture", noiseTextureIndex);
    mainShader.uniformSampler("MaxTexture", maxTextureIndex);
    mainShader.uniformSampler("MinTexture", minTextureIndex);
    mainShader.uniformSampler("BeamTexture", beamTextureIndex);
    mainShader.uniformUInt("SampleCount", frameCounter);
    mainShader.uniformUInt("FrameWidth", width);
    mainShader.uniformUInt("FrameHeight", height);
    mainShader.uniformBool("DynamicMode", dynamicMode);
    mainShader.uniformUInt("MaxNodes", maxNodes);
    mainShader.uniformUInt("MaxLevels", worldLevels);
    mainShader.uniformUInt("NoiseLevels", noiseLevels);
    mainShader.uniformUInt("PartialLevels", partialLevels);
    mainShader.uniformImage("FrameBuffer", outImageIndex);

    // Coarse passes (beam optimization.)
    // size_t beamSize = std::max(patchWidth, patchHeight);
    mainShader.uniformBool("BeamMode", true);
    mainShader.uniformUInt("PrevBeamSize", 0); // Initialize.
    size_t beamSize = 64;                      // TODO?
    while (beamSize >= 4) {
      auto const btexWidth = width / beamSize + 1, btexHeight = height / beamSize + 1;
      mainShader.uniformUInt("CurrBeamSize", beamSize);
      fbo[2].bindColorTextureAt(0, beamTextureIndex);
      glBindImageTexture(outImageIndex, fbo[0].colorTextures().at(0), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
      glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      glDispatchCompute((btexWidth - 1) / patchWidth + 1, (btexHeight - 1) / patchHeight + 1, 1);
      mainShader.uniformUInt("PrevBeamSize", beamSize);
      beamSize /= 4;
      swap(fbo[0], fbo[2]);
    }

    // Render scene (sample once for each pixel.)
    mainShader.uniformBool("BeamMode", false);
    mainShader.uniformUInt("CurrBeamSize", 1);
    fbo[2].bindColorTextureAt(0, beamTextureIndex);
    glBindImageTexture(outImageIndex, fbo[0].colorTextures().at(0), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glDispatchCompute((width - 1) / patchWidth + 1, (height - 1) / patchHeight + 1, 1);

    gl.setDrawArea(0, 0, window.width(), window.height());
    gl.clear();

    // Present to screen.
    basicShader.use();
    basicShader.uniformSampler("Texture2D", prevFrameTextureIndex);
    basicShader.uniformBool("Texture2DEnabled", true);
    basicShader.uniformBool("ColorEnabled", false);
    basicShader.uniformBool("GammaCorrection", true);
    fbo[0].bindColorTextureAt(0, prevFrameTextureIndex);
    // See: https://www.khronos.org/opengl/wiki/Memory_Model#External_visibility
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    auto wfrac = static_cast<float>(width) / static_cast<float>(fbo[0].size());
    auto hfrac = static_cast<float>(height) / static_cast<float>(fbo[0].size());
    VertexBuffer(VertexArray(VertexLayout(OpenGL::triangleStrip, 2, 2))
                   .texCoords({0.0f, hfrac})
                   .vertex({-1.0f, 1.0f})
                   .texCoords({0.0f, 0.0f})
                   .vertex({-1.0f, -1.0f})
                   .texCoords({wfrac, hfrac})
                   .vertex({1.0f, 1.0f})
                   .texCoords({wfrac, 0.0f})
                   .vertex({1.0f, -1.0f}))
      .draw();

    // Update FPS
    frameCounter++;
    frameCounterScheduler.refresh();
    while (!frameCounterScheduler.inSync()) {
      // Count nodes.
      uint32_t count = 0;
      outBuffer.download(sizeof(count), &count);
      // Update window title.
      std::stringstream ss;
      ss << "Voxel Raycasting Test (";
      if (dynamicMode) {
        ss << count << " (" << static_cast<size_t>(count) * 100 / maxNodes << "%) nodes dynamic";
      } else {
        ss << count << " nodes static";
      }
      if (pathTracing) {
        ss << ", " << frameCounter << " samples per pixel";
      } else {
        ss << ", FPS: " << frameCounter << ", X: " << camera.position().x << ", Y: " << camera.position().y
           << ", Z: " << camera.position().z;
        frameCounter = 0;
      }
      ss << ")";
      Window::singleton().setTitle(ss.str());
      frameCounterScheduler.increase();
    }
    window.pollEvents();

    // Screenshot
    static bool opressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_O)) {
      if (!opressed) {
        if (pathTracing) {
          // Read data.
          Bitmap bmp(width, height, 3);
          fbo[0].bindBufferForRead(0);
          glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, bmp.data());
          fbo[0].unbindBuffers();
          // gamma correction.
          for (size_t i = 0; i < bmp.height(); i++)
            for (size_t j = 0; j < bmp.width(); j++) {
              for (size_t k = 0; k < bmp.bytesPerPixel(); k++) {
                double col = bmp.at(j, i, k) / 255.0;
                col = pow(col, 1.0 / gamma);
                bmp.at(j, i, k) = static_cast<uint8_t>(col * 255.0);
              }
            }
          // Save file.
          std::stringstream ss;
          ss << ScreenshotPath << width << "x" << height << "-" << frameCounter << "spp-"
             << UpdateScheduler::timeFromEpoch() - startTime << "s.bmp";
          bmp.swapChannels(0, 2);
          bmp.save(ss.str());
          Log::info("Saved screenshot `" + ss.str() + "`.");
        }
      }
      opressed = true;
    } else opressed = false;

    // Switches
    static bool lpressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_L)) {
      if (!lpressed) window.setMouseLocked(!window.mouseLocked());
      lpressed = true;
    } else {
      lpressed = false;
    }

    static bool upressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_U)) {
      if (!upressed) window.setFullscreen(!window.fullscreen());
      upressed = true;
    } else {
      upressed = false;
    }

    static bool ppressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_P)) {
      if (!ppressed) {
        pathTracing = !pathTracing;
        if (pathTracing) enablePathTracing(window, fbo);
        else disablePathTracing(window, fbo);
        frameCounter = 0;
      }
      ppressed = true;
    } else {
      ppressed = false;
    }

    static bool cpressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_C)) {
      if (!cpressed) {
        Tree curr(1, 1);
        curr.download(ssbo);
        curr.check();
      }
      cpressed = true;
    } else {
      cpressed = false;
    }

    static bool gpressed = false;
    if (Window::isKeyPressed(SDL_SCANCODE_G)) {
      if (!gpressed) {
        Tree curr(1, 1), opt(1, 1);
        curr.download(ssbo);
        curr.gc(opt);
        opt.upload(ssbo, false);
      }
      gpressed = true;
    } else {
      gpressed = false;
    }

    // Camera parameters.
    auto aspect = static_cast<float>(width) / static_cast<float>(height);
    camera.setPerspective(70.0f, aspect, 0.1f, 256.0f);
    if (!pathTracing) camera.update(window);

    if (Window::isKeyPressed(SDL_SCANCODE_ESCAPE)) break;
  }

  config.save();
  return 0;
}

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include "bitmap.h"
#include "camera.h"
#include "config.h"
#include "framebuffer.h"
#include "shaderstorage.h"
#include "texture.h"
#include "tree.h"
#include "updatescheduler.h"
#include "vertexarray.h"
#include "window.h"

struct MainOutputData {
  uint32_t count;
};

struct HitTestOutputData {
  float velocity[4];
  bool onGround;
};

static_assert(std::is_standard_layout_v<MainOutputData> && std::is_trivially_copyable_v<MainOutputData>);
static_assert(std::is_standard_layout_v<HitTestOutputData> && std::is_trivially_copyable_v<HitTestOutputData>);

constexpr auto beamLevels = 1_z;
constexpr auto beamSizes = std::array<size_t, beamLevels>{4_z};

constexpr auto frameTextureIndex = 0, noiseTextureIndex = 1, maxTextureIndex = 2, minTextureIndex = 3;
constexpr auto frameImageIndex = 0;
constexpr auto beamImageIndices = std::array<GLint, beamLevels>{1};
constexpr auto treeBufferIndex = 0, mainOutputBufferIndex = 1, hitTestOutputBufferIndex = 2;

auto initTreeBuffer(bool dynamicMode, size_t maxNodes, size_t worldSize, size_t maxHeight) -> ShaderStorage {
  if (dynamicMode) {
    auto initialHeader = static_cast<uint32_t>(1);
    auto res = ShaderStorage(sizeof(initialHeader) + sizeof(uint32_t) * maxNodes);
    res.upload(0, sizeof(initialHeader), &initialHeader);
    return res;
  } else {
    auto tree = Tree(worldSize, maxHeight);
    tree.generate();
    auto res = ShaderStorage(tree.uploadSize());
    tree.upload(res);
    return res;
  }
}

auto loadNoiseMipmaps(Bitmap const& image, bool maximal) -> Texture {
  auto const size = image.width();
  auto const levels = ceilLog2(size);
  auto res = Texture();
  auto prev = res.push();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, static_cast<GLint>(levels));
  for (auto level = 0_z, scale = 1_z; level <= levels; level++, scale *= 2_z) {
    assert(scale <= size);
    auto curr = Bitmap(size / scale, size / scale, image.bytesPerPixel());
    for (auto i = 0_z; i < size / scale; i++)
      for (auto j = 0_z; j < size / scale; j++)
        for (auto k = 0_z; k < image.bytesPerPixel(); k++) {
          auto sum = uint8_t(maximal ? 0 : 255);
          for (auto i1 = 0_z; i1 < scale; i1++)
            for (auto j1 = 0_z; j1 < scale; j1++) {
              auto const c = image.at(j * scale + j1, i * scale + i1, k);
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
  res.pop(prev);
  return res;
}

auto updateCameraFrame(Window const& window, Camera& camera) -> void {
  MouseState mouse = window.mouseMotion();
  camera.rotation += Vec3f(-static_cast<float>(mouse.y) * 0.3f, -static_cast<float>(mouse.x) * 0.3f, 0.0f);
}

auto updateCameraInterval(
  Window const& window,
  Camera& camera,
  Vec3f& velocity,
  float& accY,
  bool& onGround,
  bool flying
) -> void {
  camera.position += velocity;

  if (flying) {
    velocity *= 0.8f;
    accY = 0.0f;
  } else {
    velocity.x *= 0.4f;
    velocity.z *= 0.4f;
    if (onGround) accY = 0.0f;
    else velocity.y += accY;
    accY -= 0.005f;
  }

  auto acc = Vec3f(0);
  if (window.isKeyPressed(SDL_SCANCODE_W)) acc += Vec3f(0, 0, -1);
  if (window.isKeyPressed(SDL_SCANCODE_S)) acc += Vec3f(0, 0, 1);
  if (window.isKeyPressed(SDL_SCANCODE_A)) acc += Vec3f(-1, 0, 0);
  if (window.isKeyPressed(SDL_SCANCODE_D)) acc += Vec3f(1, 0, 0);
  if (flying) {
    if (window.isKeyPressed(SDL_SCANCODE_SPACE)) acc += Vec3f(0, 1, 0);
    if (window.isKeyPressed(SDL_SCANCODE_LCTRL)) acc += Vec3f(0, -1, 0);
  } else if (onGround) {
    if (window.isKeyPressed(SDL_SCANCODE_SPACE)) velocity.y = 0.2f;
  }
  if (window.isKeyPressed(SDL_SCANCODE_TAB)) acc *= 256.0f;

  acc *= 0.1f;
  velocity += camera.transformedVelocity(acc, Vec3i(0, 1, 0));
}

auto fullscreenQuad(float width, float height, float size) -> VertexArray {
  auto wfrac = width / size, hfrac = height / size;
  return VertexArray(VertexLayout(OpenGL::triangleStrip, 2, 2))
    .texCoords({0.0f, hfrac})
    .vertex({-1.0f, 1.0f})
    .texCoords({0.0f, 0.0f})
    .vertex({-1.0f, -1.0f})
    .texCoords({wfrac, hfrac})
    .vertex({1.0f, 1.0f})
    .texCoords({wfrac, 0.0f})
    .vertex({1.0f, -1.0f});
}

auto average(std::vector<double> const& in, size_t size, size_t scale) -> std::vector<double> {
  assert(in.size() == size * size);
  auto res = std::vector<double>(size * size / scale / scale);
  for (auto i = 0_z; i < size; i += scale)
    for (auto j = 0_z; j < size; j += scale) {
      double avg = 0.0;
      for (auto k = 0_z; k < scale; k++)
        for (auto l = 0_z; l < scale; l++) {
          auto ik = i + k;
          auto jl = j + l;
          ik = (ik >= scale / 2) ? ik - scale / 2 : size + ik - scale / 2;
          jl = (jl >= scale / 2) ? jl - scale / 2 : size + jl - scale / 2;
          avg += in[ik * size + jl];
        }
      avg /= static_cast<double>(scale * scale);
      res[(i / scale) * (size / scale) + (j / scale)] = avg;
    }
  return res;
}

auto enlarge(std::vector<double> const& in, size_t size, size_t scale) -> std::vector<double> {
  assert(in.size() == size * size);
  auto res = std::vector<double>(size * size * scale * scale);
  for (auto i = 0_z; i < size * scale; i++)
    for (auto j = 0_z; j < size * scale; j++) {
      auto i0 = i / scale, j0 = j / scale;
      auto i1 = (i0 + 1) % size, j1 = (j0 + 1) % size;
      auto a00 = in[i0 * size + j0];
      auto a01 = in[i0 * size + j1];
      auto a10 = in[i1 * size + j0];
      auto a11 = in[i1 * size + j1];
      auto fi = static_cast<double>(i % scale) / static_cast<double>(scale);
      auto fj = static_cast<double>(j % scale) / static_cast<double>(scale);
      res[i * (size * scale) + j] = std::lerp(std::lerp(a00, a01, fj), std::lerp(a10, a11, fj), fi);
    }
  return res;
}

auto add(std::vector<double>& a, std::vector<double> const& b, size_t size) -> void {
  assert(a.size() == size * size);
  assert(b.size() == size * size);
  for (auto i = 0_z; i < size; i++)
    for (auto j = 0_z; j < size; j++) a[i * size + j] += b[i * size + j];
}

auto subtract(std::vector<double>& a, std::vector<double> const& b, size_t size) -> void {
  assert(a.size() == size * size);
  assert(b.size() == size * size);
  for (auto i = 0_z; i < size; i++)
    for (auto j = 0_z; j < size; j++) a[i * size + j] -= b[i * size + j];
}

auto test() -> void {
  auto bmp = Bitmap("in.bmp");
  assert(bmp.width() == 1024 && bmp.height() == 1024);

  auto size = bmp.width();
  auto img = std::vector<double>(size * size);

  for (auto i = 0_z; i < size; i++)
    for (auto j = 0_z; j < size; j++) img[i * size + j] = bmp.at(j, i, 0) / 255.0;

  constexpr auto levels = 10_z, limit = 6_z;
  auto arr = std::array<std::vector<double>, levels>();

  for (auto level = 0_z; level < levels; level++) {
    auto tmp = average(img, size, 1_z << level);
    auto currSize = size >> level;
    auto limitSize = 1_z << limit;
    assert(tmp.size() == currSize * currSize);

    if (currSize > limitSize) {
      for (auto i = 0_z; i < limitSize; i++)
        for (auto j = 0_z; j < limitSize; j++) {
          double avg = 0.0, cnt = 0.0;
          for (auto k = i; k < currSize; k += limitSize)
            for (auto l = j; l < currSize; l += limitSize) {
              avg += tmp[k * currSize + l];
              cnt++;
            }
          avg /= cnt;
          for (auto k = i; k < currSize; k += limitSize)
            for (auto l = j; l < currSize; l += limitSize) tmp[k * currSize + l] = avg;
        }
    }

    arr[level] = enlarge(tmp, currSize, 1_z << level);
    subtract(img, arr[level], size);
  }

  img = std::vector<double>(size * size, 0.0);
  for (auto level = 0_z; level < levels; level++) add(img, arr[level], size);

  for (auto i = 0_z; i < size; i++)
    for (auto j = 0_z; j < size; j++) {
      auto dvalue = img[i * size + j] * 255.0;
      auto value = static_cast<uint8_t>(std::max(std::min(dvalue, 255.0), 0.0));
      bmp.at(j, i, 0) = bmp.at(j, i, 1) = bmp.at(j, i, 2) = value;
    }

  bmp.save("out.bmp");
}

auto main() -> int {
  // test();
  // return 0;
  auto config = Config();
  config.load(configPath() + configFilename());

  auto const multisample = config.getOr("GL.Multisamples", 0_z);
  auto const forceMinimumVersion = config.getOr("GL.ForceMinimumVersion", 0) != 0;
  auto const debugContext = config.getOr("GL.Debugging", 0) != 0;

  auto const fov = config.getOr("Render.FieldOfView", 70.0f);
  auto const workgroupWidth = config.getOr("Render.WorkgroupWidth", 8_z);
  auto const workgroupHeight = config.getOr("Render.WorkgroupHeight", 8_z);
  auto const renderWidth = config.getOr("Render.RenderWidth", 0_z);
  auto const renderHeight = config.getOr("Render.RenderHeight", 0_z);

  auto const dynamicMode = config.getOr("World.Dynamic", 0) != 0;
  auto const maxNodes = config.getOr("World.Dynamic.MaxNodes", 268435454_z);
  auto const maxHeight = config.getOr("World.Static.MaxHeight", 256_z);
  auto const worldLevels = config.getOr("World.MaxLevels", 8_z);
  auto const noiseLevels = config.getOr("World.Dynamic.NoiseLevels", 8_z);
  auto const partialLevels = config.getOr("World.Dynamic.PartialLevels", 4_z);
  auto const lodQuality = config.getOr("World.Dynamic.LodQuality", 0.5f);

  auto& window = Window::singleton("", 852, 480, multisample, forceMinimumVersion, debugContext);
  auto& gl = window.gl();

  Log::info("Renderer: " + gl.getString(GL_RENDERER) + " [" + gl.getString(GL_VENDOR) + "]");
  Log::info("OpenGL version: " + gl.getString(GL_VERSION));

  // Load shaders.
  auto const basicShader = ShaderProgram({
    ShaderStage(OpenGL::vertexShader, shaderPath() + "Basic.vsh"),
    ShaderStage(OpenGL::fragmentShader, shaderPath() + "Basic.fsh"),
  });

  auto const mainShader = ShaderProgram({ShaderStage(OpenGL::computeShader, shaderPath() + "Main.csh")});
  auto const mainOutput = ShaderStorage(sizeof(MainOutputData));
  mainOutput.bindAt(mainOutputBufferIndex);

  auto const hitTestShader = ShaderProgram({ShaderStage(OpenGL::computeShader, shaderPath() + "HitTest.csh")});
  auto const hitTestOutput = ShaderStorage(sizeof(HitTestOutputData));
  hitTestOutput.bindAt(hitTestOutputBufferIndex);

  // Initialise voxels.
  auto const worldSize = 1_z << worldLevels;
  auto const noiseSize = 1_z << noiseLevels;
  auto treeBuffer = initTreeBuffer(dynamicMode, maxNodes, worldSize, maxHeight);
  treeBuffer.bindAt(treeBufferIndex);

  // Initialise noise.
  auto noiseImage = Bitmap(noiseSize, noiseSize, 4);
  for (size_t x = 0; x < noiseSize; x++)
    for (size_t y = 0; y < noiseSize; y++) {
      noiseImage.at(x, y, 2) = noiseImage.at(x, y, 0) = static_cast<uint8_t>(rand() % 256);
      noiseImage.at(x, y, 3) = noiseImage.at(x, y, 1) = static_cast<uint8_t>(rand() % 256);
    }
  auto const noiseTexture = Texture(noiseImage, 0);
  noiseTexture.bindAt(noiseTextureIndex);

  // Initialise noise bounds.
  auto maxImage = Bitmap(noiseSize, noiseSize, 4);
  auto minImage = Bitmap(noiseSize, noiseSize, 4);
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
  auto const maxTexture = Texture(loadNoiseMipmaps(maxImage, true));
  maxTexture.bindAt(maxTextureIndex);
  auto const minTexture = Texture(loadNoiseMipmaps(minImage, false));
  minTexture.bindAt(minTextureIndex);

  // Initialise (empty) frame textures.
  auto frameWidth = 0_z, frameHeight = 0_z, frameSize = 0_z;
  auto frame = Texture();
  frame.bindAt(frameTextureIndex);
  glBindImageTexture(frameImageIndex, frame.handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
  auto beams = std::array<Texture, beamLevels>();
  for (auto i = 0_z; i < beamLevels; i++) {
    glBindImageTexture(beamImageIndices[i], beams[i].handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
  }
  auto quad = VertexBuffer(fullscreenQuad(0.0f, 0.0f, 0.0f), true);

  // Camera parameters.
  auto camera = Camera();
  camera.fov = fov;
  camera.aspect = 1.0f; // Updated per frame.
  camera.near = 0.1f;
  camera.far = 256.0f;
  camera.position.x = static_cast<float>(worldSize) * static_cast<float>(rand()) / (RAND_MAX + 1.0f);
  camera.position.y = static_cast<float>(worldSize) + 2.0f;
  camera.position.z = static_cast<float>(worldSize) * static_cast<float>(rand()) / (RAND_MAX + 1.0f);

  auto cameraUpdateScheduler = UpdateScheduler(30.0);
  auto cameraVelocity = Vec3f(0);
  auto cameraAccY = 0.0f;
  auto cameraOnGround = false;
  auto cameraFlying = false;
  auto cameraCrossWall = false;

  auto frameCounterScheduler = UpdateScheduler(1.0);
  auto frameCounter = 0_z;

  auto startTime = UpdateScheduler::timeFromEpoch();
  auto pathTracing = false;
  window.setMouseLocked(true);

  // Main loop.
  while (!window.shouldQuit()) {
    window.pollEvents();

    // =====================
    // Handle window events.
    // =====================

    static bool lpressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_L)) {
      if (!lpressed) window.setMouseLocked(!window.mouseLocked());
      lpressed = true;
    } else {
      lpressed = false;
    }

    static bool ppressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_P)) {
      if (!ppressed) {
        pathTracing = !pathTracing;
        window.setMouseLocked(!pathTracing);
        frameCounter = 0;
      }
      ppressed = true;
    } else {
      ppressed = false;
    }

    static bool cpressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_C)) {
      if (!cpressed) {
        auto curr = Tree(1, 1);
        curr.download(treeBuffer);
        curr.check();
      }
      cpressed = true;
    } else {
      cpressed = false;
    }

    static bool gpressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_G)) {
      if (!gpressed) {
        auto curr = Tree(1, 1), opt = Tree(1, 1);
        curr.download(treeBuffer);
        curr.gc(opt);
        opt.upload(treeBuffer);
      }
      gpressed = true;
    } else {
      gpressed = false;
    }

    static bool f1pressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_F1)) {
      if (!f1pressed) cameraFlying = !cameraFlying;
      f1pressed = true;
    } else {
      f1pressed = false;
    }

    static bool f2pressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_F2)) {
      if (!f2pressed) window.setFullscreen(!window.fullscreen());
      f2pressed = true;
    } else {
      f2pressed = false;
    }

    static bool f4pressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_F4)) {
      if (!f4pressed) cameraCrossWall = !cameraCrossWall;
      f4pressed = true;
    } else {
      f4pressed = false;
    }

    // Screenshot.
    /*
    static bool opressed = false;
    if (window.isKeyPressed(SDL_SCANCODE_O)) {
      if (!opressed) {
        if (pathTracing) {
          // Read pixel data.
          auto bmp = Bitmap(frameWidth, frameHeight, 3);
          Texture::select(frameTextureIndices.back());
          glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, bmp.data());
          // Gamma conversion.
          auto const gamma = 2.2;
          for (auto i = 0_z; i < bmp.height(); i++)
            for (auto j = 0_z; j < bmp.width(); j++) {
              for (auto k = 0_z; k < bmp.bytesPerPixel(); k++) {
                auto col = bmp.at(j, i, k) / 255.0;
                col = pow(col, 1.0 / gamma);
                bmp.at(j, i, k) = static_cast<uint8_t>(col * 255.0);
              }
            }
          // Save file.
          std::stringstream ss;
          ss << screenshotPath() << frameWidth << "x" << frameHeight << "-" << frameCounter << "spp-"
             << UpdateScheduler::timeFromEpoch() - startTime << "s.bmp";
          bmp.swapChannels(0, 2);
          bmp.save(ss.str());
          Log::info("Saved screenshot `" + ss.str() + "`.");
        }
      }
      opressed = true;
    } else opressed = false;
    */

    // Update camera.
    if (!pathTracing) {
      updateCameraFrame(window, camera);
      cameraUpdateScheduler.refresh();
      while (!cameraUpdateScheduler.inSync()) {
        updateCameraInterval(
          window,
          camera,
          cameraVelocity,
          cameraAccY,
          cameraOnGround,
          cameraFlying || cameraCrossWall
        );
        if (!cameraCrossWall) {
          // Invoke the hit test program.
          auto const boxMin = camera.position - Vec3f(0.3f, 1.5f, 0.3f);
          auto const boxMax = camera.position + Vec3f(0.3f, 0.2f, 0.3f);
          hitTestShader.use();
          hitTestShader.uniformUInt("MaxLevels", static_cast<GLuint>(worldLevels));
          hitTestShader.uniformVec3("BoxMin", boxMin.x, boxMin.y, boxMin.z);
          hitTestShader.uniformVec3("BoxMax", boxMax.x, boxMax.y, boxMax.z);
          hitTestShader.uniformVec3("Velocity", cameraVelocity.x, cameraVelocity.y, cameraVelocity.z);
          // glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
          glDispatchCompute(1, 1, 1);
          glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
          auto data = HitTestOutputData();
          hitTestOutput.download(0, sizeof(HitTestOutputData), &data);
          cameraVelocity = Vec3f(data.velocity[0], data.velocity[1], data.velocity[2]);
          cameraOnGround = data.onGround;
        }
        cameraUpdateScheduler.increase();
      }
    } else {
      cameraUpdateScheduler.sync();
    }

    // Update FPS.
    frameCounter++;
    frameCounterScheduler.refresh();
    while (!frameCounterScheduler.inSync()) {
      // Count nodes.
      glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
      auto data = MainOutputData();
      mainOutput.download(0, sizeof(MainOutputData), &data);
      // Update window title.
      std::stringstream ss;
      ss << "Voxel Raycasting Test (";
      if (dynamicMode) {
        ss << data.count << " (" << static_cast<size_t>(data.count) * 100 / maxNodes << "%) nodes dynamic";
      } else {
        ss << data.count << " nodes static";
      }
      if (pathTracing) {
        ss << ", " << frameCounter << " samples per pixel";
      } else {
        ss << ", FPS: " << frameCounter << ", X: " << camera.position.x << ", Y: " << camera.position.y
           << ", Z: " << camera.position.z;
        frameCounter = 0;
      }
      ss << ")";
      Window::singleton().setTitle(ss.str());
      frameCounterScheduler.increase();
    }

    // Exit program if ESC is pressed.
    if (window.isKeyPressed(SDL_SCANCODE_ESCAPE)) break;

    // =============
    // Render frame.
    // =============

    // Check if frame textures should be resized.
    if (!pathTracing) {
      auto const width = renderWidth > 0 ? renderWidth : window.width();
      auto const height = renderHeight > 0 ? renderHeight : window.height();
      if (frameWidth != width || frameHeight != height) {
        frameWidth = width;
        frameHeight = height;
        frameSize = 1_z << ceilLog2(std::max(width, height));
        frame.reallocate(frameSize, OpenGL::internalFormat4f);
        for (auto i = 0_z; i < beamLevels; i++) {
          beams[i].reallocate(frameSize / beamSizes[i], OpenGL::internalFormat4f);
        }
        quad = VertexBuffer(
          fullscreenQuad(
            static_cast<float>(frameWidth),
            static_cast<float>(frameHeight),
            static_cast<float>(frameSize)
          ),
          true
        );
        camera.aspect = static_cast<float>(frameWidth) / static_cast<float>(frameHeight);
      }
    }

    auto interp = camera;
    interp.position += cameraVelocity * static_cast<float>(std::min(cameraUpdateScheduler.delta(), 1.0));

    // Initialise shaders.
    mainShader.use();
    mainShader.uniformImage("FrameImage", frameImageIndex);
    mainShader.uniformImages("BeamImage", beamLevels, beamImageIndices.data());
    mainShader.uniformSampler("NoiseTexture", noiseTextureIndex);
    mainShader.uniformSampler("MaxTexture", maxTextureIndex);
    mainShader.uniformSampler("MinTexture", minTextureIndex);

    mainShader.uniformUInt("FrameWidth", static_cast<GLuint>(frameWidth));
    mainShader.uniformUInt("FrameHeight", static_cast<GLuint>(frameHeight));
    mainShader.uniformBool("PathTracing", pathTracing);
    mainShader.uniformBool("ProfilerOn", window.isKeyPressed(SDL_SCANCODE_M));

    // See: https://en.cppreference.com/w/cpp/language/lifetime
    // mainShader.uniformMat4("ProjectionMatrix", interp.projection().data());
    // mainShader.uniformMat4("ModelViewMatrix", interp.modelView().data());
    mainShader.uniformMat4("ProjectionInverse", interp.projection().inverted().data());
    mainShader.uniformMat4("ModelViewInverse", interp.modelView().inverted().data());
    mainShader.uniformVec3("CameraPosition", interp.position.x, interp.position.y, interp.position.z);
    mainShader.uniformFloat("CameraFov", interp.fov * 3.14159265f / 180.0f);
    // mainShader.uniformFloat("Time", static_cast<float>(UpdateScheduler::timeFromEpoch() - startTime));
    mainShader.uniformFloat("RandomSeed", static_cast<float>(UpdateScheduler::timeFromEpoch() - startTime));

    mainShader.uniformBool("DynamicMode", dynamicMode);
    mainShader.uniformUInt("MaxNodes", static_cast<GLuint>(maxNodes));
    mainShader.uniformUInt("MaxLevels", static_cast<GLuint>(worldLevels));
    mainShader.uniformUInt("NoiseLevels", static_cast<GLuint>(noiseLevels));
    mainShader.uniformUInt("PartialLevels", static_cast<GLuint>(partialLevels));
    mainShader.uniformFloat("LodQuality", lodQuality);

    // See: https://www.khronos.org/opengl/wiki/Memory_Model#External_visibility
    auto barriers = GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT;

    // Render scene, coarse to fine.
    mainShader.uniformUInt("PrevBeamIndex", beamLevels);
    mainShader.uniformUInt("PrevBeamSize", 1);
    for (auto i = 0_z; i < beamLevels; i++) {
      auto const beamSize = beamSizes[i];
      auto const currWidth = frameWidth / beamSize + 1;
      auto const currHeight = frameHeight / beamSize + 1;
      mainShader.uniformUInt("CurrBeamIndex", static_cast<GLuint>(i));
      mainShader.uniformUInt("CurrBeamSize", static_cast<GLuint>(beamSize));
      glMemoryBarrier(barriers);
      glDispatchCompute((currWidth - 1) / workgroupWidth + 1, (currHeight - 1) / workgroupHeight + 1, 1);
      mainShader.uniformUInt("PrevBeamIndex", static_cast<GLuint>(i));
      mainShader.uniformUInt("PrevBeamSize", static_cast<GLuint>(beamSize));
    }
    mainShader.uniformUInt("CurrBeamIndex", beamLevels);
    mainShader.uniformUInt("CurrBeamSize", 1);
    glMemoryBarrier(barriers);
    glDispatchCompute((frameWidth - 1) / workgroupWidth + 1, (frameHeight - 1) / workgroupHeight + 1, 1);

    gl.setDrawArea(0, 0, window.width(), window.height());
    gl.clear();

    // Present to screen.
    basicShader.use();
    basicShader.uniformSampler("Texture2D", frameTextureIndex);
    basicShader.uniformBool("Texture2DEnabled", true);
    basicShader.uniformBool("ColorEnabled", false);
    basicShader.uniformBool("GammaConversion", true);
    glMemoryBarrier(barriers);
    quad.draw();

    window.swapBuffers();
    gl.checkError();
  }

  config.save(configPath() + configFilename());
  return 0;
}

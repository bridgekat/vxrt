#include "tree.h"
#include <cassert>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "bitmap.h"
#include "common.h"
#include "log.h"
#include "worldgen.h"

uint32_t Tree::nodeAt(size_t x, size_t y, size_t z) {
  return static_cast<int64_t>(y) < mHeightMap[x * mSize + z] ? 1u : 0u;
  /*
  auto x0 = 0_z, y0 = 0_z, z0 = 0_z, size = mSize;
  auto ind = 0_z;
  while (!mNodes[ind].leaf) {
    assert(size > 1);
    auto xm = x0 + size / 2, ym = y0 + size / 2, zm = z0 + size / 2;
    ind = mNodes[ind].data;
    if (x >= xm) x = xm, ind += 1;
    if (y >= ym) y = ym, ind += 2;
    if (z >= zm) z = zm, ind += 4;
    size /= 2;
  }
  return mNodes[ind].data;
  */
}

void Tree::generate() {
  Log::info("Generating terrain height...");
  // auto bmp = Bitmap("out.bmp");
  // assert(bmp.width() == mSize && bmp.height() == mSize);
  for (auto x = 0_z; x < mSize; x++) {
    for (auto z = 0_z; z < mSize; z++) {
      auto dx = static_cast<double>(x), dz = static_cast<double>(z);
      mHeightMap[x * mSize + z] = WorldGen::getHeight(dx, dz) + 64;
      // mHeightMap[x * mSize + z] = bmp.at(x, z, 0) / 4;
    }
  }
  Log::info("Generating tree...");
  mNodes.resize(1);
  mBlocksGenerated = 0;
  generateNode(0, 0, 0, 0, mSize);
}

void Tree::upload(ShaderStorage& ssbo) {
  Log::info("Uploading tree data...");

  assert(ssbo.size() >= uploadSize());
  uint32_t nodeCount = static_cast<uint32_t>(mNodes.size());
  ssbo.upload(0, sizeof(uint32_t), &nodeCount);
  ssbo.upload(sizeof(uint32_t), mNodes.size() * sizeof(uint32_t), mNodes.data());

  std::stringstream ss;
  ss << nodeCount << " nodes uploaded.";
  Log::info(ss.str());
}

void Tree::download(ShaderStorage& ssbo) {
  Log::info("Downloading tree data...");
  uint32_t nodeCount = 0;
  ssbo.download(0, sizeof(uint32_t), &nodeCount);
  mNodes.resize(nodeCount);
  ssbo.download(sizeof(uint32_t), sizeof(uint32_t) * nodeCount, mNodes.data());
  std::stringstream ss;
  ss << nodeCount << " nodes downloaded.";
  Log::info(ss.str());
}

int32_t Tree::dfs(size_t ind, size_t& count, size_t& redundant) {
  count++;
  if (!mNodes[ind].generated) return -1;
  if (mNodes[ind].leaf) return mNodes[ind].data;
  bool re = true;
  int c0 = 0;
  for (size_t i = 0; i < 8; i++) {
    auto curr = dfs(mNodes[ind].data + i, count, redundant);
    if (i == 0) c0 = curr;
    if (curr < 0 || curr != c0) re = false;
  }
  if (re) redundant++;
  return re ? c0 : -1;
}

void Tree::check() {
  Log::info("Checking tree...");
  size_t count = 0, redundant = 0;
  dfs(0, count, redundant);
  std::stringstream ss;
  ss.str("");
  ss << "Allocated nodes: " << mNodes.size();
  Log::info(ss.str());
  ss.str("");
  ss << "Reachable nodes: " << count;
  Log::info(ss.str());
  ss.str("");
  ss << "Redundant nodes: " << redundant << " (" << redundant * 100 / count << "%)";
  Log::info(ss.str());
}

// Returns true if node is merged to a single leaf.
bool Tree::gcdfs(Node const& node, Node& other, Tree& res) {
  other = node;
  if (!node.generated) return false;
  if (node.leaf) return true;
  // Allocate children for `other`.
  other.data = static_cast<uint32_t>(res.mNodes.size());
  res.mNodes.resize(other.data + 8);
  // Mid: `node` and `other` are intermediate.
  bool mergeable = true;
  for (size_t i = 0; i < 8; i++) {
    bool f = gcdfs(mNodes[node.data + i], res.mNodes[other.data + i], res);
    if (!f || res.mNodes[other.data + i].data != res.mNodes[other.data].data) mergeable = false;
  }
  if (mergeable) {
    assert(res.mNodes.size() == other.data + 8);
    auto data = res.mNodes[other.data].data;
    res.mNodes.resize(other.data);
    other.leaf = true;
    other.data = data;
  }
  return mergeable;
}

void Tree::gc(Tree& res) {
  Log::info("Optimizing tree...");
  res.mNodes.reserve(mNodes.size());
  res.mNodes.push_back(Node());
  gcdfs(mNodes[0], res.mNodes[0], res);
  // res.check();
}

void Tree::generateNode(size_t ind, size_t x0, size_t y0, size_t z0, size_t size) {
  assert(size >= 1 && ind < mNodes.size());
  assert(0 <= x0 && x0 < mSize);
  assert(0 <= y0 && y0 < mSize);
  assert(0 <= z0 && z0 < mSize);
  mNodes[ind].generated = true;
  if (size == 1) {
    // Generate single block
    // auto dx0 = static_cast<double>(x0), dy0 = static_cast<double>(y0), dz0 = static_cast<double>(z0);
    // auto sx0 = static_cast<int64_t>(x0), sy0 = static_cast<int64_t>(y0), sz0 = static_cast<int64_t>(z0);
    // double density = WorldGen::getDensity(dx0, dy0, dz0);
    // mNodes[ind].data = WorldGen::getBlock(sx0, sy0, sz0, mHeightMap[x0 * mSize + z0], density) ? 1 : 0;
    mNodes[ind].data = static_cast<int64_t>(y0) < mHeightMap[x0 * mSize + z0] ? 1 : 0;
    mNodes[ind].leaf = true;
    // Count
    mBlocksGenerated++;
    if (mBlocksGenerated % 10000000 == 0) {
      size_t percent = mBlocksGenerated * 100 / (mSize * mSize * mHeight);
      std::stringstream ss;
      ss << mBlocksGenerated << " (" << percent << "%) blocks generated, ";
      ss << mNodes.size() << " nodes used.";
      Log::verbose(ss.str());
    }
    return;
  }
  if (y0 >= mHeight) {
    mNodes[ind].data = 0;
    mNodes[ind].leaf = true;
    return;
  }
  auto cptr = mNodes.size();
  auto half = size / 2;
  assert(cptr < (1u << 30));
  mNodes[ind].data = static_cast<uint32_t>(cptr);
  mNodes[ind].leaf = false;
  mNodes.resize(cptr + 8);
  generateNode(cptr + 0, x0, y0, z0, half);
  generateNode(cptr + 1, x0 + half, y0, z0, half);
  generateNode(cptr + 2, x0, y0 + half, z0, half);
  generateNode(cptr + 3, x0 + half, y0 + half, z0, half);
  generateNode(cptr + 4, x0, y0, z0 + half, half);
  generateNode(cptr + 5, x0 + half, y0, z0 + half, half);
  generateNode(cptr + 6, x0, y0 + half, z0 + half, half);
  generateNode(cptr + 7, x0 + half, y0 + half, z0 + half, half);
  if (mNodes.size() == cptr + 8) {
    bool f = true;
    for (auto i = cptr + 0; i < cptr + 8; i++) {
      assert(mNodes[i].leaf);
      if (mNodes[i].data != mNodes[cptr].data) {
        f = false;
        break;
      }
    }
    if (f) {
      mNodes[ind].leaf = true;
      mNodes[ind].data = mNodes[cptr].data;
      mNodes.resize(mNodes.size() - 8);
    }
  }
}

void addPoint(size_t i, size_t x, size_t y, size_t z, std::vector<uint16_t>& buffer) {
  buffer.push_back(static_cast<uint16_t>(x));
  buffer.push_back(static_cast<uint16_t>(y));
  buffer.push_back(static_cast<uint16_t>(z));
  buffer.push_back(static_cast<uint16_t>(i));
}

auto Tree::pointMesh() -> std::pair<OpenGL::Object, size_t> {
  auto buffer = std::vector<uint16_t>();
  for (auto x = 0_z; x < mSize; x++) {
    for (auto y = 0_z; y < mHeight; y++) {
      for (auto z = 0_z; z < mSize; z++) {
        if (nodeAt(x, y, z) == 0u) continue;
        if (x > 0 && nodeAt(x - 1, y, z) == 0u) addPoint(0, x, y, z, buffer);         // Left.
        if (x < mSize - 1 && nodeAt(x + 1, y, z) == 0u) addPoint(1, x, y, z, buffer); // Right.
        if (y > 0 && nodeAt(x, y - 1, z) == 0u) addPoint(2, x, y, z, buffer);         // Bottom.
        if (y < mSize - 1 && nodeAt(x, y + 1, z) == 0u) addPoint(3, x, y, z, buffer); // Top.
        if (z > 0 && nodeAt(x, y, z - 1) == 0u) addPoint(4, x, y, z, buffer);         // Back.
        if (z < mSize - 1 && nodeAt(x, y, z + 1) == 0u) addPoint(5, x, y, z, buffer); // Front.
      }
    }
  }

  // Create and populate vertex buffers.
  auto vbo = OpenGL::Object{};
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(uint16_t), buffer.data(), GL_STATIC_DRAW);

  // Specify vertex attribute arrays.
  auto vao = OpenGL::Object{};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, 4 * sizeof(uint16_t), static_cast<uint16_t*>(nullptr));
  glEnableVertexAttribArray(0);
  glVertexAttribIPointer(1, 1, GL_UNSIGNED_SHORT, 4 * sizeof(uint16_t), static_cast<uint16_t*>(nullptr) + 3);
  glEnableVertexAttribArray(1);

  return {vao, buffer.size() / 4};
}

// See: https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
template <class T>
inline void hash_combine(size_t& seed, T const& v) noexcept {
  std::hash<T> hasher{};
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
}

// See: https://en.cppreference.com/w/cpp/utility/hash
template <>
struct std::hash<Vec3u> {
  size_t operator()(Vec3u const& s) const noexcept {
    size_t res = 0;
    hash_combine(res, s.x);
    hash_combine(res, s.y);
    hash_combine(res, s.z);
    return res;
  }
};

void makeVertex(
  size_t i,
  size_t x,
  size_t y,
  size_t z,
  std::vector<uint16_t>& buffer,
  std::vector<uint32_t>& indices,
  std::unordered_map<Vec3u, uint32_t>& map
) {
  // TEMP CODE
  auto coords = Vec3u(static_cast<unsigned int>(x), static_cast<unsigned int>(y), static_cast<unsigned int>(z * 6 + i));
  if (auto it = map.find(coords); it != map.end()) {
    indices.push_back(it->second);
    return;
  }
  auto index = static_cast<uint32_t>(buffer.size() / 4);
  map[coords] = index;
  buffer.push_back(static_cast<uint16_t>(x));
  buffer.push_back(static_cast<uint16_t>(y));
  buffer.push_back(static_cast<uint16_t>(z));
  buffer.push_back(static_cast<uint16_t>(i));
  indices.push_back(index);
}

std::pair<OpenGL::Object, size_t> Tree::triangleMesh() {
  auto buffer = std::vector<uint16_t>();
  auto indices = std::vector<uint32_t>();
  auto map = std::unordered_map<Vec3u, uint32_t>();
  for (auto x = 0_z; x < mSize; x++) {
    for (auto y = 0_z; y < mHeight; y++) {
      for (auto z = 0_z; z < mSize; z++) {
        if (nodeAt(x, y, z) == 0u) continue;
        // Left.
        if (x > 0 && nodeAt(x - 1, y, z) == 0u) {
          makeVertex(0, x, y, z, buffer, indices, map);
          makeVertex(0, x, y, z + 1, buffer, indices, map);
          makeVertex(0, x, y + 1, z + 1, buffer, indices, map);
          makeVertex(0, x, y, z, buffer, indices, map);
          makeVertex(0, x, y + 1, z + 1, buffer, indices, map);
          makeVertex(0, x, y + 1, z, buffer, indices, map);
        }
        // Right.
        if (x < mSize - 1 && nodeAt(x + 1, y, z) == 0u) {
          makeVertex(1, x + 1, y, z, buffer, indices, map);
          makeVertex(1, x + 1, y + 1, z, buffer, indices, map);
          makeVertex(1, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(1, x + 1, y, z, buffer, indices, map);
          makeVertex(1, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(1, x + 1, y, z + 1, buffer, indices, map);
        }
        // Bottom.
        if (y > 0 && nodeAt(x, y - 1, z) == 0u) {
          makeVertex(2, x, y, z, buffer, indices, map);
          makeVertex(2, x + 1, y, z, buffer, indices, map);
          makeVertex(2, x + 1, y, z + 1, buffer, indices, map);
          makeVertex(2, x, y, z, buffer, indices, map);
          makeVertex(2, x + 1, y, z + 1, buffer, indices, map);
          makeVertex(2, x, y, z + 1, buffer, indices, map);
        }
        // Top.
        if (y < mSize - 1 && nodeAt(x, y + 1, z) == 0u) {
          makeVertex(3, x, y + 1, z, buffer, indices, map);
          makeVertex(3, x, y + 1, z + 1, buffer, indices, map);
          makeVertex(3, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(3, x, y + 1, z, buffer, indices, map);
          makeVertex(3, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(3, x + 1, y + 1, z, buffer, indices, map);
        }
        // Back.
        if (z > 0 && nodeAt(x, y, z - 1) == 0u) {
          makeVertex(4, x, y, z, buffer, indices, map);
          makeVertex(4, x, y + 1, z, buffer, indices, map);
          makeVertex(4, x + 1, y + 1, z, buffer, indices, map);
          makeVertex(4, x, y, z, buffer, indices, map);
          makeVertex(4, x + 1, y + 1, z, buffer, indices, map);
          makeVertex(4, x + 1, y, z, buffer, indices, map);
        }
        // Front.
        if (z < mSize - 1 && nodeAt(x, y, z + 1) == 0u) {
          makeVertex(5, x, y, z + 1, buffer, indices, map);
          makeVertex(5, x + 1, y, z + 1, buffer, indices, map);
          makeVertex(5, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(5, x, y, z + 1, buffer, indices, map);
          makeVertex(5, x + 1, y + 1, z + 1, buffer, indices, map);
          makeVertex(5, x, y + 1, z + 1, buffer, indices, map);
        }
      }
    }
  }

  // Create and populate vertex buffers.
  auto vbo = OpenGL::Object{};
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(uint16_t), buffer.data(), GL_STATIC_DRAW);

  // Specify vertex attribute arrays.
  auto vao = OpenGL::Object{};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, 4 * sizeof(uint16_t), static_cast<uint16_t*>(nullptr));
  glEnableVertexAttribArray(0);
  glVertexAttribIPointer(1, 1, GL_UNSIGNED_SHORT, 4 * sizeof(uint16_t), static_cast<uint16_t*>(nullptr) + 3);
  glEnableVertexAttribArray(1);

  // Create and populate index buffers.
  auto ebo = OpenGL::Object{};
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

  return {vao, indices.size()};
}

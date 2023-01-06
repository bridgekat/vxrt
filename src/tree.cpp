#include "tree.h"
#include <cassert>
#include <sstream>
#include <vector>
#include "log.h"
#include "worldgen.h"

void Tree::generate() {
  Log::info("Generating terrain height...");
  for (int x = 0; x < mSize; x++)
    for (int z = 0; z < mSize; z++) { mHeightMap[x * mSize + z] = WorldGen::getHeight(x, z) + 64; }
  Log::info("Generating tree...");
  mNodes.resize(1);
  mBlocksGenerated = 0;
  generateNode(0, 0, 0, 0, mSize);
}

void Tree::upload(ShaderStorage& ssbo) {
  Log::info("Uploading tree data...");

  assert(ssbo.size() >= uploadSize());
  uint32_t nodeCount = mNodes.size();
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

int Tree::dfs(size_t ind, size_t& count, size_t& redundant) {
  count++;
  if (!mNodes[ind].generated) return -1;
  if (mNodes[ind].leaf) return mNodes[ind].data;
  bool re = true;
  int c0 = 0;
  for (size_t i = 0; i < 8; i++) {
    int curr = dfs(mNodes[ind].data + i, count, redundant);
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
  other.data = res.mNodes.size();
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

void Tree::generateNode(size_t ind, int x0, int y0, int z0, int size) {
  assert(size >= 1 && ind < mNodes.size());
  mNodes[ind].generated = true;
  if (size == 1) {
    // Generate single block
    // double density = WorldGen::getDensity(x0, y0, z0);
    // mNodes[ind].data = WorldGen::getBlock(x0, y0, z0, mHeightMap[x0 * mSize + z0], density);
    mNodes[ind].data = y0 < mHeightMap[x0 * mSize + z0] ? 1 : 0;
    mNodes[ind].leaf = true;
    // Count
    mBlocksGenerated++;
    if (mBlocksGenerated % 10000000 == 0) {
      auto percent = mBlocksGenerated * 100 / (mSize * mSize * mHeight);
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
  mNodes[ind].leaf = false;
  int cptr = mNodes[ind].data = mNodes.size();
  int half = size / 2;
  mNodes.resize(mNodes.size() + 8);
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
    for (int i = cptr + 0; i < cptr + 8; i++) {
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

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

void Tree::upload(ShaderStorageBuffer& ssbo, bool allocate) {
  Log::info("Uploading tree data...");
  if (allocate) ssbo.allocateImmutable((mNodes.size() + 1) * sizeof(unsigned int), true);
  unsigned int nodeCount = mNodes.size();
  ssbo.uploadSubData(0, sizeof(unsigned int), &nodeCount);
  ssbo.uploadSubData(sizeof(unsigned int), mNodes.size() * sizeof(unsigned int), mNodes.data());
  std::stringstream ss;
  ss << nodeCount << " nodes uploaded.";
  Log::info(ss.str());
}

void Tree::download(ShaderStorageBuffer& ssbo) {
  Log::info("Downloading tree data...");
  unsigned int nodeCount = 0;
  ssbo.downloadSubData(0, sizeof(unsigned int), &nodeCount);
  mNodes.resize(nodeCount);
  ssbo.downloadSubData(sizeof(unsigned int), sizeof(unsigned int) * nodeCount, mNodes.data());
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
  ss << "Valid nodes: " << count;
  Log::info(ss.str());
  ss.str("");
  ss << "Redundant nodes: " << redundant << " (" << redundant * 100 / count << "%)";
  Log::info(ss.str());
}

int Tree::gcdfs(size_t ind, size_t rind, Tree& res) {
  assert(rind < res.mNodes.size());
  res.mNodes[rind] = mNodes[ind];
  if (!mNodes[ind].generated) return -1;
  if (mNodes[ind].leaf) return mNodes[ind].data;
  bool re = true;
  int c0 = 0;
  size_t children = res.mNodes.size();
  res.mNodes[rind].data = children;
  res.mNodes.resize(children + 8);
  for (size_t i = 0; i < 8; i++) {
    int curr = gcdfs(mNodes[ind].data + i, children + i, res);
    if (i == 0) c0 = curr;
    if (curr < 0 || curr != c0) re = false;
  }
  if (re) {
    assert(res.mNodes.size() == children + 8);
    res.mNodes[rind].leaf = true;
    res.mNodes[rind].data = c0;
    res.mNodes.resize(children);
  }
  return re ? c0 : -1;
}

void Tree::gc(Tree& res) {
  Log::info("Optimizing tree...");
  res.mNodes.push_back(TreeNode());
  gcdfs(0, 0, res);
  //	res.check();
}

void Tree::generateNode(size_t ind, int x0, int y0, int z0, int size) {
  assert(size >= 1 && ind < mNodes.size());
  mNodes[ind].generated = true;
  if (size == 1) {
    // Generate single block
    double density = WorldGen::getDensity(x0, y0, z0);
    mNodes[ind].data = WorldGen::getBlock(x0, y0, z0, mHeightMap[x0 * mSize + z0], density);
    mNodes[ind].leaf = true;
    // Count
    mBlocksGenerated++;
    if (mBlocksGenerated % 10000000 == 0) {
      int percent = mBlocksGenerated * 100 / ((long long) mSize * mSize * mHeight);
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

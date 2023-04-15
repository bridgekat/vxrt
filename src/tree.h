#ifndef TREE_H_
#define TREE_H_

#include <cstdint>
#include <utility>
#include <vector>
#include "shaderstorage.h"
#include "vertexarray.h"

// TODO: arrange
class Tree {
public:
  struct Node {
    uint32_t generated: 1;
    uint32_t leaf: 1;
    uint32_t data: 30;
  };

  Tree(size_t size, size_t height):
    mSize(size),
    mHeight(height),
    mHeightMap(size * size) {}

  size_t size() { return mSize; }
  uint32_t nodeAt(size_t x, size_t y, size_t z);
  void generate();
  size_t uploadSize() { return (mNodes.size() + 1) * sizeof(uint32_t); };
  void upload(ShaderStorage& ssbo);
  void download(ShaderStorage& ssbo);
  void check();
  void gc(Tree& res);
  std::pair<OpenGL::Object, size_t> pointMesh();
  std::pair<OpenGL::Object, size_t> triangleMesh();

private:
  std::vector<Node> mNodes;
  size_t mSize, mHeight, mBlocksGenerated;
  std::vector<int64_t> mHeightMap;

  void generateNode(size_t ind, size_t x0, size_t y0, size_t z0, size_t size);
  int32_t dfs(size_t ind, size_t& count, size_t& redundant);
  bool gcdfs(Node const& node, Node& other, Tree& res);
};

#endif // TREE_H_

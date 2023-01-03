#ifndef TREE_H_
#define TREE_H_

#include <cstdint>
#include <vector>
#include "shaderstorage.h"

// TODO: arrange
class Tree {
public:
  struct Node {
    uint32_t generated: 1;
    uint32_t leaf: 1;
    uint32_t data: 30;
  };

  Tree(int size, int height): mSize(size), mHeight(height), mHeightMap(new int[size * size]) {}
  ~Tree() { delete[] mHeightMap; }

  int size() { return mSize; }
  void generate();
  size_t uploadSize() { return (mNodes.size() + 1) * sizeof(uint32_t); };
  void upload(ShaderStorage& ssbo);
  void download(ShaderStorage& ssbo);
  void check();
  void gc(Tree& res);

private:
  std::vector<Node> mNodes;
  int mSize, mHeight;
  long long mBlocksGenerated;
  int* mHeightMap;

  void generateNode(size_t ind, int x0, int y0, int z0, int size);
  int dfs(size_t ind, size_t& count, size_t& redundant);
  bool gcdfs(Node const& node, Node& other, Tree& res);
};

#endif // TREE_H_

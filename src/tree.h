#ifndef TREE_H_
#define TREE_H_

#include <vector>
#include "shaderbuffer.h"

class Tree {
public:
	struct TreeNode {
		unsigned int generated : 1;
		unsigned int leaf : 1;
		unsigned int data : 30;
	};
	
	Tree(int size, int height):
		mSize(size), mHeight(height), mHeightMap(new int[size * size]) {}
	
	~Tree() {
		delete[] mHeightMap;
	}
	
	int size() { return mSize; }
	void generate();
	void upload(ShaderBuffer& ssbo, bool allocate);
	void download(ShaderBuffer& ssbo);
	void check();
	void gc(Tree& res);

private:
	std::vector<TreeNode> mNodes;
	int mSize, mHeight;
	long long mBlocksGenerated;
	int* mHeightMap;
	
	void generateNode(size_t ind, int x0, int y0, int z0, int size);
	int dfs(size_t ind, size_t& count, size_t& redundant);
	int gcdfs(size_t ind, size_t rind, Tree& res);
};

#endif


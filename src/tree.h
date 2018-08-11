#ifndef TREE_H_
#define TREE_H_

#include <vector>
#include "shaderbuffer.h"

class Tree {
public:
	struct TreeNode {
		bool data;
		bool leaf;
		unsigned int children;
	};
	
	Tree(int size, int height):
		mNodeCount(0), mSize(size), mHeight(height), mHeightMap(new int[size * size]) {}
	
	~Tree() {
		delete[] mHeightMap;
	}
	
	int size() { return mSize; }
	void generate();
	void upload(ShaderBuffer& ssbo);
	
private:
	std::vector<TreeNode> mNodes;
	int mNodeCount, mSize, mHeight;
	long long mBlocksGenerated;
	int* mHeightMap;
	
	void generateNode(size_t ind, int x0, int y0, int z0, int size);
};

#endif


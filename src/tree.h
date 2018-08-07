#ifndef TREE_H_
#define TREE_H_

#include "shaderbuffer.h"

class Tree {
public:
	struct TreeNode {
		bool data;
		bool leaf;
		unsigned int children;
	};
	
	Tree(int size = 256, unsigned int maxNodes = 16777216):
		mSize(size), mNodes(new TreeNode[maxNodes]), mHeightMap(new int[size * size]) {}
	
	~Tree() {
		delete[] mNodes;
		delete[] mHeightMap;
	}
	
	int size() { return mSize; }
	void generate();
	void upload(ShaderBuffer& ssbo);
	
private:
	TreeNode* mNodes;
	int mNodeCount, mNodesGenerated, mSize;
	int* mHeightMap;
	
	void generateNode(int ind, TreeNode* arr, int x0, int y0, int z0, int size);
};

#endif


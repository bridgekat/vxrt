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
	
	Tree(unsigned int maxNodes = 16777216): mNodes(new TreeNode[maxNodes]) {
		
	}
	
	void generate();
	void update(ShaderBuffer& ssbo);
	
private:
	TreeNode* mNodes;
	int mNodeCount, mNodesGenerated;
	
	void generateNode(int ind, TreeNode* arr, int x0, int y0, int z0, int size);
};

#endif


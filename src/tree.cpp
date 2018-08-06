#include "tree.h"

#include <vector>
#include <sstream>
#include "worldgen.h"
#include "debug.h"
#include "logger.h"

const int Size = 256;
int height[Size][Size];

void Tree::generate() {
	LogInfo("Generating terrain height...");
	for (int x = 0; x < Size; x++) {
		for (int z = 0; z < Size; z++) {
			height[x][z] = WorldGen::getHeight(x, z) + 64;
		}
	}
	LogInfo("Generating tree...");
	mNodeCount = 1;
	mNodesGenerated = 0;
	generateNode(0, mNodes, 0, 0, 0, Size);
}

void Tree::update(ShaderBuffer& ssbo){
	LogInfo("Uploading tree data...");
	unsigned int* buffer = new unsigned int[mNodeCount + 1];
	buffer[0] = 0u;
	for (int i = 0; i < mNodeCount; i++) {
		buffer[i + 1] = ((int(mNodes[i].data)) + (int(mNodes[i].leaf) << 1) + (mNodes[i].children << 2));
	}
	ssbo.update((mNodeCount + 1) * sizeof(unsigned int), (void*)buffer);
	delete[] buffer;
	std::stringstream ss; ss << mNodeCount << " nodes uploaded."; LogInfo(ss.str());
}

void Tree::generateNode(int ind, TreeNode* arr, int x0, int y0, int z0, int size) {
	Assert(size >= 1);
	mNodesGenerated++;
	if (mNodesGenerated % 1000000 == 0) {
		std::stringstream ss;
		ss << mNodesGenerated << " nodes generated, " << mNodeCount << " indices used.";
		LogInfo(ss.str());
	}
	if (size == 1) {
		arr[ind].data = (height[x0][z0] >= y0);
		arr[ind].leaf = true;
		return;
	}
	arr[ind].leaf = false;
	int cptr = arr[ind].children = mNodeCount;
	int half = size / 2;
	mNodeCount += 8;
	generateNode(cptr + 0, arr, x0, y0, z0, half);
	generateNode(cptr + 1, arr, x0 + half, y0, z0, half);
	generateNode(cptr + 2, arr, x0, y0 + half, z0, half);
	generateNode(cptr + 3, arr, x0 + half, y0 + half, z0, half);
	generateNode(cptr + 4, arr, x0, y0, z0 + half, half);
	generateNode(cptr + 5, arr, x0 + half, y0, z0 + half, half);
	generateNode(cptr + 6, arr, x0, y0 + half, z0 + half, half);
	generateNode(cptr + 7, arr, x0 + half, y0 + half, z0 + half, half);
	if (mNodeCount == cptr + 8) {
		bool f = true;
		for (int i = cptr + 1; i < cptr + 8; i++) {
			if (arr[i].data != arr[cptr].data) {
				f = false;
				break;
			}
		}
		if (f) {
			arr[ind].leaf = true;
			arr[ind].data = arr[cptr].data;
			mNodeCount -= 8;
		}
	}
}


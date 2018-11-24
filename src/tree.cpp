#include "tree.h"

#include <vector>
#include <sstream>
#include "worldgen.h"
#include "debug.h"
#include "logger.h"

void Tree::generate() {
	LogInfo("Generating terrain height...");
	for (int x = 0; x < mSize; x++) for (int z = 0; z < mSize; z++) {
		mHeightMap[x * mSize + z] = WorldGen::getHeight(x, z) + 64;
	}
	LogInfo("Generating tree...");
	mNodeCount = 1;
	mBlocksGenerated = 0;
	generateNode(0, 0, 0, 0, mSize);
}

void Tree::upload(ShaderBuffer& ssbo){
	LogInfo("Uploading tree data...");
	unsigned int* buffer = new unsigned int[mNodeCount + 1];
	buffer[0] = 0u;
	for (int i = 0; i < mNodeCount; i++) {
		buffer[i + 1] = mNodes[i].leaf ? 1u : 0u;
		if (mNodes[i].leaf) buffer[i + 1] ^= (int(mNodes[i].data) << 1);
		else buffer[i + 1] ^= (int(mNodes[i].children) << 1);
	}
	ssbo.update((mNodeCount + 1) * sizeof(unsigned int), (void*)buffer);
	delete[] buffer;
	std::stringstream ss; ss << mNodeCount << " nodes uploaded."; LogInfo(ss.str());
}

void Tree::generateNode(size_t ind, int x0, int y0, int z0, int size) {
	Assert(size >= 1);
	if (ind >= mNodes.size()) mNodes.resize(ind + 1);
	if (size == 1) {
		// Generate single block
		double density = WorldGen::getDensity(x0, y0, z0);
		mNodes[ind].data = WorldGen::getBlock(x0, y0, z0, mHeightMap[x0 * mSize + z0], density);
		mNodes[ind].leaf = true;
		// Count
		mBlocksGenerated++;
		if (mBlocksGenerated % 10000000 == 0) {
			int percent = mBlocksGenerated * 100 / ((long long)mSize * mSize * mHeight);
			std::stringstream ss;
			ss << mBlocksGenerated << "(" << percent << "%) blocks generated, ";
			ss << mNodeCount << " nodes used.";
			LogVerbose(ss.str());
		}
		return;
	}
	if (y0 >= mHeight) {
		mNodes[ind].data = 0;
		mNodes[ind].leaf = true;
		return;
	}
	mNodes[ind].leaf = false;
	int cptr = mNodes[ind].children = mNodeCount;
	int half = size / 2;
	mNodeCount += 8;
	generateNode(cptr + 0, x0, y0, z0, half);
	generateNode(cptr + 1, x0 + half, y0, z0, half);
	generateNode(cptr + 2, x0, y0 + half, z0, half);
	generateNode(cptr + 3, x0 + half, y0 + half, z0, half);
	generateNode(cptr + 4, x0, y0, z0 + half, half);
	generateNode(cptr + 5, x0 + half, y0, z0 + half, half);
	generateNode(cptr + 6, x0, y0 + half, z0 + half, half);
	generateNode(cptr + 7, x0 + half, y0 + half, z0 + half, half);
	if (mNodeCount == cptr + 8) {
		bool f = true;
		for (int i = cptr + 0; i < cptr + 8; i++) {
			Assert(mNodes[i].leaf);
			if (mNodes[i].data != mNodes[cptr].data) {
				f = false;
				break;
			}
		}
		if (f) {
			mNodes[ind].leaf = true;
			mNodes[ind].data = mNodes[cptr].data;
			mNodeCount -= 8;
		}
	}
}


#include <cstdint>
#include <vector>
#include <stdexcept>

class FastBitSet {
private:
    using BitBlock = uint64_t;
    static constexpr int BITS_PER_BLOCK = 64;

    std::vector<BitBlock> tree;
    size_t numBits;
    size_t numLeafNodes;
    size_t firstLeafIndex;

    void initializeTree() {
        // Calculate number of leaf nodes needed
        numLeafNodes = (numBits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;

        // For a complete binary tree, first leaf is at index (n-1)/2 + 1
        // where n is total number of nodes
        firstLeafIndex = numLeafNodes - 1;

        // Size needed for the complete tree
        size_t treeSize = numLeafNodes * 2 - 1;
        tree.resize(treeSize, 0);

        // Set padding bits in the last leaf to 1
        if (numBits % BITS_PER_BLOCK != 0) {
            size_t lastLeafBits = numBits % BITS_PER_BLOCK;
            BitBlock mask = (BitBlock(1) << lastLeafBits) - 1;
            tree[treeSize - 1] |= ~mask;
        }
    }

    size_t getLeafIndex(size_t bitIndex) const {
        return firstLeafIndex + (bitIndex / BITS_PER_BLOCK);
    }

    void updateParents(size_t leafIndex) {
        size_t current = leafIndex;
        while (current > 0) {
            // Get parent index
            size_t parent = (current - 1) / 2;

            // Parent value is AND of its children
            size_t leftChild = 2 * parent + 1;
            size_t rightChild = 2 * parent + 2;

            BitBlock oldValue = tree[parent];
            tree[parent] = tree[leftChild] & tree[rightChild];

            // If parent didn't change, we can stop
            if (oldValue == tree[parent]) break;

            current = parent;
        }
    }

public:
    explicit FastBitSet(size_t bits) : numBits(bits) {
        if (bits == 0) {
            throw std::invalid_argument("BitSet size must be positive");
        }
        initializeTree();
    }

    void setBit(size_t index) {
        if (index >= numBits) {
            throw std::out_of_range("Bit index out of range");
        }

        size_t leafIndex = getLeafIndex(index);
        size_t bitOffset = index % BITS_PER_BLOCK;

        // Set bit in leaf node
        tree[leafIndex] |= (BitBlock(1) << bitOffset);

        // Update parents
        updateParents(leafIndex);
    }

    bool isComplete() const {
        return tree[0] == ~BitBlock(0);
    }

    bool getBit(size_t index) const {
        if (index >= numBits) {
            throw std::out_of_range("Bit index out of range");
        }

        size_t leafIndex = getLeafIndex(index);
        size_t bitOffset = index % BITS_PER_BLOCK;

        return (tree[leafIndex] & (BitBlock(1) << bitOffset)) != 0;
    }

    size_t size() const {
        return numBits;
    }
};
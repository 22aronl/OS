#include "ext2.h"
#include "libk.h"

Ext2::Ext2(Ide* ide): ide(ide), root() {
    SuperBlock sb;

    ide->read(1024,sb);

    iNodeSize = sb.inode_size;
    iNodesPerGroup = sb.inodes_per_group;
    numberOfNodes = sb.inodes_count;
    numberOfBlocks = sb.blocks_count;

    blockSize = uint32_t(1) << (sb.log_block_size + 10);
    
    nGroups = (sb.blocks_count + sb.blocks_per_group - 1) / sb.blocks_per_group;
    ASSERT(nGroups * sb.blocks_per_group >= sb.blocks_count);

    auto superBlockNumber = 1024 / blockSize;
    auto groupTableNumber = superBlockNumber + 1;
    auto groupTableSize = sizeof(BlockGroup) * nGroups;
    auto groupTable = new BlockGroup[nGroups];
    auto cnt = ide->read_all(groupTableNumber * blockSize, groupTableSize, (char*) groupTable);
    ASSERT(cnt == groupTableSize);

    iNodeTables = new uint32_t[nGroups];

    for (uint32_t i=0; i < nGroups; i++) {
        auto g = &groupTable[i];
        iNodeTables[i] = g->inode_table;
    }

    root = get_node(2);
}

Node* Ext2::get_node(uint32_t number) {
    ASSERT(number > 0);
    ASSERT(number <= numberOfNodes);
    auto index = number - 1;

    auto groupIndex = index / iNodesPerGroup;
    ASSERT(groupIndex < nGroups);
    auto indexInGroup = index % iNodesPerGroup;
    auto iTableBase = iNodeTables[groupIndex];
    ASSERT(iTableBase <= numberOfBlocks);
    auto nodeOffset = iTableBase * blockSize + indexInGroup * iNodeSize;

    auto out = new Node(ide,number,blockSize);
    ide->read(nodeOffset,out->data);
    return out;
}


///////////// Node /////////////

void Node::get_symbol(char* buffer) {
    MISSING();
}

void Node::read_block(uint32_t index, char* buffer) {
    ASSERT(index < data.n_sectors / (block_size / 512));

    auto refs_per_block = block_size / 4;

    uint32_t block_index;

    if (index < 12) {
        uint32_t* direct = &data.direct0;
        block_index = direct[index];
    } else if (index < (12 + refs_per_block)) {
        ide->read(data.indirect_1 * block_size + (index - 12) * 4,block_index);
    } else {
        block_index = 0;
        Debug::panic("index = %d\n",index);
    }

    auto cnt = ide->read_all(block_index * block_size, block_size,buffer);
    ASSERT(cnt == block_size);
}

uint32_t Node::find(const char* name) {
    uint32_t out = 0;

    entries([&out,name](uint32_t number, const char* nm) {
        if (K::streq(name,nm)) {
            out = number;
        }
    });

    return out;
}

uint32_t Node::entry_count() {
    ASSERT(is_dir());
    uint32_t count = 0;
    entries([&count](uint32_t,const char*) {
        count += 1;
    });
    return count;
}


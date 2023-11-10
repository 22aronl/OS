#include "ext2.h"
#include "libk.h"

Ext2::Ext2(Ide *ide) : ide(ide), root() {
    uint32_t *supergroup = new uint32_t[1024 / sizeof(uint32_t)];
    // char* supergroup = new char[1024];
    uint32_t supergroup_start = 1024 / ide->block_size;
    ide->read_all(1024, 1024, (char *)supergroup);

    // Debug::printf("inode counts %ld\n", supergroup[0]);
    // Debug::printf("block counts %ld\n", supergroup[1]);
    // Debug::printf("Block Size: %ld\n", supergroup[5]);
    // Debug::printf("Block Size2: %ld\n", 1024 << supergroup[6]);
    // Debug::printf("Blocks per group %ld\n", supergroup[7]);
    // Debug::printf("Block2s per group %ld\n", supergroup[8]);
    // Debug::printf("Inodes per group %ld\n", supergroup[9]);
    // Debug::printf("one %d\n", supergroup[32]);
    // Debug::printf("one %d\n", supergroup[33]);
    // Debug::printf("one %d\n", supergroup[34]);
    // Debug::printf("one %d\n", supergroup[35]);
    block_size = 1024 << supergroup[6];
    blocks_per_group = supergroup[8]; // TODO: SERIOUS ERROR
    i_nodes_per_group = supergroup[10];
    sectors_per_block = block_size / ide->block_size;

    i_node_size = supergroup[22] & 0xFF;

    starting_sector = block_size % (ide->block_size) > (2 * supergroup_start)
                          ? block_size % (ide->block_size)
                          : (2 * supergroup_start);
    uint32_t total_blocks = supergroup[1];
    num_blocks = (total_blocks - 1) / blocks_per_group + 1;
    block_group_size = 2 + (i_nodes_per_group * i_node_size) / ide->block_size + blocks_per_group;

    i_node_groups = new uint32_t[(num_blocks - 1) / sizeof(uint32_t) + 1];
    uint32_t starting_sector = block_size > 2048 ? block_size : 2048;
    uint32_t cur_sector = starting_sector/ide->block_size;
    uint32_t total_groups_visited = 0;

    do {
        ide->read_block(cur_sector++, (char *)supergroup);
        for (uint32_t i = 2;
             i < ide->block_size / sizeof(uint32_t) && total_groups_visited < num_blocks; i += 8) {
            i_node_groups[total_groups_visited++] = supergroup[i];
        }
    } while (total_groups_visited < num_blocks);
    delete[] supergroup;
    root = get_node(2);
}

Node *Ext2::get_node(uint32_t number) {
    if(number == 0)
        return nullptr;
    uint32_t group_number = (number - 1) / i_nodes_per_group;
    uint32_t index = (number - 1) % i_nodes_per_group;
    uint32_t byte_start = i_node_groups[group_number] * block_size + index * i_node_size;
    return new Node(ide, number, block_size, i_node_size, byte_start);
}

///////////// Node /////////////

uint32_t Node::calculate_sector(uint32_t block_id) {
    return block_size * block_id; // / ide->block_size;
    // uint32_t group_number = block_id / this->blocks_per_group;
    // return ((block_id % this->blocks_per_group) * this->block_size +
    //         this->starting_sector +
    //         group_number * this->blocks_per_group * this->block_size) /
    //        ide->block_size; //TODO: THIS IS VERY WRONG
}

uint32_t Node::get_block_id(uint32_t index) {
    uint32_t num_block = this->block_size / sizeof(uint32_t);
    if (index < 12) {
        return this->block_pointers[index];
    } else if (index < 12 + num_block) {
        uint32_t *indirect = new uint32_t[num_block];
        ide->read_all(calculate_sector(this->block_pointers[12]), block_size, (char *)indirect);
        uint32_t block = indirect[index - 12];
        delete[] indirect;
        return block;
    } else if (index < 12 + num_block + num_block * num_block) {
        uint32_t *indirect = new uint32_t[num_block];
        ide->read_all(calculate_sector(this->block_pointers[13]), block_size, (char *)indirect);
        uint32_t block = indirect[(index - 12 - num_block) / num_block];
        ide->read_all(calculate_sector(block), block_size, (char *)indirect);
        block = indirect[(index - 12 - num_block) % num_block];
        delete[] indirect;
        return block;
    } else {
        uint32_t *indirect = new uint32_t[num_block];
        ide->read_all(calculate_sector(this->block_pointers[14]), block_size, (char *)indirect);
        uint32_t block =
            indirect[(index - 12 - num_block - num_block * num_block) / (num_block * num_block)];
        ide->read_all(calculate_sector(block), block_size, (char *)indirect);
        block =
            indirect[((index - 12 - num_block - num_block * num_block) % (num_block * num_block)) / (num_block)];
        ide->read_all(calculate_sector(block), block_size, (char *)indirect);
        block = indirect[(index - 12 - num_block - num_block * num_block) % num_block];
        delete[] indirect;
        return block;
    }
}

void Node::get_symbol(char *buffer) { memcpy(buffer, sym_link, size); }

void Node::read_block(uint32_t index, char *buffer) {
    //Debug::printf("%d\n", size_in_blocks());
    //Debug::printf("index %u %u\n", index, this->block_pointers[0]);
    if (index >= size_in_blocks()) { // this codes wrong
        Debug::panic("index out of bounds");
    }

    //Debug::printf("sector %u, %u, %ld\n", get_block_id(index),
                  //calculate_sector(get_block_id(index)), block_size);
    ide->read_all(calculate_sector(get_block_id(index)), block_size, buffer);
}

void Node::read_dir() {
    if (this->size == 0) {
        return;
    }
    ext2_dir *next = nullptr;
    char *buffer = new char[this->block_size];
    read_block(0, buffer);
    uint32_t offset = 0;
    uint32_t total = 0;
    uint32_t cur_block = 0;
    this->num_entries = 0;
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    //Debug::printf("this-<size %u\n", this->size);
    while (total < this->size) {
        inode = *(uint32_t *)(buffer + offset);
        rec_len = *(uint16_t *)(buffer + offset + 4);
        name_len = *(uint8_t *)(buffer + offset + 6);
        file_type = *(uint8_t *)(buffer + offset + 7);
        char *name = new char[name_len + 1];
        memcpy(name, buffer + offset + 8, name_len);
        name[name_len] = '\0';
        // Debug::printf("Node %s, %ld, %ld, %ld, %ld, %ld, %ld\n", name, inode, rec_len, name_len,
                    //   offset, total, size);
        ext2_dir *dir = new ext2_dir{inode, rec_len, name_len, file_type, name, nullptr};

        if (this->dir_entries == nullptr) {
            this->dir_entries = dir;
        } else {
            next->next = dir;
        }

        next = dir;
        offset += rec_len;
        total += rec_len;
        this->num_entries++;
        if (offset >= this->block_size) {
            offset = 0;
            cur_block++;
            if (total < this->size)
                read_block(cur_block, buffer);
        }
    }
    delete[] buffer;
}

bool compare_name(const char *name1, const char *name2) {
    //Debug::printf("comparing %s %s\n", name1, name2);
    uint32_t i = 0;
    while (name1[i] != '\0' && name2[i] != '\0') {
        if (name1[i] != name2[i]) {
            return false;
        }
        i++;
    }
    return name1[i] == '\0' && name2[i] == '\0';
}

uint32_t Node::find(const char *name) {
    if (!is_dir()) {
        Debug::panic("not a directory");
    }
    ext2_dir *cur = this->dir_entries;
    while (cur != nullptr) {
        // Debug::printf("current %s compare %s\n", cur->name, name);
        if (compare_name(cur->name, name)) {
            return cur->inode;
        }
        cur = cur->next;
    }
    return 0;
}

uint32_t Node::entry_count() {
    ASSERT(is_dir());
    return this->num_entries;
}

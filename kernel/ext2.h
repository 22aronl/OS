#pragma once
#include "atomic.h"
#include "ide.h"
#include "libk.h"

struct ext2_dir {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char *name;
    ext2_dir *next;
};

// A wrapper around an i-node
class Node : public BlockIO { // we implement BlockIO because we
                              // represent data
    // size of an I-node
    Ide *ide;

    uint32_t inode_size;
    uint32_t starting_sector;  // TODO: DEFINE THIS iun constructor
    uint32_t blocks_per_group; // TODO: DEFINE THIS iun constructor
    uint16_t i_type;
    uint16_t hard_links;
    uint32_t size;
    uint32_t *block_pointers;
    uint32_t *sym_link;
    ext2_dir *dir_entries;
    uint32_t num_entries;
    bool flag031;

  public:
    // i-number of this node
    const uint32_t number;

    Node(Ide *ide, uint32_t number, uint32_t block_size, uint32_t inode_size, uint32_t byte_loc)
        : BlockIO(block_size), ide(ide), inode_size(inode_size), number(number) {
        uint32_t *buffer = new uint32_t[inode_size / sizeof(uint32_t)];
        // char *buffer = new char[inode_size];
        ide->read_all(byte_loc, inode_size, (char *)buffer); // TODO: ERROR
        flag031 = false;

        // Debug::printf("finsihed reading\n");
        // Debug::printf("buf %04X\n", (unsigned char)buffer[0]);
        // Debug::printf("buf %04X\n", (unsigned char)buffer[1]);
        // Debug::printf("buf %x\n", buffer[0] & 0xF000);
        // Debug::printf("buf %ld\n", buffer[0] >> 0x10);
        // Debug::printf("buf %ld\n", buffer[0] & 0xFFFF);

        // Debug::printf("buf %hhx\n", buffer[1]);
        // Debug::printf("buf %ld\n", buffer[2]);
        i_type = buffer[0] & 0xF000;    // byte 0 & 1
        size = buffer[1];               // bytes 4->7
        hard_links = buffer[6] >> 0x10; // byte 26 & 27

        dir_entries = nullptr;
        // num_entries = 0;

        if (is_file() || is_dir() || (is_symlink() && size >= 60)) {
            block_pointers = new uint32_t[15];
            for (int i = 0; i < 15; i++)
                block_pointers[i] = buffer[10 + i];
            if (is_dir())
                read_dir();
            else if(is_symlink()) {
                uint32_t* temp = new uint32_t[(size - 1) / sizeof(uint32_t) + 2];
                // Assume symlink is in one block
                read_all(0, size, (char*) temp);
                //ide->read(calculate_sector(buffer[10]), size, (char *)block_pointers);
                //((char *)temp)[size] = '\0';
                //Debug::printf("s %s\n", temp);
                sym_link = temp;
            }
        } else if (is_symlink() && (size < 60)) {
            sym_link = new uint32_t[(size - 1) / sizeof(uint32_t) + 2];
            memcpy(sym_link, buffer + 10, size);
            //((char *)sym_link)[size] = '\0';
        }

        // if(is_symlink())
        //     size++;
        delete[] buffer;
    }

    virtual ~Node() {
        if (is_file())
            delete[] block_pointers;
        else if (is_dir()) {
            delete[] block_pointers;
            ext2_dir *next = dir_entries;
            while (next != nullptr) {
                ext2_dir *temp = next;
                next = next->next;
                delete[] temp->name;
                delete temp;
            }
        } else if(is_symlink()) {
            delete[] block_pointers;
            delete[] sym_link;
        }
    }

    uint32_t calculate_sector(uint32_t block_id);
    uint32_t get_block_id(uint32_t index);
    void read_dir();

    // How many bytes does this i-node represent
    //    - for a file, the size of the file
    //    - for a directory, implementation dependent
    //    - for a symbolic link, the length of the name
    uint32_t size_in_bytes() override { return size; }

    // read the given block (panics if the block number is not valid)
    // remember that block size is defined by the file system not the device
    void read_block(uint32_t number, char *buffer) override;

    inline uint16_t get_type() { return i_type; }

    // true if this node is a directory
    bool is_dir() { return i_type == 0x4000; } //0x41ED; } // 

    // true if this node is a file
    bool is_file() { return i_type == 0x8000; }

    // true if this node is a symbolic link
    bool is_symlink() { return i_type == 0xA000; }

    // If this node is a symbolic link, fill the buffer with
    // the name the link referes to.
    //
    // Panics if the node is not a symbolic link
    //
    // The buffer needs to be at least as big as the the value
    // returned by size_in_byte()
    void get_symbol(char *buffer);

    // Returns the number of hard links to this node
    uint32_t n_links() { return hard_links; }

    void show(const char *msg) { MISSING(); }

    uint32_t find(const char *name);

    // Returns the number of entries in a directory node
    //
    // Panics if not a directory
    uint32_t entry_count();
};

// This class encapsulates the implementation of the Ext2 file system
class Ext2 {
    // The device on which the file system resides
    Ide *ide;

  public:
    // The root directory for this file system
    Node *root;

//   private:
    uint32_t i_node_size;
    uint32_t i_nodes_per_group;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t starting_sector;
    uint32_t block_group_size;
    uint32_t num_blocks;
    uint32_t sectors_per_block;
    uint32_t *i_node_groups;


  public:
    // Mount an existing file system residing on the given device
    // Panics if the file system is invalid
    Ext2(Ide *ide);

    // Returns the block size of the file system. Doesn't have
    // to match that of the underlying device
    uint32_t get_block_size() { return block_size; }

    // Returns the actual size of an i-node. Ext2 specifies that
    // an i-node will have a minimum size of 128B but could have
    // more bytes for extended attributes
    uint32_t get_inode_size() { return i_node_size; }

    // Returns the node with the given i-number
    Node *get_node(uint32_t number);

    // If the given node is a directory, return a reference to the
    // node linked to that name in the directory.
    //
    // Returns a null reference if "name" doesn't exist in the directory
    //
    // Panics if "dir" is not a directory
    Node *find(Node *dir, const char *name) {
        uint32_t number = dir->find(name);
        if (number == 0) {
            return nullptr;
        } else {
            // Debug::printf("found %s at %d\n",name,number);
            return get_node(number);
        }
    }

    Node *find_relative(Node *dir, char *name) {
        if (dir == nullptr) {
            return nullptr;
        }

        if(name[0] == '\0') {
            return dir;
        }

        if (!dir->is_dir())
            return nullptr;

        uint32_t len = 0;
        while (name[len] != '\0' && name[len] != '/')
            len++;
        if (name[len] == '\0') {
            return find(dir, name);
        }

        name[len] = '\0';
        return find_relative(find(dir, name), name + len + 1);
    }

    Node *find_absolute(const char *name) {
        char *n = new char[K::strlen(name) + 1];
        for (int i = 0; i < K::strlen(name) + 1; i++)
            n[i] = name[i];
        Node *node = find_relative(root, n + 1);
        delete[] n;
        return node;
    }
};

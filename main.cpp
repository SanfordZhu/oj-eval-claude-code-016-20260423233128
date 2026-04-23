#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

const int MAX_KEY_SIZE = 64;
const int ORDER = 59;
const int PAGE_SIZE = 4096;

struct PageHeader {
    bool is_leaf;
    int num_keys;
    int parent_page;
    int next_leaf;
    int prev_leaf;
    int page_num;
};

struct BPTree {
    int fd;
    char* data;
    size_t file_size;
    string filename;
    int root_page;
    int next_free_page;

    BPTree(const string& fname) : filename(fname) {
        fd = open(fname.c_str(), O_RDWR);
        if (fd < 0) {
            fd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
            root_page = 0;
            next_free_page = 1;
            file_size = sizeof(root_page) + sizeof(next_free_page) + PAGE_SIZE;
            ftruncate(fd, file_size);
            data = (char*)mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            write_header();
            create_root();
        } else {
            struct stat st;
            fstat(fd, &st);
            file_size = st.st_size;
            data = (char*)mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            read_header();
        }
    }

    ~BPTree() {
        munmap(data, file_size);
        close(fd);
    }

    void ensure_space() {
        size_t needed = sizeof(root_page) + sizeof(next_free_page) + (size_t)(next_free_page) * PAGE_SIZE;
        if (needed > file_size) {
            munmap(data, file_size);
            file_size = needed;
            ftruncate(fd, file_size);
            data = (char*)mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        }
    }

    void write_header() {
        memcpy(data + 0, &root_page, sizeof(root_page));
        memcpy(data + sizeof(root_page), &next_free_page, sizeof(next_free_page));
        msync(data, sizeof(root_page) + sizeof(next_free_page), MS_SYNC);
    }

    void read_header() {
        memcpy(&root_page, data + 0, sizeof(root_page));
        memcpy(&next_free_page, data + sizeof(root_page), sizeof(next_free_page));
    }

    int allocate_page() {
        int page = next_free_page++;
        write_header();
        ensure_space();
        return page;
    }

    void create_root() {
        int page = allocate_page();
        PageHeader header;
        header.is_leaf = true;
        header.num_keys = 0;
        header.parent_page = -1;
        header.next_leaf = -1;
        header.prev_leaf = -1;
        header.page_num = page;
        write_page(page, header);
        root_page = page;
        write_header();
    }

    void read_page(int page_num, PageHeader& header, vector<string>& keys, vector<int>& values) {
        keys.clear();
        char* page_start = data + sizeof(root_page) + sizeof(next_free_page) + page_num * PAGE_SIZE;
        memcpy(&header, page_start, sizeof(header));
        if (header.num_keys > 0) {
            keys.reserve(header.num_keys);
            values.reserve(header.num_keys);
        }
        char* ptr = page_start + sizeof(header);
        for (int i = 0; i < header.num_keys; i++) {
            char buf[MAX_KEY_SIZE + 1];
            memcpy(buf, ptr, MAX_KEY_SIZE);
            buf[MAX_KEY_SIZE] = '\0';
            keys.emplace_back(buf);
            ptr += MAX_KEY_SIZE;
            int val;
            memcpy(&val, ptr, sizeof(val));
            ptr += sizeof(val);
            values.push_back(val);
        }
    }

    void write_page(int page_num, PageHeader& header, const vector<string>& keys, const vector<int>& values) {
        char* page_start = data + sizeof(root_page) + sizeof(next_free_page) + page_num * PAGE_SIZE;
        memcpy(page_start, &header, sizeof(header));
        char* ptr = page_start + sizeof(header);
        for (size_t i = 0; i < keys.size(); i++) {
            char buf[MAX_KEY_SIZE] = {0};
            strncpy(buf, keys[i].c_str(), MAX_KEY_SIZE);
            memcpy(ptr, buf, MAX_KEY_SIZE);
            ptr += MAX_KEY_SIZE;
            int val = values[i];
            memcpy(ptr, &val, sizeof(val));
            ptr += sizeof(val);
        }
        msync(page_start, PAGE_SIZE, MS_SYNC);
    }

    void write_page(int page_num, PageHeader& header) {
        vector<string> keys;
        vector<int> values;
        write_page(page_num, header, keys, values);
    }

    int compare(const string& a, const string& b) {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    int find_leaf(int root, const string& key) {
        int current = root;
        while (true) {
            PageHeader header;
            vector<string> keys;
            vector<int> children;
            read_page(current, header, keys, children);
            if (header.is_leaf) {
                return current;
            }
            int left = 0, right = header.num_keys;
            while (left < right) {
                int mid = (left + right) / 2;
                if (compare(key, keys[mid]) >= 0) {
                    left = mid + 1;
                } else {
                    right = mid;
                }
            }
            current = children[left];
        }
    }

    int get_parent_page(int page_num) {
        PageHeader header;
        vector<string> keys;
        vector<int> values;
        read_page(page_num, header, keys, values);
        return header.parent_page;
    }

    bool insert_entry(const string& key, int value) {
        int leaf_page = find_leaf(root_page, key);
        PageHeader header;
        vector<string> keys;
        vector<int> values;
        read_page(leaf_page, header, keys, values);

        for (int i = 0; i < header.num_keys; i++) {
            if (keys[i] == key && values[i] == value) {
                return true;
            }
        }

        int left = 0, right = header.num_keys;
        while (left < right) {
            int mid = (left + right) / 2;
            if (compare(key, keys[mid]) > 0) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        int pos = left;
        while (pos < header.num_keys && compare(key, keys[pos]) == 0 && values[pos] < value) {
            pos++;
        }

        keys.insert(keys.begin() + pos, key);
        values.insert(values.begin() + pos, value);
        header.num_keys++;

        if (header.num_keys <= ORDER) {
            write_page(leaf_page, header, keys, values);
            return true;
        }

        split_leaf(leaf_page, header, keys, values);
        return true;
    }

    void split_leaf(int old_page, PageHeader& old_header, vector<string>& old_keys, vector<int>& old_values) {
        int new_page = allocate_page();
        PageHeader new_header;
        new_header.is_leaf = true;
        new_header.parent_page = old_header.parent_page;
        new_header.next_leaf = old_header.next_leaf;
        new_header.prev_leaf = old_page;
        new_header.num_keys = 0;

        if (old_header.next_leaf != -1) {
            PageHeader next_header;
            vector<string> next_keys;
            vector<int> next_values;
            read_page(old_header.next_leaf, next_header, next_keys, next_values);
            next_header.prev_leaf = new_page;
            write_page(old_header.next_leaf, next_header, next_keys, next_values);
        }
        old_header.next_leaf = new_page;

        int mid = (old_header.num_keys + 1) / 2;
        vector<string> new_keys;
        vector<int> new_values;
        for (int i = mid; i < old_header.num_keys; i++) {
            new_keys.push_back(old_keys[i]);
            new_values.push_back(old_values[i]);
        }
        old_keys.resize(mid);
        old_values.resize(mid);
        old_header.num_keys = mid;
        new_header.num_keys = new_keys.size();

        write_page(old_page, old_header, old_keys, old_values);
        write_page(new_page, new_header, new_keys, new_values);

        insert_into_parent(old_page, new_keys[0], new_page);
    }

    void insert_into_parent(int left_page, const string& key, int right_page) {
        if (left_page == root_page) {
            int new_root = allocate_page();
            PageHeader header;
            header.is_leaf = false;
            header.num_keys = 1;
            header.parent_page = -1;
            header.next_leaf = -1;
            header.prev_leaf = -1;
            header.page_num = new_root;

            vector<string> keys;
            vector<int> children;
            keys.push_back(key);
            children.push_back(left_page);
            children.push_back(right_page);

            PageHeader left_header;
            vector<string> left_keys;
            vector<int> left_values;
            read_page(left_page, left_header, left_keys, left_values);
            left_header.parent_page = new_root;
            write_page(left_page, left_header, left_keys, left_values);

            PageHeader right_header;
            vector<string> right_keys;
            vector<int> right_values;
            read_page(right_page, right_header, right_keys, right_values);
            right_header.parent_page = new_root;
            write_page(right_page, right_header, right_keys, right_values);

            write_page(new_root, header, keys, children);
            root_page = new_root;
            write_header();
            return;
        }

        PageHeader parent_header;
        vector<string> parent_keys;
        vector<int> parent_children;
        int parent_page = get_parent_page(left_page);
        read_page(parent_page, parent_header, parent_keys, parent_children);

        int left = 0, right = parent_header.num_keys;
        while (left < right) {
            int mid = (left + right) / 2;
            if (compare(key, parent_keys[mid]) > 0) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        int pos = left;
        parent_keys.insert(parent_keys.begin() + pos, key);
        parent_children.insert(parent_children.begin() + pos + 1, right_page);
        parent_header.num_keys++;

        PageHeader left_header;
        vector<string> left_keys;
        vector<int> left_values;
        read_page(left_page, left_header, left_keys, left_values);
        left_header.parent_page = parent_page;
        write_page(left_page, left_header, left_keys, left_values);

        PageHeader right_header;
        vector<string> right_keys;
        vector<int> right_values;
        read_page(right_page, right_header, right_keys, right_values);
        right_header.parent_page = parent_page;
        write_page(right_page, right_header, right_keys, right_values);

        if (parent_header.num_keys <= ORDER) {
            write_page(parent_page, parent_header, parent_keys, parent_children);
            return;
        }

        split_internal(parent_page, parent_header, parent_keys, parent_children);
    }

    void split_internal(int old_page, PageHeader& old_header, vector<string>& old_keys, vector<int>& old_children) {
        int new_page = allocate_page();
        PageHeader new_header;
        new_header.is_leaf = false;
        new_header.parent_page = old_header.parent_page;
        new_header.next_leaf = -1;
        new_header.prev_leaf = -1;
        new_header.num_keys = 0;

        int mid = old_header.num_keys / 2;
        string split_key = old_keys[mid];

        vector<string> new_keys;
        vector<int> new_children;
        for (int i = mid + 1; i < old_header.num_keys; i++) {
            new_keys.push_back(old_keys[i]);
        }
        for (int i = mid + 1; i <= old_header.num_keys; i++) {
            new_children.push_back(old_children[i]);
        }

        for (size_t i = 0; i < new_children.size(); i++) {
            PageHeader child_header;
            vector<string> child_keys;
            vector<int> child_values;
            read_page(new_children[i], child_header, child_keys, child_values);
            child_header.parent_page = new_page;
            write_page(new_children[i], child_header, child_keys, child_values);
        }

        old_keys.resize(mid);
        old_children.resize(mid + 1);
        old_header.num_keys = mid;
        new_header.num_keys = new_keys.size();

        write_page(old_page, old_header, old_keys, old_children);
        write_page(new_page, new_header, new_keys, new_children);

        insert_into_parent(old_page, split_key, new_page);
    }

    bool delete_entry(const string& key, int value) {
        int leaf_page = find_leaf(root_page, key);
        PageHeader header;
        vector<string> keys;
        vector<int> values;
        read_page(leaf_page, header, keys, values);

        bool found = false;
        int pos = 0;
        for (pos = 0; pos < header.num_keys; pos++) {
            if (keys[pos] == key && values[pos] == value) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }

        keys.erase(keys.begin() + pos);
        values.erase(values.begin() + pos);
        header.num_keys--;

        write_page(leaf_page, header, keys, values);
        return true;
    }

    vector<int> find_values(const string& key) {
        vector<int> result;
        int current_page = find_leaf(root_page, key);

        while (current_page != -1) {
            PageHeader header;
            vector<string> keys;
            vector<int> values;
            read_page(current_page, header, keys, values);

            for (int i = 0; i < header.num_keys; i++) {
                int cmp = compare(key, keys[i]);
                if (cmp == 0) {
                    result.push_back(values[i]);
                } else if (cmp < 0) {
                    break;
                }
            }

            if (header.next_leaf == -1) {
                break;
            }
            PageHeader next_header;
            vector<string> next_keys;
            vector<int> next_values;
            read_page(header.next_leaf, next_header, next_keys, next_values);
            if (next_header.num_keys == 0 || compare(key, next_keys[0]) < 0) {
                break;
            }
            current_page = header.next_leaf;
        }

        return result;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    setvbuf(stdin, nullptr, _IOFBF, 1 << 20);
    setvbuf(stdout, nullptr, _IOFBF, 1 << 20);

    BPTree tree("bptree.db");

    int n;
    cin >> n;

    while (n--) {
        string cmd;
        cin >> cmd;
        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            tree.insert_entry(key, value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            tree.delete_entry(key, value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            vector<int> result = tree.find_values(key);
            if (result.empty()) {
                cout << "null\n";
            } else {
                for (size_t i = 0; i < result.size(); i++) {
                    if (i > 0) cout << ' ';
                    cout << result[i];
                }
                cout << '\n';
            }
        }
    }

    return 0;
}

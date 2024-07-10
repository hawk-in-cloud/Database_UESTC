# Proj2. btree索引实现
* tips: btree 即 b树结构

### 实验目的：（详细要求见ppt）

#### 根据第2章btree的知识设计并实现一个磁盘存储的索引：
* 查找
* 插入一个条目
* 删除一个条目

### 实验内容：

#### 写一个自解释的record结构，分为4个部分：
* 记录长度
* 字段偏移量数组，逆序，从header开始到各字段头部的偏移量
* Header，类型
* 各字段顺序摆放

#### 写一个Common Header
* SpaceId
* Free space pointer
* Garbage chain
* BlockId
* Next block
```
//索引块头部
// 32B+8B=40B
// 12B+36B=48B
struct IndexHeader : CommonHeader
{
    unsigned int next;       // 下一个数据块(4B)
    long long stamp;         // 时戳(8B)
    unsigned short slots;    // slots[]长度(2B)
    unsigned short freesize; // 空闲空间大小(2B)
    unsigned int self;       // 本块id(4B)
    bool is_leaf;            //标记叶子节点(1B)
    char pad[7];             //填充(7B)
};

//超块头
struct SuperHeader :CommonHeader
{
    unsigned int first;       // 第1个数据块(4B)
    long long stamp;          // 时戳(8B)
    unsigned int idle;        // 空闲块(4B)
    unsigned int datacounts;  // 数据块个数(4B)
    unsigned int idlecounts;  // 空闲块个数(4B)
    unsigned int self;        // 本块id(4B)
    unsigned int maxid;       // 最大的blockid(4B)
    unsigned int indexcounts; //索引块个数(4B)
    long long records;        // 记录数目(8B)
    unsigned int indexroot;   // 索引根节点id(4B)
    // hi young
    unsigned short order;   //阶数(2B)?
    unsigned short height;  //树的高度(2B)
    unsigned int indexleaf; //标识第一个叶子节点的位置(4B)
};
```
#### 写一个Trailer
* Checksum32
```
struct Trailer
{
    Slot slots[1];         // slots数组
    unsigned int checksum; // 校验和(4B)
};
```

#### 写一个Data Header
* Rows
* Slots
```
struct DataHeader : CommonHeader
{
    unsigned int next;       // 下一个数据块(4B)
    unsigned int self;       // 本块id(4B)
};
```

## 实现过程如下：

### btree的索引结构实现：
* 定义了一个 B+ 树类，提供了搜索、插入和删除操作的接口，并包括一些用于管理树的内部状态和节点分裂的辅助函数。这个类用于数据库系统中的索引结构
```
#ifndef __DB_BPT_H__
#define __DB_BPT_H__
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <queue>
#include <stack>
#include "./block.h"
#include "./table.h"
#include "./buffer.h"
namespace db {
class bplus_tree
{
  public:
    Table *table_;
    std::stack<unsigned int> route;
    std::stack<unsigned short> routeslot;
    bplus_tree(bool force_empty = false)
        : table_(NULL)
    {}

    /*顶层操作*/
    unsigned int search(void *key, size_t key_len);
    unsigned int insert(void *key, size_t key_len, unsigned int value);
    int remove(void *key, size_t key_len);
    /*底层操作*/
    inline Table *get_table() { return table_; }//获取表
    inline void set_table(Table *table) { table_ = table; }//设置该树所属表
    unsigned int index_create(IndexBlock *preindex);//该函数用于叶子节点的分裂，分配新节点的同时维护链表
    void insert_to_index(void* key, size_t key_len, unsigned int newblockid);//叶子节点分裂后向上传递的函数
    /*删除时的叶子节点是否需要合并的操作*/
    //1.是否从右边节点借 2.需要借记录的叶子节点id 3.每个节点最少多少条记录。
    bool borrow_key(bool from_right, IndexBlock& borrower, unsigned short limit);
    void remove_from_index(std::pair<void*, size_t>);

    //清空数据结构中的栈
    std::pair<bool, unsigned int> index_search(void* key, size_t key_len);
    inline void reset_route()
    {
        while (!route.empty()) {
            route.pop();
        }
    }
    inline void reset_routeslot()
    {
        while (!routeslot.empty()) {
            routeslot.pop();
        }
    }
    //清空树，用于测试
    void clear_tree(unsigned int root);
};
} // namespace db
#endif // __DB_BPT_H__
```

---
### block中完成btree的索引搜索：
```
// 设定索引根节点
    inline void setIndexroot(unsigned int Indexroot)
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        header->indexroot = htobe32(Indexroot);
    }
    // 获取索引根节点
    inline unsigned int getIndexroot()
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        return be32toh(header->indexroot);
    }
    // 设定索引节点数量
    inline void setIndexcounts(unsigned int Indexcounts)
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        header->indexcounts = htobe32(Indexcounts);
    }
    // 获取索引节点数量
    inline unsigned int getIndexcounts()
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        return be32toh(header->indexcounts);
    }

    //设定阶数
    inline void setOrder(unsigned short setorder)
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        header->order = setorder;
    }
    //获取阶数
    inline unsigned short getOrder()
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        return header->order;
    }
    //设定树高
    inline void setHeight(unsigned short setheight)
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        header->height = setheight;
    }
    //获取树高
    inline unsigned short getHeight()
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        return header->height;
    }
    //设定第一个叶子节点位置
    inline void setIndexLeaf(unsigned int setindexleaf)
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        header->indexleaf = setindexleaf;
    }
    //获取第一个叶子节点位置
    inline unsigned int getIndexLeaf()
    {
        SuperHeader *header = reinterpret_cast<SuperHeader *>(buffer_);
        return header->indexleaf;
    }
```

---
### btree的block结构实现
```
class IndexBlock : public MetaBlock
{
  public:
    Table *table_; // 指向table
  public:
    IndexBlock()
        : table_(NULL)
    {}
    // 设定table
    inline void setTable(Table *table) { table_ = table; }
    // 获取table
    inline Table *getTable() { return table_; }

  public:
    // 查询记录
    // 给定一个关键字，从slots[]上搜索到该记录：
    // 1. 根据meta确定key的位置；
    // 2. 采用二分查找在slots[]上寻找
    // 返回值：
    //      1.找不到该记录，返回(false,lowbound)
    //      2.找到该记录，返回（true,lowbound）

    std::pair<bool, unsigned short> searchRecord(void *key, size_t size);
    unsigned short requireLength(std::vector<struct iovec> &iov);
    // 插入记录
    // 在block中插入记录，步骤如下：
    // 1. 先检查空间是否足够，如果够，则插入，然后重新排序；
    // 2. 不够，根据key寻找插入位置，从这个位置将block劈开；
    // 3. 计算劈开后前面序列的长度，然后判断新记录加入后空间是否足够，够则插入；
    // 4. 先将新的记录插入一个新的block，然后挪动原有记录到新的block；
    // 返回值：
    //      first:
    //           true - 表示记录完全插入
    //           false - 表示block被分裂
    //      second:
    //           -1 - 记录存在，不能插入
    //           index - 记录插入的位置（无论成功与否）
    std::pair<bool, unsigned short>
    insertRecord(std::vector<struct iovec> &iov);

    //清除
    void clear(
        unsigned short spaceid,
        unsigned int self,
        unsigned short type,
        bool is_leaf);
    //定位第一个Record，以应付不同头部大小的Block
    inline unsigned short getFirstRecord() { return sizeof(IndexHeader); }
    //设置叶子节点标记
    inline void setMark(bool mark)
    {
        IndexHeader *header = reinterpret_cast<IndexHeader *>(buffer_);
        header->is_leaf = mark;
    }
    //获取叶子节点标记
    inline bool getMark()
    {
        IndexHeader *header = reinterpret_cast<IndexHeader *>(buffer_);
        return header->is_leaf;
    }
    bool IndexBlock::copyRecord(size_t key_len,Record &record);
};
```

---

### 其中的三项操作：
#### 1. 查找：
```
std::pair<bool, unsigned short> IndexBlock::searchRecord(void *buf, size_t len)
{
    IndexHeader *header = reinterpret_cast<IndexHeader *>(buffer_);

    // 获取key位置
    RelationInfo *info = table_->info_;
    unsigned int key = info->key;
    //使用search找到lowbound
    unsigned short index = info->fields[key].type->search(buffer_, 0, buf, len);

    //判断lowbound的key和搜索的key是否一致。
    Record record;
    if (index >= getSlots()) {
        return std::pair<bool, unsigned short>(false, index);
    }

    refslots(index, record);
    unsigned char *pkey;
    unsigned int plen;
    record.refByIndex(&pkey, &plen, key);
    // key相等,存在该记录
    if (memcmp(pkey, buf, len) == 0)
        return std::pair<bool, unsigned short>(true, index);
    // key不相等，不存在该记录
    else
        return std::pair<bool, unsigned short>(false, index);
}

unsigned short IndexBlock::requireLength(std::vector<struct iovec> &iov)
{
    size_t length = ALIGN_TO_SIZE(Record::size(iov)); // 对齐8B后的长度
    size_t trailer =
        ALIGN_TO_SIZE((getSlots() + 1) * sizeof(Slot) + sizeof(unsigned int)) -
        ALIGN_TO_SIZE(
            getSlots() * sizeof(Slot) +
            sizeof(unsigned int)); // trailer新增部分
    return (unsigned short) (length + trailer);
}

```
#### 2. 插入一个条目：
```
std::pair<bool, unsigned short>
IndexBlock::insertRecord(std::vector<struct iovec> &iov)
{
    RelationInfo *info = table_->info_;
    unsigned int key = info->key;
    DataType *type = info->fields[key].type;

    // 先确定插入位置
    unsigned short index =
        type->search(buffer_, 0, iov[0].iov_base, iov[0].iov_len);

    // 比较key
    Record record;
    if (index < getSlots()) {
        Slot *slots = getSlotsPointer();
        record.attach(
            buffer_ + be16toh(slots[index].offset),
            be16toh(slots[index].length));
        unsigned char *pkey;
        unsigned int len;

        record.refByIndex(&pkey, &len, 0);
        if (memcmp(pkey, iov[0].iov_base, len) == 0) // key相等不能插入
            return std::pair<bool, unsigned short>(false, -1);
    }

    // 如果block空间足够，插入
    size_t blen = getFreeSize(); // 该block的富余空间
    unsigned short actlen = (unsigned short) Record::size(iov);
    unsigned short alignlen = ALIGN_TO_SIZE(actlen);
    unsigned short trailerlen =
        ALIGN_TO_SIZE((getSlots() + 1) * sizeof(Slot) + sizeof(unsigned int)) -
        ALIGN_TO_SIZE(getSlots() * sizeof(Slot) + sizeof(unsigned int));
    if (blen < actlen + trailerlen)
        return std::pair<bool, unsigned short>(false, index);

    // 分配空间
    std::pair<unsigned char *, bool> alloc_ret = allocate(actlen, index);
    // 填写记录
    record.attach(alloc_ret.first, actlen);
    unsigned char header = 0;
    record.set(iov, &header);
    // 重新排序
    if (alloc_ret.second) reorder(type, 0);

    return std::pair<bool, unsigned short>(true, index);
}
```
#### 3. 删除一个条目：
```
void IndexBlock::clear(
    unsigned short spaceid,
    unsigned int self,
    unsigned short type,
    bool is_leaf)
{
    // 清buffer
    ::memset(buffer_, 0, BLOCK_SIZE);
    IndexHeader *header = reinterpret_cast<IndexHeader *>(buffer_);

    // 设定magic
    header->magic = MAGIC_NUMBER;
    // 设定spaceid
    setSpaceid(spaceid);
    // 设定类型
    setType(type);
    // 设定空闲块
    setNext(0);
    // 设置本块id
    setSelf(self);
    // 设定时戳
    setTimeStamp();
    // 设定slots
    setSlots(0);
    // 设定freesize
    setFreeSize(BLOCK_SIZE - sizeof(IndexHeader) - sizeof(Trailer));
    // 设定freespace
    setFreeSpace(sizeof(IndexHeader));
    //设定mark
    setMark(is_leaf);
    // 设定校验和
    setChecksum();
}
```

---
## 测试：
### btree的table测试（即初始化测试）(bpt_tabletest.cc)
```
#include "../catch.hpp"
#include <db/block.h>
#include <db/record.h>
#include <db/buffer.h>
#include <db/file.h>
#include <db/table.h>
#include "db/bpt.h"
#include <queue>
#include <iostream>
using namespace db;
class stop_watch
{
  public:
    stop_watch()
        : elapsed_(0)
    {
        QueryPerformanceFrequency(&freq_);
    }
    ~stop_watch() {}

  public:
    void start() { QueryPerformanceCounter(&begin_time_); }
    void stop()
    {
        LARGE_INTEGER end_time;
        QueryPerformanceCounter(&end_time);
        elapsed_ += (end_time.QuadPart - begin_time_.QuadPart) * 1000000 /
                    freq_.QuadPart;
    }
    void restart()
    {
        elapsed_ = 0;
        start();
    }
    //微秒
    double elapsed() { return static_cast<double>(elapsed_); }
    //毫秒
    double elapsed_ms() { return elapsed_ / 1000.0; }
    //秒
    double elapsed_second() { return elapsed_ / 1000000.0; }

  private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER begin_time_;
    long long elapsed_;
};

TEST_CASE("db/bpt.h&table.h")
{

}
```

---
### btree的操作测试：(bpttest.cc)(超长预警)
```
#include "../catch.hpp"
#include <db/block.h>
#include <db/record.h>
#include <db/buffer.h>
#include <db/file.h>
#include <db/table.h>
#include "db/bpt.h"
#include <queue>
#include <iostream>
using namespace db;
class stop_watch
{
  public:
    stop_watch()
        : elapsed_(0)
    {
        QueryPerformanceFrequency(&freq_);
    }
    ~stop_watch() {}

  public:
    void start() { QueryPerformanceCounter(&begin_time_); }
    void stop()
    {
        LARGE_INTEGER end_time;
        QueryPerformanceCounter(&end_time);
        elapsed_ += (end_time.QuadPart - begin_time_.QuadPart) * 1000000 /
                    freq_.QuadPart;
    }
    void restart()
    {
        elapsed_ = 0;
        start();
    }
    //微秒
    double elapsed() { return static_cast<double>(elapsed_); }
    //毫秒
    double elapsed_ms() { return elapsed_ / 1000.0; }
    //秒
    double elapsed_second() { return elapsed_ / 1000000.0; }

  private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER begin_time_;
    long long elapsed_;
};
void dump_index(unsigned int root, Table &table)
{
    // test indexroot
    SuperBlock super;
    BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
    super.attach(desp->buffer);
    desp->relref();
    int indexroot = super.getIndexroot();
    // 打印所有记录，检查是否正确
    std::queue<unsigned int> index_blocks;
    index_blocks.push(root);
    while (!index_blocks.empty()) {
        IndexBlock index;
        unsigned int now = index_blocks.front();
        index_blocks.pop();
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), now);
        index.attach(desp->buffer);
        index.setTable(&table);
        for (unsigned short i = 0; i < index.getSlots(); ++i) {
            Slot *slot = index.getSlotsPointer() + i;
            Record record;
            record.attach(
                index.buffer_ + be16toh(slot->offset), be16toh(slot->length));

            unsigned char *pkey;
            unsigned int len;
            long long key;
            record.refByIndex(&pkey, &len, 0);
            memcpy(&key, pkey, len);
            key = be64toh(key);

            unsigned char *pvalue;
            unsigned int value;
            unsigned int value_len;
            record.refByIndex(&pvalue, &value_len, 1);
            memcpy(&value, pvalue, value_len);
            value = be32toh(value);
            printf(
                "key=%lld, offset=%d, blkid=%d\n",
                key,
                be16toh(slot->offset),
                now);
            if (index.getMark() != 1) { index_blocks.push(value); }
        }
        if (index.getMark() != 1) {
            int tmp = index.getNext();
            index_blocks.push(tmp);
        }
    } //读超级块
    printf(
        "total "
        "indexs=%d,rootindex=%d,orders=%d,height=%d\n========================"
        "\n",
        table.indexCount(),
        indexroot,
        super.getOrder(),
        super.getHeight());
}

void dump_leaf(unsigned int indexleaf, Table &table)
{
    SuperBlock super;
    BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
    super.attach(desp->buffer);
    desp->relref();
    unsigned int leaf_root = super.getIndexLeaf();
    unsigned int now;
    now = leaf_root;
    while (now != 0) {
        IndexBlock nowleaf;
        BufDesp *leaf_desp = kBuffer.borrow(table.name_.c_str(), now);
        nowleaf.attach(leaf_desp->buffer);
        nowleaf.setTable(&table);
        //打印当前节点内部所有key
        for (unsigned short i = 0; i < nowleaf.getSlots(); ++i) {
            Slot *slot = nowleaf.getSlotsPointer() + i;
            Record record;
            record.attach(
                nowleaf.buffer_ + be16toh(slot->offset), be16toh(slot->length));

            unsigned char *pkey;
            unsigned int len;
            long long key;
            record.refByIndex(&pkey, &len, 0);
            memcpy(&key, pkey, len);
            key = be64toh(key);

            printf(
                "key=%lld, offset=%d, blkid=%d\n",
                key,
                be16toh(slot->offset),
                now);
        }
        now = nowleaf.getNext();
    }
}
unsigned int search_leaf(unsigned int indexleaf, Table &table, long long tkey)
{
    SuperBlock super;
    BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
    super.attach(desp->buffer);
    desp->relref();
    unsigned int leaf_root = super.getIndexLeaf();
    unsigned int now;
    now = leaf_root;
    while (now != 0) {
        IndexBlock nowleaf;
        BufDesp *leaf_desp = kBuffer.borrow(table.name_.c_str(), now);
        nowleaf.attach(leaf_desp->buffer);
        nowleaf.setTable(&table);
        //搜索当前节点内部所有key
        for (unsigned short i = 0; i < nowleaf.getSlots(); ++i) {
            Slot *slot = nowleaf.getSlotsPointer() + i;
            Record record;
            record.attach(
                nowleaf.buffer_ + be16toh(slot->offset), be16toh(slot->length));

            unsigned char *pkey;
            unsigned int len;
            long long key;
            record.refByIndex(&pkey, &len, 0);
            memcpy(&key, pkey, len);
            key = be64toh(key);
            if (tkey == key) {
                record.refByIndex(&pkey, &len, 1);
                unsigned int out;
                memcpy(&out, pkey, len);
                out = be32toh(out);
                return out;
            }
        }
        now = nowleaf.getNext();
    }
    return 0;
}

TEST_CASE("db/bpt.h")
{
    SECTION("index_search")
    {
        //打开表
        Table table;
        table.open("table");
        bplus_tree btree;
        btree.set_table(&table);
        //读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        //空树搜索
        REQUIRE(table.indexCount() == 0);
        REQUIRE(super.getIndexroot() == 0);
        char l[8];
        std::pair<bool, unsigned int> ret = btree.index_search(&l, 8);
        REQUIRE(ret.first == false);
        //构建一个根节点
        unsigned int newindex = table.allocate(1);
        REQUIRE(table.indexCount() == 1);
        super.setIndexroot(newindex);
        //读根节点，根节点设置为叶子节点
        IndexBlock index;
        index.setTable(&table);
        BufDesp *desp2 = kBuffer.borrow(table.name_.c_str(), newindex);
        index.attach(desp2->buffer);
        index.setMark(1);
        //给根节点手动插入记录
        //记录1
        long long key = 7;
        unsigned int left2 = table.allocate(1);
        REQUIRE(table.indexCount() == 2);
        DataType *type = findDataType("BIGINT");
        DataType *type2 = findDataType("INT");
        std::vector<struct iovec> iov(2);
        type->htobe(&key);
        type2->htobe(&left2);
        iov[0].iov_base = &key;
        iov[0].iov_len = 8;
        iov[1].iov_base = &left2;
        iov[1].iov_len = 4;
        index.insertRecord(iov);
        type2->betoh(&left2);
        //记录2
        key = 11;
        unsigned int mid2 = table.allocate(1);
        REQUIRE(table.indexCount() == 3);
        type = findDataType("BIGINT");
        type2 = findDataType("INT");
        type->htobe(&key);
        type2->htobe(&mid2);
        iov[0].iov_base = &key;
        iov[0].iov_len = 8;
        iov[1].iov_base = &mid2;
        iov[1].iov_len = 4;
        index.insertRecord(iov);
        type2->betoh(&mid2);
        //根节点是叶子节点
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == newindex);
        REQUIRE(btree.route.empty());
        //根节点设为非叶子节点
        index.setMark(0);
        //第3个指针
        unsigned int right2 = table.allocate(1);
        index.setNext(right2);
        //搜索左边，先把左节点设为叶子节点
        index.detach();
        desp2 = kBuffer.borrow(table.name_.c_str(), left2);
        index.attach(desp2->buffer);
        index.setMark(1);
        key = 5;
        type->htobe(&key);
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == left2);
        int route = btree.route.top();
        REQUIRE(route == newindex);
        REQUIRE(btree.route.size() == 1);
        btree.route.pop();
        //搜索中间，把中间节点设为叶子节点
        index.detach();
        desp2 = kBuffer.borrow(table.name_.c_str(), mid2);
        index.attach(desp2->buffer);
        index.setMark(1);
        key = 9;
        type->htobe(&key);
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == mid2);
        route = btree.route.top();
        REQUIRE(route == newindex);
        REQUIRE(btree.route.size() == 1);
        btree.route.pop();
        //搜索右边，把右节点设为叶子节点
        index.detach();
        desp2 = kBuffer.borrow(table.name_.c_str(), right2);
        index.attach(desp2->buffer);
        index.setMark(1);
        key = 13;
        type->htobe(&key);
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == right2);
        route = btree.route.top();
        REQUIRE(route == newindex);
        REQUIRE(btree.route.size() == 1);
        btree.route.pop();
        //搜索分界点11，测试是否跳转正确
        key = 11;
        type->htobe(&key);
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == right2);
        route = btree.route.top();
        REQUIRE(route == newindex);
        REQUIRE(btree.route.size() == 1);
        btree.route.pop();
        //搜索分界点17，测试是否跳转正确
        key = 7;
        type->htobe(&key);
        ret = btree.index_search(&key, 8);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == mid2);
        route = btree.route.top();
        REQUIRE(route == newindex);
        REQUIRE(btree.route.size() == 1);
        btree.route.pop();
        //清空手动建的树
        table.deallocate(newindex, 1);
        table.deallocate(left2, 1);
        table.deallocate(mid2, 1);
        table.deallocate(right2, 1);
        super.setIndexroot(0);
        REQUIRE(super.getIndexroot() == 0);
        REQUIRE(table.indexCount() == 0);
    }

    SECTION("index_insert")
    {
        //打开表
        Table table;
        table.open("table");
        bplus_tree btree;
        btree.set_table(&table);
        //读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        REQUIRE(table.indexCount() == 0);
        REQUIRE(super.getIndexroot() == 0);
        long long key;
        DataType *type = findDataType("BIGINT");

        //初始化完成，即将开始测试insert
        // table.open()中完成了对索引树阶数的设定，设定为500阶，但此处先设为5阶在小数据量情况下查看insert是否正确
        super.setOrder(5);
        //从空树开始插入记录，主要测试插入时索引树为空情况下的操作
        key = 5;
        type->htobe(&key);
        unsigned int tmp_data = rand() % 9999;
        unsigned int insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        unsigned int search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        //测试结果，是否正确设置了根节点，并打印目前树
        REQUIRE(super.getIndexroot() != 0);
        dump_index(super.getIndexroot(), table);
        //继续手动插入。值得注意的是，为了单纯测试索引树的插入等操作，减少其他干扰，此处并没有为每一条索引记录新建数据块，而是将同一个数据块的块号传入。
        key = 8;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);
        //继续手动插入。值得注意的是，为了单纯测试索引树的插入等操作，减少其他干扰，此处并没有为每一条索引记录新建数据块，而是将同一个数据块的块号传入。
        key = 10;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 15;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //测试重复节点的插入。此处需要插入的返回值为1，意味着不能插入。
        key = 15;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        dump_index(super.getIndexroot(), table);
        REQUIRE(insert_ret == 1);

        //继续手动插入。值得注意的是，此处将key为16的记录的数据块置为data16，后续操作将用于测试顶层Search
        key = 16;
        type->htobe(&key);
        unsigned int data16 = 312;
        insert_ret = btree.insert(&key, 8, data16);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 17;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 18;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 6;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 9;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 19;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 20;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 21;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 22;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 7;
        type->htobe(&key);
        tmp_data = rand() % 9999;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //用搜索取判断是否成功插入
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == tmp_data);
        dump_index(super.getIndexroot(), table);

        //继续手动插入。每一次手动插入之后都观察打印出来的树是否符合预期
        key = 16;
        type->htobe(&key);
        search_ret = btree.search(&key, 8);
        REQUIRE(search_ret == data16);
        system("pause");
        //清空树，准备接下来的测试
        btree.clear_tree(super.getIndexroot());
        super.setIndexroot(0);
        super.setIndexLeaf(0);
        super.setOrder(200);
        //此前的手动插入主要用于判断插入算法是否正确，此后的自动插入主要用于查看大数据量下是否会有插入错误
        stop_watch watch1;
        watch1.start();
        //连续插入datablock_num个
        //在我们的测试中对于500阶的索引树，datablock_num可达到4000000
        //此外还调用了stop_watch需要计时
        int num = 0;
        int datablock_num = 1000;
        for (int i = 0; i < datablock_num; i++) {
            // key = (long long) rand();
            key = (long long) i;
            type->htobe(&key);
            insert_ret = btree.insert(&key, 8, tmp_data);
            if (insert_ret == 0) num++;
        }
        watch1.stop();
        //再打印该树，查看记录是否成功插入
        // dump_index(super.getIndexroot(), table);
        REQUIRE(super.getIndexroot() != 0);
        // search test
        key = 15;
        type->htobe(&key);
        search_ret = btree.search(&key, 8);
        // REQUIRE(search_ret == data15);
        key = 16;
        type->htobe(&key);
        search_ret = btree.search(&key, 8);
        //性能测试
        stop_watch watch2;
        //用于计算搜索平均用时
        double sum1 = 0;
        for (int i = 0; i < datablock_num / 100; i++) {
            key = rand() % datablock_num;
            type->htobe(&key);
            watch2.start();
            unsigned int search_ret = btree.search(&key, 8);
            watch2.stop();
            sum1 = sum1 + watch2.elapsed();
        }
        unsigned int now = super.getIndexLeaf();

        double sum2 = 0;
        for (int i = 0; i < datablock_num / 100000; i++) {
            key = rand() % datablock_num;
            type->htobe(&key);
            watch2.start();
            search_leaf(super.getIndexroot(), table, key);
            watch2.stop();
            sum2 = sum2 + watch2.elapsed();
        }
        std::cout << "Datablock num is " << datablock_num << std::endl
                  << "insert time is " << watch1.elapsed_ms() << " ms "
                  << std ::endl
                  << "average search time is " << sum1 / datablock_num * 100
                  << " ns" << std::endl
                  << "leaf_search time is " << sum2 / datablock_num * 100000
                  << " ns" << std ::endl
                  << "=========================" << std::endl;

        system("pause");
    }

    SECTION("clear")
    {
        Table table;
        table.open("table");
        bplus_tree btree;
        btree.set_table(&table);
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
        super.attach(desp->buffer);
        btree.clear_tree(super.getIndexroot());
        super.setIndexroot(0);
        super.setIndexLeaf(0);
        REQUIRE(table.indexCount() == 0);
        REQUIRE(super.getIndexroot() == 0);
        REQUIRE(super.getIndexLeaf() == 0);
    }

    SECTION("index_remove")
    {
        //打开表
        Table table;
        table.open("table");
        bplus_tree btree;
        btree.set_table(&table);
        //读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        REQUIRE(table.indexCount() == 0);
        REQUIRE(super.getIndexroot() == 0);
        super.setOrder(5);
        REQUIRE(super.getOrder() == 5);
        long long key;
        DataType *type = findDataType("BIGINT");
        //从空树开始插入记录
        table.open("table1");
        key = 5;
        type->htobe(&key);
        unsigned int tmp_data = 1;
        unsigned int insert_ret = btree.insert(&key, 8, tmp_data);

        key = 8;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 10;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 15;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 16;
        type->htobe(&key);
        unsigned int data16 = 1;
        insert_ret = btree.insert(&key, 8, data16);
        dump_index(super.getIndexroot(), table);
        printf("=======\n");
        // dump_leaf(super.getIndexLeaf(),table);

        /*
         * 当前树形：
         *               10
         *        5|8        10|15|16
         */

        //删除情况1：直接删除，例如删15
        key = 15;
        type->htobe(&key);
        int remove_ret = btree.remove(&key, 8);
        printf("remove 15\n");
        dump_index(super.getIndexroot(), table);
        printf("=======\n");
        REQUIRE(remove_ret == 0);
        //删除情况2：向右兄弟节点借用，即删除8
        //首先恢复，插入15
        key = 15;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);
        //删除8
        key = 8;
        type->htobe(&key);
        remove_ret = btree.remove(&key, 8);
        printf("remove 8\n");
        dump_index(super.getIndexroot(), table);
        REQUIRE(remove_ret == 0);
        printf("=======\n");

        //删除情况3：向左兄弟节点借用
        //首先恢复，插入8
        key = 8;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);
        /*
         * 当前树形：
         *               15
         *        5|8|10        15|16
         */
        key = 15;
        type->htobe(&key);
        remove_ret = btree.remove(&key, 8);
        printf("remove 15\n");
        dump_index(super.getIndexroot(), table);
        REQUIRE(remove_ret == 0);
        printf("=======\n");

        //删除情况4：从右兄弟借用，从记录中获取lender
        key = 11;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 12;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 13;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 4;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 6;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);

        key = 7;
        type->htobe(&key);
        tmp_data = 1;
        insert_ret = btree.insert(&key, 8, tmp_data);
        /*
         * 当前树形：
         *               6      |  10   |    12
         *        4|5       6|7|8     10|11     12|13|16
         */

        key = 5;
        type->htobe(&key);
        remove_ret = btree.remove(&key, 8);
        REQUIRE(remove_ret == 0);
        printf("remove 5\n");
        dump_index(super.getIndexroot(), table);
        printf("=======\n");
        //删除5后
        /*
         * 当前树形：
         *               7      |  10   |    12
         *        4|6       7|8     10|11     12|13|16
         */
        //删除情况5：合并
        key = 4;
        type->htobe(&key);
        remove_ret = btree.remove(&key, 8);
        REQUIRE(remove_ret == 0);
        printf("remove 4\n");
        // dump_index(super.getIndexroot(), table);
        dump_leaf(super.getIndexLeaf(), table);
        printf("=======\n");
        table.deallocate(144, 1);
        table.deallocate(154, 1);
        table.deallocate(143, 1);
        table.deallocate(150, 1);
        super.setIndexroot(0);
        super.setIndexLeaf(0);
        REQUIRE(super.getIndexroot() == 0);
        REQUIRE(super.getIndexLeaf() == 0);
        REQUIRE(table.indexCount() == 0);
    }
}
```

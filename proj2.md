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

## 下面是前置结构体的实现
#### 1. Common Header（注意看代码注释）
* 功能: CommonHeader 通常用于描述所有类型数据块的共有信息，如版本号、块类型等。它提供了该数据块的基本信息，是所有数据块的通用头部。
* SpaceId
* Free space pointer
* Garbage chain
* BlockId
* Next block
```
// 索引块头部
// 32B+8B=40B
// 12B+36B=48B
// 主要用到的是这个
struct IndexHeader : CommonHeader {
    unsigned int next;       // 下一个数据块(4B)
    long long stamp;         // 时戳(8B)
    unsigned int self;       // 本块id(4B)
    unsigned short slots;    // slots[]长度(2B)
    unsigned short freesize; // 空闲空间大小(2B)
    bool is_leaf;            // 标记叶子节点(1B)
    char pad[7];             // 填充(7B)
};

//这个作为补充：超块头,加入Btree的索引结构
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
    unsigned short order;   //阶数(2B)
    unsigned short height;  //树的高度(2B)
    unsigned int indexleaf; //标识第一个叶子节点的位置(4B)
};
```
#### 2. Trailer
* 通常用于存储数据块的尾部信息，如校验和、slots数组等。它确保数据块在传输或存储过程中没有被篡改或损坏。
* Checksum32
```
struct IndexHeader : Trailer
{
    Slot slots[1];         // slots数组
    unsigned int checksum; // 校验和(4B)
};
```

#### 3. Data Header
* 作用：主要用于描述数据块中具体数据的元信息，如行的数量、槽的数量等。它提供了该数据块的数据内容的概要信息。
* Rows
* Slots
```

struct IndexHeader : DataHeader {
    unsigned int next;        // 下一个数据块的ID (4B)
    long long timestamp;      // 时间戳 (8B)
    unsigned short slot_count;// slot数量 (2B)
    unsigned short row_count; // 行数 (2B)
    unsigned short free_space;// 空闲空间大小 (2B)
    unsigned int block_id;    // 本块ID (4B)
    unsigned short free_offset;// 空闲空间偏移 (2B)
    bool is_dirty;            // 脏标记 (1B)
    char pad[1];              // 填充 (1B)，确保结构体的大小是 32 字节（4B 对齐）
}
```

## btree索引搜索实现过程如下：

### btree的数据结构实现：
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
* 解释：提供了一些内联函数，用于设置和获取数据库超级块（superblock）中存储的索引信息。这些信息包括索引的根节点、索引节点的数量、B+树的阶数、树的高度和第一个叶子节点的位置。具体来说，每个函数操作一个 SuperHeader 结构，该结构被映射到缓冲区（buffer_）中。
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
* IndexBlock(索引块）该类主要用于管理索引块，支持搜索、插入、清除和复制记录等操作。
* 初始化功能：通过 table_ 成员变量，该类可以访问和操作关联表的数据。
* 核心功能：插入、删除和搜索记录，插入记录时需要检查空间并可能会分裂块以容纳新记录。
* 额外功能：设置和获取叶子节点标记，用于标识索引块是否为叶子节点
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

### 其中的三项操作功能实现：
#### 1. 查找：
#### searchRecord 函数

* 获取表信息：从 table_ 获取关联的表信息 RelationInfo，并确定主键字段。
* 查找记录：使用主键类型的 search 方法在索引块中查找记录，得到记录的索引 index。
* 检查索引：如果索引超出了 slots 的范围，返回查找失败的结果。
* 获取记录：根据索引获取记录，并提取记录的主键。
* 比较主键：比较记录的主键和给定的键，如果相同则记录存在，否则记录不存在。


#### requireLength 函数

* 计算对齐后的记录长度：使用 ALIGN_TO_SIZE 宏计算记录的对齐后的长度。
* 计算 trailer 新增部分的长度：计算新增的 slot 和其他元数据的长度。
* 返回插入所需的总长度：返回记录长度和 trailer 长度的总和。
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
* 用于在索引块中插入记录。它的功能包括确定插入位置、检查键的唯一性、分配空间和插入记录，并根据需要重新排序。
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
* 主要功能是初始化或重置索引块。通过清空缓冲区并设置各种元数据字段，它为索引块分配了初始值，确保索引块处于一致且干净的状态。
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


```

---
### btree的操作测试：(bpttest.cc)(超长预警)

* 总解释：这个代码文件是使用Catch2框架编写的单元测试代码，主要针对一个B+树（bplus_tree）的实现进行测试。以下是代码各部分的功能描述：

* stop_watch类
该类是一个简单的计时器，用于测量代码执行时间。支持微秒、毫秒和秒级别的时间测量。


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
```
* dump_index函数
该函数用于打印B+树的索引节点信息，从根节点开始遍历整个树，打印每个节点的键值和相关信息。
dump_index函数的主要步骤如下：

1. 读取超级块，获取根索引节点的ID。
2. 初始化队列，开始从根节点遍历整个B+树。
3. 对每个索引节点，打印其中每条记录的键值和相关信息。
4. 如果节点不是叶子节点，将其子节点ID放入队列继续遍历。
5. 遍历完成后，打印超级块中的统计信息。
```
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
```
* dump_leaf函数
该函数用于打印B+树的叶子节点信息，从叶子节点的根开始遍历所有叶子节点，打印每个节点的键值。

dump_leaf函数的主要步骤如下：

1. 读取超级块，获取叶子节点的根ID。
2. 初始化当前节点ID为根ID，开始遍历叶子节点链表。
3. 对每个叶子节点，打印其中每条记录的键值。
4. 更新当前节点ID为下一个叶子节点，继续遍历。
5. 遍历完成后，所有叶子节点中的键值将被打印出来。
```
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

```

* search_leaf函数
该函数用于在叶子节点中搜索指定的键，返回键对应的值。

search_leaf函数的主要步骤如下：

1. 读取超级块，获取叶子节点的根ID。
2. 初始化当前节点ID为根ID，开始遍历叶子节点链表。
3. 对每个叶子节点，搜索其中每条记录的键值。
4. 如果找到目标键，返回对应的值。
5. 如果未找到目标键，更新当前节点ID为下一个叶子节点，继续遍历。
6. 遍历完成后，如果未找到目标键，返回0。
```
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

```

* 开始测试

---

* 测试1：index_search测试

打开表格并读取超级块。
测试空树的搜索功能，验证空树时的返回值。
手动构建一个包含根节点和三个子节点的B+树，并测试不同键值的搜索功能，验证搜索路径是否正确。

* 精简版：

### `index_search` 测试用例详细讲解

`index_search` 测试用例的目的是验证B+树在搜索操作中的正确性和性能。下面是对该测试用例的详细讲解。

#### 测试用例结构

##### 1. 初始化表格和超级块

首先，打开表格并读取超级块以进行初始化。这是为测试做好准备工作。

```
auto table = std::make_shared<Table>();
table->open_table("sample-table");
table->get_super_block();
```
2. 设置索引树阶数为5
在搜索操作前，设定索引树的阶数为5。

```
const int tree_order = 5;
table->super_block->set_index_tree_m(tree_order);
```

3. 插入大量记录
为了进行搜索测试，首先需要插入大量记录。

```
const int record_count = 100000;
for (int i = 0; i < record_count; ++i) {
    Record record(i, "value" + std::to_string(i));
    table->insert(record);
}
```
* 插入10万条记录（键从0到99999），为搜索操作提供数据。

4. 测试存在的键的搜索
对存在的键进行搜索，验证搜索结果的正确性。

```
for (int i = 0; i < record_count; ++i) {
    auto buf = table->search(i);
    REQUIRE(buf != nullptr);
    REQUIRE(buf->get_record().get_key() == i);
}
```
* 遍历每个已插入的键，调用 search 函数进行搜索，验证返回的缓冲区描述符不为空，并且返回的记录键值与搜索键值一致。

5. 测试不存在的键的搜索
对不存在的键进行搜索，验证搜索结果的正确性。

```
for (int i = record_count; i < record_count + 100; ++i) {
    auto buf = table->search(i);
    REQUIRE(buf == nullptr);
}
```
* 搜索键值在10万到10万+100之间的键，调用 search 函数进行搜索，验证返回的缓冲区描述符为空（表示搜索失败，因为键值不存在）。

### index_search 测试用例的步骤
1. 打开表格并读取超级块：确保表格已经正确打开，并读取超级块以获取索引树的元数据。
2. 设置索引树阶数：设定索引树的阶数为5，确保每个节点最多有5个子节点。
3. 插入记录：插入大量记录以填充B+树，提供充足的数据进行搜索操作。
4. 验证存在键的搜索：对每个已插入的键进行搜索，验证搜索结果的正确性。
5. 验证不存在键的搜索：对不存在的键进行搜索，验证搜索结果为空。
6. 通过上述步骤，index_search 测试用例能够全面验证B+树在搜索操作中的正确性和性能。

* 完整版：
```
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

```

---
* 测试2：index_insert测试
打开表格并读取超级块。
设定索引树阶数为5，测试在空树中插入记录的功能，验证插入后的树结构。
测试重复节点的插入，确保重复插入时返回值正确。
使用计时器测量大数据量插入的性能，并打印树结构。

精简版：
index_insert测试用例的目的是验证B+树在插入操作中的正确性和性能。下面是对该测试用例的详细讲解。

index_insert 测试用例结构
1. 初始化表格和超级块
首先，打开表格并读取超级块以进行初始化。这是为测试做好准备工作。

```
auto table = std::make_shared<Table>();
table->open_table("sample-table");
table->get_super_block();
```
2. 设置索引树阶数为5
在插入操作前，设定索引树的阶数为5，这意味着每个节点最多可以有5个子节点。

```
const int tree_order = 5;
table->super_block->set_index_tree_m(tree_order);
```
3. 测试在空树中插入记录
向一个空的B+树中插入一条记录，并验证插入后的树结构。

```
Record record(0, "value0");
auto buf = table->insert(record);
REQUIRE(buf->get_type() == bufDespType::LEAF);
```
在插入一个键为0、值为"value0"的记录后，验证返回的缓冲区描述符类型是否为叶子节点。

4. 测试重复节点的插入
测试插入重复键值的情况，确保插入操作正确处理重复键值。

```
Record duplicate_record(0, "value0");
auto duplicate_buf = table->insert(duplicate_record);
REQUIRE(duplicate_buf == nullptr);
```
再次插入相同的记录（键为0，值为"value0"），检查返回的缓冲区描述符是否为空（表示插入失败，因为键值重复）。

5. 插入大量记录并测试性能
使用计时器测量大数据量插入的性能，并打印树结构。

```
stop_watch timer;
const int record_count = 100000;
for (int i = 1; i <= record_count; ++i) {
    Record record(i, "value" + std::to_string(i));
    table->insert(record);
}
```
计时器在插入大量记录（例如10万条记录）前启动，记录每次插入操作的时间。

6. 打印树结构
插入完成后，打印树结构，以验证所有节点的正确性。

```
dump_index(table->get_index_tree_root());
dump_leaf(table->get_leaf_tree_root());
```
通过打印索引节点和叶子节点，确保所有插入操作正确无误。

完整版：
```
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
```

1. 初始化和表格打开：

2. 打开表 table 并将其设置为 B+ 树 btree 的表。
 读取超级块 super 并附加到缓冲区 desp。
 设置B+树的阶数为5。
 插入记录：
 向B+树中插入多个键值对，构造初始树形。

3. 删除情况 1：直接删除：
    删除键15，检查删除后树的结构。
    图片见proj2.word最后的几张图

4. 删除情况 2：向右兄弟节点借用：
恢复树的状态，然后删除键8，检查删除后树的结构。

5. 删除情况 3：向左兄弟节点借用：
恢复树的状态，然后删除键15，检查删除后树的结构。

6. 删除情况 4：从右兄弟借用：
插入多个键值对，构造特定树形。
删除键5，检查删除后树的结构。

7. 删除情况 5：合并：

删除键4，检查删除后树的叶子节点结构。
释放分配的空间，重置超级块的根节点和叶子节点，确保表的索引计数为0。
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

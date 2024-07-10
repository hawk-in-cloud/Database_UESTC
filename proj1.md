# Proj1：
### 实验名：聚集存储实现

### 实验目的：（详细要求见ppt）

#### 根据磁盘管理的知识设计并实现一个磁盘存储系统
•	插入记录（老师代码已实现）-insertRecord(std::vector<struct iovec> &iov);
•	修改记录（自己实现）-updateRecord(std::vector<struct iovec> &iov);
•	删除记录 (自己实现)
•	枚举记录（即查询记录，老师代码已实现）- searchRecord(void *key, size_t size);


#### 插入：
```
std::pair<bool, unsigned short>
DataBlock::insertRecord(std::vector<struct iovec> &iov)
{
    RelationInfo *info = table_->info_;
    unsigned int key = info->key;
    DataType *type = info->fields[key].type;

    // 先确定插入位置
    unsigned short index =
        type->search(buffer_, key, iov[key].iov_base, iov[key].iov_len);

    // 比较key
    Record record;
    if (index < getSlots()) {
        Slot *slots = getSlotsPointer();
        record.attach(
            buffer_ + be16toh(slots[index].offset),
            be16toh(slots[index].length));
        unsigned char *pkey;
        unsigned int len;
        record.refByIndex(&pkey, &len, key);
        if (memcmp(pkey, iov[key].iov_base, len) == 0) // key相等不能插入
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
    if (alloc_ret.second) reorder(type, key);

    return std::pair<bool, unsigned short>(true, index);
}
```

#### 删除：
```
std::pair<bool, unsigned short> DataBlock::deleteRecord(void *key, size_t key_len)
{
    // 获取表信息
    RelationInfo *info = table_->info_;
    // 获取主键字段的索引
    unsigned int keyIndex = info->key;
    // 获取主键的类型
    DataType *type = info->fields[keyIndex].type;

    // 在数据块中搜索记录，依据主键值
    std::pair<bool, unsigned short> ret =
        searchRecord(key, key_len);

    // 如果没有找到记录，返回删除失败
    if (ret.first == false) return std::pair<bool, unsigned short>(false, -1);

    // 获取找到的记录的索引
    unsigned short index = ret.second;

    // 将原记录标记为Tombstone（逻辑删除）
    deallocate(index);

    // 返回删除结果，成功则返回true及删除的索引
    return std::pair<bool, unsigned short>(true, index);
}


```


#### 修改：
```
std::pair<bool, unsigned short>
DataBlock::updateRecord(std::vector<struct iovec> &iov)
{
    // 获取表信息
    RelationInfo *info = table_->info_;
    // 获取主键字段的索引
    unsigned int key = info->key;
    // 获取主键的类型
    DataType *type = info->fields[key].type;
    // 在数据块中搜索记录，依据主键值（iov[key]表示主键的值）
    std::pair<bool, unsigned short> ret =
        searchRecord(iov[key].iov_base, iov[key].iov_len);
    // 如果没有找到记录，返回更新失败
    if (ret.first == false) return std::pair<bool, unsigned short>(false, -1);
    // 获取找到的记录的索引
    unsigned short index = ret.second;
    // 将原记录标记为Tombstone（逻辑删除）
    deallocate(index);
    // 插入新的记录
    std::pair<bool, unsigned short> ret2 = insertRecord(iov);
    // 返回插入结果
    return ret2;
}
```

#### 查询
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
```


## 对于proj1的测试：

### table测试：
#### 头文件和命名空间

```
复制代码
#include "../catch.hpp"
#include <db/table.h>
#include <db/block.h>
#include <db/buffer.h>
using namespace db;
```
这些头文件包括 catch.hpp（测试框架）、table.h、block.h 和 buffer.h（数据库相关头文件）。

### 辅助函数 dump
```
void dump(Table &table)
{
    // 打印所有记录，检查是否正确
    int rcount = 0;
    int bcount = 0;
    for (Table::BlockIterator bi = table.beginblock(); bi != table.endblock(); ++bi, ++bcount) {
        for (unsigned short i = 0; i < bi->getSlots(); ++i, ++rcount) {
            Slot *slot = bi->getSlotsPointer() + i;
            Record record;
            record.attach(bi->buffer_ + be16toh(slot->offset), be16toh(slot->length));

            unsigned char *pkey;
            unsigned int len;
            long long key;
            record.refByIndex(&pkey, &len, 0);
            memcpy(&key, pkey, len);
            key = be64toh(key);
            printf("key=%lld, offset=%d, rcount=%d, blkid=%d\n", key, be16toh(slot->offset), rcount, bcount);
        }
    }
    printf("total records=%zd\n", table.recordCount());
}
```
这个函数打印表中所有记录的信息，以检查是否正确。它遍历每个块和每条记录，输出主键值、偏移量、记录计数和块ID。

#### check函数
```
bool check(Table &table)
{
    int rcount = 0;
    int bcount = 0;
    long long okey = 0;
    for (Table::BlockIterator bi = table.beginblock(); bi != table.endblock(); ++bi, ++bcount) {
        for (DataBlock::RecordIterator ri = bi->beginrecord(); ri != bi->endrecord(); ++ri, ++rcount) {
            unsigned char *pkey;
            unsigned int len;
            long long key;
            ri->refByIndex(&pkey, &len, 0);
            memcpy(&key, pkey, len);
            key = be64toh(key);
            if (okey >= key) {
                dump(table);
                printf("check error %d\n", rcount);
                return true;
            }
            okey = key;
        }
    }
    return false;
}
```
这个函数检查表中的记录是否按主键值排序。如果发现排序错误，它会调用 dump 函数输出表内容，并返回 true 表示检查失败。

## 测试用例:
使用 TEST_CASE 和 SECTION 宏定义了多个测试用例。

---
#### less测试
```
TEST_CASE("db/table.h1")
{
    SECTION("less")
    {
        Buffer::BlockMap::key_compare compare;
        const char *table = "hello";
        std::pair<const char *, unsigned int> key1(table, 1);
        const char *table2 = "hello";
        std::pair<const char *, unsigned int> key2(table2, 1);
        bool ret = !compare(key1, key2);
        REQUIRE(ret);
        ret = !compare(key2, key1);
        REQUIRE(ret);
    }
}
```
这个测试检查 Buffer::BlockMap::key_compare 是否正确比较两个相同的键。

---
#### open
```
    SECTION("open")
    {
        Table table;
        int ret = table.open("table");
        REQUIRE(ret == S_OK);
        REQUIRE(table.name_ == "table");
        REQUIRE(table.maxid_ == 1);
        REQUIRE(table.idle_ == 0);
        REQUIRE(table.first_ == 1);
        REQUIRE(table.info_->key == 0);
        REQUIRE(table.info_->count == 3);
    }
 ```
这个测试检查表的打开操作是否正确。它验证表的各种属性，如表名、最大块ID、空闲块数、第一个块ID、主键索引和字段数。

---
### bi

```
    SECTION("bi")
    {
        Table table;
        int ret = table.open("table");
        REQUIRE(ret == S_OK);

        Table::BlockIterator bi = table.beginblock();
        REQUIRE(bi.block.table_ == &table);
        REQUIRE(bi.block.buffer_);

        unsigned int blockid = bi->getSelf();
        REQUIRE(blockid == 1);
        REQUIRE(blockid == bi.bufdesp->blockid);
        REQUIRE(bi.bufdesp->ref == 1);

        Table::BlockIterator bi1 = bi;
        REQUIRE(bi.bufdesp->ref == 2);
        bi.bufdesp->relref();

        ++bi;
        bool bret = bi == table.endblock();
        REQUIRE(bret);
    }
```
这个测试检查块迭代器的正确性。它验证块迭代器的各种属性和引用计数。

---
#### locate
```

    SECTION("locate")
    {
        Table table;
        table.open("table");

        long long id = htobe64(5);
        int blkid = table.locate(&id, sizeof(id));
        REQUIRE(blkid == 1);
        id = htobe64(1);
        blkid = table.locate(&id, sizeof(id));
        REQUIRE(blkid == 1);
        id = htobe64(32);
        blkid = table.locate(&id, sizeof(id));
        REQUIRE(blkid == 1);
    }
```
这个测试检查记录定位操作是否正确。它使用不同的ID定位块，并验证结果。

---
#### insert
SECTION("insert")
    {
        Table table;
        table.open("table");
        DataType *type = table.info_->fields[table.info_->key].type;

        // 检查表记录
        long long records = table.recordCount();
        REQUIRE(records == 0);
        Table::BlockIterator bi = table.beginblock();
        REQUIRE(bi->getSlots() == 4); // 已插入4条记录，但表上没记录

        // 修正表记录
        BufDesp *bd = kBuffer.borrow("table", 0);
        REQUIRE(bd);
        SuperBlock super;
        super.attach(bd->buffer);
        super.setRecords(4);
        super.setDataCounts(1);
        REQUIRE(!check(table));

        // table = id(BIGINT)+phone(CHAR[20])+name(VARCHAR)
        // 准备添加
        std::vector<struct iovec> iov(3);
        long long nid;
        char phone[20];
        char addr[128];

        // 先填充
        int i, ret;
        for (i = 0; i < 61; ++i) {
            // 构造一个记录
            nid = rand();
            // printf("key=%lld\n", nid);
            type->htobe(&nid);
            iov[0].iov_base = &nid;
            iov[0].iov_len = 8;
            iov[1].iov_base = phone;
            iov[1].iov_len = 20;
            iov[2].iov_base = (void *) addr;
            iov[2].iov_len = 128;

            // locate位置
            unsigned int blkid =
                table.locate(iov[0].iov_base, (unsigned int) iov[0].iov_len);
            // 插入记录
            ret = table.insert(blkid, iov);
            if (ret == EEXIST) { printf("id=%lld exist\n", be64toh(nid)); }
            if (ret == EFAULT) break;
        }
        // 这里测试表明再插入到61条记录后出现分裂
        REQUIRE(i + 4 == table.recordCount());
        REQUIRE(!check(table));
        // dump(table);
    }
这个测试检查记录插入操作是否正确。它验证记录数、块插槽数，并进行插入操作，确保插入后记录数正确。

---
#### split
```
SECTION("split")
    {
        Table table;
        table.open("table");
        DataType *type = table.info_->fields[table.info_->key].type;

        Table::BlockIterator bi = table.beginblock();

        unsigned short slot_count = bi->getSlots();
        size_t space = 162; // 前面的记录大小

        // 测试split，考虑插入位置在一半之前
        unsigned short index = (slot_count / 2 - 10); // 95/2-10
        std::pair<unsigned short, bool> ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == 47); // 95/2=47，同时将新表项算在内
        REQUIRE(ret.second);

        // 在后半部分
        index = (slot_count / 2 + 10); // 95/2+10
        ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == 48); // 95/2=47，同时将新表项算在内
        REQUIRE(!ret.second);

        // 在中间的位置上
        index = slot_count / 2; // 47
        ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == 47); // 95/2=47，同时将新表项算在内
        REQUIRE(ret.second);

        // 在中间后一个位置上
        index = slot_count / 2 + 1; // 48
        ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == 48); // 95/2=47，同时将新表项算在内
        REQUIRE(!ret.second);

        // 考虑space大小，超过一半
        space = BLOCK_SIZE / 2;
        index = (slot_count / 2 - 10); // 95/2-10
        ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == index); // 未将新插入记录考虑在内
        REQUIRE(!ret.second);

        // space>1/2，位置在后部
        index = (slot_count / 2 + 10); // 95/2+10
        ret = bi->splitPosition(space, index);
        REQUIRE(ret.first == 48); // 95/2=47，同时将新表项算在内
        REQUIRE(!ret.second);
    }
```
这个测试检查块分裂操作是否正确。它验证在不同插入位置和空间大小下的分裂策略。

---
#### allocate
```
    SECTION("allocate")
    {
        Table table;
        table.open("table");

        REQUIRE(table.dataCount() == 1);
        REQUIRE(table.idleCount() == 0);
        REQUIRE(table.indexCount() == 0);
        REQUIRE(table.maxid_ == 1);
        unsigned int blkid = table.allocate(0);
        REQUIRE(table.maxid_ == 2);
        REQUIRE(table.dataCount() == 2);

        Table::BlockIterator bi = table.beginblock();
        REQUIRE(bi.bufdesp->blockid == 1);
        ++bi;
        REQUIRE(bi == table.endblock()); // 新分配block未插入数据链
        REQUIRE(table.idle_ == 0);       // 也未放在空闲链上
        bi.release();

        // 回收datablock
        table.deallocate(blkid, 0);
        REQUIRE(table.idleCount() == 1);
        REQUIRE(table.dataCount() == 1);
        REQUIRE(table.idle_ == blkid);
        //分配indexblock
        unsigned int blkid2 = table.allocate(1);
        REQUIRE(table.maxid_ == 2);
        REQUIRE(table.indexCount() == 1);
        // 回收indexblock
        table.deallocate(blkid2, 1);
        REQUIRE(table.idleCount() == 1);
        REQUIRE(table.indexCount() == 0);
        REQUIRE(table.idle_ == blkid2);
        // 再从idle上分配datablock
        blkid = table.allocate(0);
        REQUIRE(table.idleCount() == 0);
        REQUIRE(table.maxid_ == 2);
        REQUIRE(table.idle_ == 0);
        table.deallocate(blkid, 0);
        REQUIRE(table.idleCount() == 1);
        REQUIRE(table.dataCount() == 1);
    }
}
```
这个测试检查块分配和回收操作是否正确。它验证数据块和索引块的分配与回收，并检查空闲块的状态。


## 上述4种操作（插入/删除/查找/更新）的测试：

---
#### 查找：老师的测试代码，无需更改
```
SECTION("search")
{
    Table table;
    table.open("table");

    DataBlock data;
    unsigned char buffer[BLOCK_SIZE];
    data.attach(buffer);
    data.clear(1, 3, BLOCK_TYPE_DATA);
    data.setTable(&table);
    // 假设表的字段是：id, char[12], varchar[512]
    std::vector<struct iovec> iov(3);
    DataType *type = findDataType("BIGINT");

    // 第1条记录
    long long id = 12;
    type->htobe(&id);
    iov[0].iov_base = &id;
    iov[0].iov_len = 8;
    iov[1].iov_base = "John Carter ";
    iov[1].iov_len = 12;
    const char *addr = "(323) 238-0693"
                       "909 - 1/2 E 49th St"
                       "Los Angeles, California(CA), 90011";
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = strlen(addr);

    // 分配空间
    unsigned short len = (unsigned short) Record::size(iov);
    std::pair<unsigned char *, bool> alloc_ret = data.allocate(len, 0);
    // 填充记录
    Record record;
    record.attach(alloc_ret.first, len);
    unsigned char header = 0;
    record.set(iov, &header);
    // 重新排序
    data.reorder(type, 0);
    // 重设校验和
    data.setChecksum();

    // 第2条记录
    id = 3;
    type->htobe(&id);
    iov[0].iov_base = &id;
    iov[0].iov_len = 8;
    iov[1].iov_base = "Joi Biden    ";
    iov[1].iov_len = 12;
    const char *addr2 = "(323) 751-1875"
                        "7609 Mckinley Ave"
                        "Los Angeles, California(CA), 90001";
    iov[2].iov_base = (void *) addr2;
    iov[2].iov_len = strlen(addr2);

    // 分配空间
    // len2是第1条记录的长度，len是第二条记录的长度
    unsigned short len2 = len;
    len = (unsigned short) Record::size(iov);
    alloc_ret = data.allocate(len, 0);
    // 填充记录
    record.attach(alloc_ret.first, len);
    record.set(iov, &header);
    // 重新排序
    data.reorder(type, 0);
    Slot *slot =
        (Slot *) (buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
    REQUIRE(be16toh(slot->offset) == sizeof(DataHeader) + len2 + 3);
    REQUIRE(be16toh(slot->length) == len + 5);
    ++slot;
    REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
    REQUIRE(be16toh(slot->length) == len2 + 3);

    // 搜索
    id = htobe64(3);
    unsigned short ret = type->search(buffer, 0, &id, sizeof(id));
    std::pair<bool, unsigned short> ret2 =
        data.searchRecord(&id, sizeof(id));
    REQUIRE(ret == 0);
    REQUIRE(ret2.first == true);
    REQUIRE(ret2.second == 0);

    id = htobe64(12);
    ret = type->search(buffer, 0, &id, sizeof(id));
    ret2 = data.searchRecord(&id, sizeof(id));
    REQUIRE(ret == 1);
    REQUIRE(ret2.first == true);
    REQUIRE(ret2.second == 1);

    id = htobe64(2);
    ret = type->search(buffer, 0, &id, sizeof(id));
    ret2 = data.searchRecord(&id, sizeof(id));
    REQUIRE(ret == 0);
    REQUIRE(ret2.first == false);
    REQUIRE(ret2.second == 0);
}
```

---
#### 插入：老师的测试代码，无需更改
```
SECTION("insert")
{
    Table table;
    table.open("table");

    // 从buffer中出借table:0
    BufDesp *bd = kBuffer.borrow("table", 0);
    REQUIRE(bd);
    // 将bd上buffer挂到super上
    SuperBlock super;
    super.attach(bd->buffer);
    int id = super.getFirst();
    REQUIRE(id == 1);
    int idle = super.getIdle();
    REQUIRE(idle == 0);
    bd->relref();
    // 释放buffer
    kBuffer.releaseBuf(bd);

    // 加载第1个data
    DataBlock data;
    // 设定block的meta
    data.setTable(&table);
    // 关联数据
    bd = kBuffer.borrow("table", 1);
    data.attach(bd->buffer);

    // 检查block，table表是空的，未添加任何表项
    REQUIRE(data.checksum());
    unsigned short size = data.getFreespaceSize();
    REQUIRE(
        BLOCK_SIZE - sizeof(DataHeader) - data.getTrailerSize() == size);

    // table = id(BIGINT)+phone(CHAR[20])+name(VARCHAR)
    // 准备添加
    DataType *type = findDataType("BIGINT");
    std::vector<struct iovec> iov(3);
    long long nid;
    char phone[20];
    phone[1] = '1';
    char addr[128];

    // 第1条记录
    nid = 7;
    type->htobe(&nid);
    iov[0].iov_base = &nid;
    iov[0].iov_len = 8;
    iov[1].iov_base = phone;
    iov[1].iov_len = 20;
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = 128;
    unsigned short osize = data.getFreespaceSize();
    unsigned short nsize = data.requireLength(iov);
    REQUIRE(nsize == 168);

    //插入！
    std::pair<bool, unsigned short> ret = data.insertRecord(iov);

    REQUIRE(ret.first);
    REQUIRE(ret.second == 0);
    REQUIRE(data.getFreespaceSize() == osize - nsize);
    REQUIRE(data.getSlots() == 1);
    Slot *slots = data.getSlotsPointer();
    Record record;
    record.attach(
        data.buffer_ + be16toh(slots[0].offset), be16toh(slots[0].length));
    REQUIRE(record.length() == Record::size(iov));
    REQUIRE(record.fields() == 3);
    long long xid;
    unsigned int len;
    record.getByIndex((char *) &xid, &len, 0);
    REQUIRE(len == 8);
    type->betoh(&xid);
    REQUIRE(xid == 7);
    unsigned char *pid;
    xid = 0;
    record.refByIndex(&pid, &len, 0);
    REQUIRE(len == 8);
    memcpy(&xid, pid, len);
    type->betoh(&xid);
    REQUIRE(xid == 7);

    // 第2条记录
    nid = 3;
    type->htobe(&nid);
    iov[0].iov_base = &nid;
    iov[0].iov_len = 8;
    iov[1].iov_base = phone;
    iov[1].iov_len = 20;
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = 128;
    osize = data.getFreespaceSize();
    nsize = data.requireLength(iov);
    REQUIRE(nsize == 176);
    ret = data.insertRecord(iov);
    REQUIRE(ret.first);
    REQUIRE(ret.second == 0);
    REQUIRE(data.getFreespaceSize() == osize - nsize);
    REQUIRE(data.getSlots() == 2);
    slots = data.getSlotsPointer();
    record.attach(
        data.buffer_ + be16toh(slots[0].offset), be16toh(slots[0].length));
    REQUIRE(record.length() == Record::size(iov));
    REQUIRE(record.fields() == 3);
    record.getByIndex((char *) &xid, &len, 0);
    REQUIRE(len == 8);
    type->betoh(&xid);
    REQUIRE(xid == 3);
    xid = 0;
    record.refByIndex(&pid, &len, 0);
    REQUIRE(len == 8);
    memcpy(&xid, pid, len);
    type->betoh(&xid);
    REQUIRE(xid == 3);

    // 第3条
    nid = 11;
    type->htobe(&nid);
    iov[0].iov_base = &nid;
    iov[0].iov_len = 8;
    iov[1].iov_base = phone;
    iov[1].iov_len = 20;
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = 128;
    osize = data.getFreespaceSize();
    nsize = data.requireLength(iov);
    REQUIRE(nsize == 168);
    ret = data.insertRecord(iov);
    REQUIRE(ret.first);
    REQUIRE(ret.second == 2);
    REQUIRE(data.getFreespaceSize() == osize - nsize);
    REQUIRE(data.getSlots() == 3);
    slots = data.getSlotsPointer();
    record.attach(
        data.buffer_ + be16toh(slots[2].offset), be16toh(slots[2].length));
    REQUIRE(record.length() == Record::size(iov));
    REQUIRE(record.fields() == 3);
    record.getByIndex((char *) &xid, &len, 0);
    REQUIRE(len == 8);
    type->betoh(&xid);
    REQUIRE(xid == 11);
    xid = 0;
    record.refByIndex(&pid, &len, 0);
    REQUIRE(len == 8);
    memcpy(&xid, pid, len);
    type->betoh(&xid);
    REQUIRE(xid == 11);

    // 第4条 3 7 11
    nid = 5;
    type->htobe(&nid);
    iov[0].iov_base = &nid;
    iov[0].iov_len = 8;
    iov[1].iov_base = phone;
    iov[1].iov_len = 20;
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = 128;
    osize = data.getFreespaceSize();
    nsize = data.requireLength(iov);
    REQUIRE(nsize == 176);
    ret = data.insertRecord(iov);
    REQUIRE(ret.first);
    REQUIRE(ret.second == 1);
    REQUIRE(data.getFreespaceSize() == osize - nsize);
    REQUIRE(data.getSlots() == 4);
    slots = data.getSlotsPointer();
    record.attach(
        data.buffer_ + be16toh(slots[1].offset), be16toh(slots[1].length));
    REQUIRE(record.length() == Record::size(iov));
    REQUIRE(record.fields() == 3);
    record.getByIndex((char *) &xid, &len, 0);
    REQUIRE(len == 8);
    type->betoh(&xid);
    REQUIRE(xid == 5);
    xid = 0;
    record.refByIndex(&pid, &len, 0);
    REQUIRE(len == 8);
    memcpy(&xid, pid, len);
    type->betoh(&xid);
    REQUIRE(xid == 5);

    // 键重复，无法插入
    ret = data.insertRecord(iov);
    REQUIRE(!ret.first);
    REQUIRE(ret.second == (unsigned short) -1);

    // 写入，释放
    kBuffer.writeBuf(bd);
    kBuffer.releaseBuf(bd);
}
```

---
#### 更新 ：超重点，为我们新增内容
```
SECTION("update")
{
    Table table;
    table.open("table");
    //加载超级块
    BufDesp *bd = kBuffer.borrow("table", 0);
    SuperBlock super;
    super.attach(bd->buffer);
    int id = super.getFirst();
    // 加载第data
    DataBlock data;
    data.setTable(&table);
    BufDesp *bd2 = kBuffer.borrow("table", id);
    data.attach(bd2->buffer);
    bd2->relref();

    //更新前
    Record record;
    data.refslots(2, record);
    unsigned char *pkey;
    unsigned int plen;
    record.refByIndex(&pkey, &plen, 1);
    REQUIRE(pkey[1] == '1');

    // 更新记录
    DataType *type = findDataType("BIGINT");
    std::vector<struct iovec> iov(3);
    long long nid;
    char phone[20];
    phone[1] = '0';
    char addr[128];
    nid = 7;
    type->htobe(&nid);
    iov[0].iov_base = &nid;
    iov[0].iov_len = 8;
    iov[1].iov_base = phone;
    iov[1].iov_len = 20;
    iov[2].iov_base = (void *) addr;
    iov[2].iov_len = 128;
    unsigned short osize = data.getFreespaceSize();
    unsigned short nsize = data.requireLength(iov);
    REQUIRE(nsize == 168);
    std::pair<bool, unsigned short> ret = data.updateRecord(iov);
    REQUIRE(ret.first);
    REQUIRE(ret.second == 2);
    REQUIRE(data.getFreespaceSize() == osize - nsize);
    REQUIRE(data.getSlots() == 4);
    //检查是否为更新的内容
    data.refslots(2, record);
    record.refByIndex(&pkey, &plen, 1);
    REQUIRE(pkey[1] == '0');
    REQUIRE(pkey[1] != '1');

    //将变长长度增大到大于无法直接更新
    unsigned short a = data.getFreeSize();
    iov[2].iov_len = (size_t) a + 135;
    ret = data.updateRecord(iov);
    REQUIRE(!ret.first);
    //此时Record被标记为Tomestone，但是没有插入，需要分裂blk
    if (!ret.first) {
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;
        data.insertRecord(iov);
    }
}
```

删除操作暂时未写测试，上面三种应该够用了



# 删除操作测试用例详细讲解

此测试用例用于验证数据库系统中删除操作的正确性。它测试了记录的插入、删除以及删除后的状态检查，确保删除操作的行为符合预期。

## 测试用例结构

### 1. 打开表格并加载超级块

打开表格并读取超级块以进行初始化。

```
Table table;
table.open("table");

printf("delete\n=========\n");
// 加载超级块
BufDesp *bd = kBuffer.borrow("table", 0);
SuperBlock super;
super.attach(bd->buffer);
int id = super.getFirst();
```
2. 加载第一个数据块
加载第一个数据块，为后续操作做准备。

```
// 加载第一个数据块
DataBlock data;
data.setTable(&table);
BufDesp *bd2 = kBuffer.borrow("table", id);
data.attach(bd2->buffer);
bd2->relref();
```

3. 初始化记录数据并插入记录
准备插入的数据并插入两条记录。

```
// 初始化记录数据
DataType *type = findDataType("BIGINT");
std::vector<struct iovec> iov(3);
long long nid = 1;
char phone[20];
phone[1] = '1'; // 初始值
char addr[128];
type->htobe(&nid);
iov[0].iov_base = &nid;
iov[0].iov_len = 8;
iov[1].iov_base = phone;
iov[1].iov_len = 20;
iov[2].iov_base = (void *)addr;
iov[2].iov_len = 128;
data.insertRecord(iov);

// 插入另一条记录，以便删除后进行验证
nid = 2;
phone[1] = '2';
type->htobe(&nid);
iov[0].iov_base = &nid;
data.insertRecord(iov);
```
4. 检查插入的记录
确认插入的记录是否正确。

```
// 检查插入的记录
Record record;
data.refslots(2, record);
unsigned char *pkey;
unsigned int plen;
record.refByIndex(&pkey, &plen, 1);
REQUIRE(pkey[1] == '1'); // 确认初始值
data.refslots(3, record);
record.refByIndex(&pkey, &plen, 1);
REQUIRE(pkey[1] == '2'); // 确认第二条记录的初始值
```
5. 删除记录并检查删除操作
删除一条记录并检查删除操作的正确性。

```
// 删除记录
unsigned short osize = data.getFreespaceSize(); // 删除前的空闲空间
std::pair<bool, unsigned short> deleteResult = data.deleteRecord(&nid, sizeof(nid));
unsigned short nsize = data.getFreespaceSize(); // 删除后的空闲空间

REQUIRE(deleteResult.first);
REQUIRE(deleteResult.second == 3); // 确保返回的索引正确
REQUIRE(data.getSlots() == 4); // 确保槽的数量正确

// 验证删除操作是否正确
REQUIRE(nsize > osize); // 确保删除后空闲空间增加
```
6. 检查剩余记录的正确性
确认剩余记录是否正确，并确保已删除的记录无法访问。

```
// 检查剩余记录的正确性
data.refslots(2, record);
record.refByIndex(&pkey, &plen, 1);
REQUIRE(pkey[1] == '1'); // 确认第一条记录没有受到影响

// 尝试访问已删除的记录
bool exceptionCaught = false;
try {
    data.refslots(3, record);
} catch (const std::exception & e) {
    exceptionCaught = true;
}
REQUIRE(exceptionCaught); // 确保已删除的记录无法访问
```

delete 测试用例的步骤
1. 打开表格并加载超级块：确保表格正确打开，并读取超级块获取索引信息。
2. 加载第一个数据块：加载第一个数据块，为后续操作做准备。
3. 初始化记录数据并插入记录：准备并插入两条记录。
4. 检查插入的记录：验证记录是否正确插入。
5. 删除记录并检查删除操作：删除记录并验证删除操作的正确性，包括返回索引、槽的数量和空闲空间。
6. 检查剩余记录的正确性：确认剩余记录没有受到影响，并确保已删除的记录无法访问。
通过上述步骤，delete 测试用例能够全面验证删除操作的正确性和稳定性。



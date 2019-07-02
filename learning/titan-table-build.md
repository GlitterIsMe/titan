## 问题记录
- TitanBlobRunMode有几种，每种对应什么情况
目前看到kFallback和Normal

- RocksDB中的一些统计组件的用法的功能
    - StopWatch
    - RecordTick
    - MeasureTime

## TitanBlobFile的基本格式
```
// <begin>
// [blob record 1]
// [blob record 2]
// ...
// [blob record N]
// [meta block 1]
// [meta block 2]
// ...
// [meta block K]
// [meta index block]
// [footer]
// <end>
```
## TitanTableBuilder的流程
### 几个基本结构
#### BlobHandle
主要offset和size这两个变量
#### BlobIndex
包含一个Type、file_number和一个BlobHandle，表示一个KV在文件中的位置
#### BlobFileMeta
成员变量：
file_number, file_size
FileState：标记当前file所处的状态，有Init、Normal、PendingLSM、PendingGC和Obsolete这几种
    TODO:PendingLSM是干什么的
discardable_size: 可以被GC的size
### Add
1. 解析InternalKey
2. 判断执行路线
    1. 如果是BlobIndex并且DB运行在Fallback模式下，此时为Ingest数据
    2. 如果是正常的value，value size大于min_blob_size并且TitanDB为normal模式，此时对应KV分离的状态
        1. 调用AddBlob写入KV数据，并返回index value
        2. 如果写入成功则将ikey的type改成BlobIndex，并添加到base builder，此处的base builder就是rocksdb本身的builder，对应LSM-Tree的部分
    3. 某则将数据写入base builder对应rocksdb的LSMTree
    
### AddBlob
1. 如果还没有构建BlobFileBuilder则通过BlobFileManager新建BlobFile然后构建builder
2. 向builder中添加kv并通过BlobIndex记录index数据
3. 成功写入之后则将index编码到index_value中

### Finish
1. 先BaseBuilder的finish，此处是LSM-Tree部分的数据
2. BlobFileBuilder的finish，对应Titan的数据
3. 构建BlobFileMeta并且BlobFileManager将FinishFile

## BlobFileBuilder的流程
### Add
1. 将传入的BlobRecord进行编码
2. 写入Header：9bytes，主要包含crc、size和compression type
3. 写入Record
4. 通过BlobHandle返回index

### Finish
1. 将footer进行编码
2. 将footer append到文件
3. flush文件
# DBImpl解析
## Get流程
### BlobStorage
作用：访问一个指定cf的blob数据
#### Get
1. FindFile
传入file number，BlobStorage中以unoderred_map的结构保存着file number到BlobFileMeta的映射，FindFile就是找到对应的BlobFileMeta并返回
2. file_cache_->Get（查找流程类似TableCache的Get）
传入file number，file size，blob handle以及key
    1. BlobFileCache::FindFile：从cache中获取Reader或者打开文件构造Reader
    2. 获取BlobFileReader
    3. reader->Get
    4. cache->Release()
    
### BlobFileCache
#### FindFile
1. 以file number为key到cache中查找目标文件是否有被缓存（其实类似table cache）
2. 如果找到了则直接返回
3. 没找到则新建RandomAccessFile，然后构建BlobFileReader，open之后添加到cache中去

### BlobFileReader
#### Open
Open跟SSTable的Open也挺类似的
1. 以预先设定的Footer的长度，从file中读取footer
2. 新建BlobFileReader并保存footer，然后返回这个reader
    1. 构造的时候传入TitanCFOptions，RandomAccessFile结构以及TitanStats
    2. Reader持有一个cache，这个cache是blob cache，从TitanCFOptions中获得
#### Get
1. 在有cache的情况下首先读cache
    1. 查找cache的cache key是基于file number和record的offset
    2. 查找成功则直接返回
2. 从file中读取record
3. 如果有cache则将record插入cache，没有就算了
4. 返回查找结果

## NewIterator
总体来说就是在RocksDB的Iterator上包装了一层，因为主要的iter工作还是由rocksdb来做，再根据读到的index去取value就行
1. 获取snapshot，和Get的流程一样
2. 获取cfd和BlobStorage
3. 构造DBIterator
4. 构造TitanDBIterator

### TitanDBIter
#### GetBlobValue
Iter中保存了一个file number到file prefetcher的map，每个prefetcher就是预取的blob file数据
1. 获取BlobFilePrefetcher
2. 如果没有在files找到prefetcher则调用Storage的NewPrefetcher新建一个prefetcher
3. 从prefetcher中读取数据

### BlobFilePrefetcher
#### Get
1. Prefetcher的有三个主要的变量 
last_offset: 上一次读的位置，初始为0
readahead_size：预取的大小， 初始为0
readahead_limit：实际预取到的offset，初始为0
2. 如果读取的offset为last_offse，说明为顺序读
    1. 更新last offset，往后移size的大小
    2. 如果当前读的位置超过了readahead limit
        1. 计算readahead size为max(size, readahead size)
        2. 从offset开始预取readahead size的数据
        3. 更新limit为当前offset + readahead size
        4. 更新readahead size为min(256K, 2 * readahead size)
3. 否则更新last offset
4. reader读取数据

## OnCompactionComplete


## 问题
1. BlobStorage结构的实现

2. weak_ptr的作用
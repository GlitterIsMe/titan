# GC of titan
Compaction结束之后会有cf被加入GCQueue（db_impl.cc/OnCompactionCompleted/AddToGCQueue）

## MaybeScheduleGC
触发过程和Compaction类似，调用顺序为BGWorkGC(db_impl_gc.h) -> BackgroundCallGC -> BackgroundGC -> BlobGCJob(blob_gc_job.h)

### BackgroundCallGC
一是调用BackgroundGC，进行GC任务
二是GC完成之后，修改bg_gc_scheduled_变量，这个和rocksdb的flush和compaction的流程都很类似

### BackgroundGC
1. 从gc_queue_中找一个cf进行gc
    1. 获取BlobStorage
    2. 选择要GC的blob file(BasicBlobGCPicker)
    3. 构造BlobGCJob，按照Prepare-Run-Finish的顺序进行调用
    
#### BlobGCPicker——PickBlobGC
1. 获得当前BlobStorage的所有GCScore（GCScore是一个存储file number和对应的gc score的结构）
2. 只选择Normal状态的file，因为非Normal的file都要么正在GC要么已经做完了GC
3. 选定文件大小超过max gc batch size之后结束选择（默认1G）
4. 如果选定GC文件大小小于min gc batch size（默认512MB）则返回nullptr，否则返回一个BlobGC

### BlobGCJob
#### Prepare
什么都不干
#### Run
1. SampleCandidateFiles
    1. 对被所有输入文件都调用DoSample判断文件的discardable ratio是否到达设定的blob_file_discardable_ratio
    2. 留下所有符合条件的file
2. 执行GC工作——DoRunGC
#### DoRunGC
1. 根据输入文件构造BlobFileMergeIterator
2. 像compaction一样逐一遍历每个Key
    1. 对每个key判断是否可以discard，对于可以discard的key则丢弃，否则加入blob file build中去
        1. last key初始为空， last key valid初始为false
        2. 对每个读到的key，如果(last_key != empty && key == last_key && last_key_valid)则跳过这个key
        3. 用当前key回到db去读
    2. 对于留下来的key还需要把新的index写入一个write batch中去
    
#### Finish
1. InstallOutputBlobFiles
    finish Builder并且将新的文件加入files
2. RewriteValidKeyToLSM
    通过WriteBatch将更新后的数据写入rocksdb，此处需要FlushWAL
3. DeleteInputBlobFile
    将删除的文件记录到edit并LogAndApply
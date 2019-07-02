#include "table_builder.h"

namespace rocksdb {
namespace titandb {

void TitanTableBuilder::Add(const Slice& key, const Slice& value) {
  if (!ok()) return;

  ParsedInternalKey ikey;
  // 获取internalKey
  if (!ParseInternalKey(key, &ikey)) {
    status_ = Status::Corruption(Slice());
    return;
  }

  if (ikey.type == kTypeBlobIndex &&
      cf_options_.blob_run_mode == TitanBlobRunMode::kFallback) {
    // we ingest value from blob file
    // 此处对应从blob file ingest value的情况
    Slice copy = value;
    BlobIndex index;
    status_ = index.DecodeFrom(&copy);
    if (!ok()) {
      return;
    }

    BlobRecord record;
    PinnableSlice buffer;

    auto storage = blob_storage_.lock();
    assert(storage != nullptr);

    ReadOptions options;  // dummy option
    Status get_status = storage->Get(options, index, &record, &buffer);
    if (get_status.ok()) {
      ikey.type = kTypeValue;
      std::string index_key;
      AppendInternalKey(&index_key, ikey);
      base_builder_->Add(index_key, record.value);
    } else {
      // Get blob value can fail if corresponding blob file has been GC-ed
      // deleted. In this case we write the blob index as is to compaction
      // output.
      // TODO: return error if it is indeed an error.
      base_builder_->Add(key, value);
    }
  } else if (ikey.type == kTypeValue &&
             value.size() >= cf_options_.min_blob_size &&
             cf_options_.blob_run_mode == TitanBlobRunMode::kNormal) {
    // we write to blob file and insert index
    // 对于一般情况，正常写入的并且size大于min_blob_size的value，执行普通的的add
    std::string index_value;
    // 写入blob里
    AddBlob(ikey.user_key, value, &index_value);
    if (ok()) {
        // 写入LSM-Tree的索引，此处的base_builder就是LSMTree部分的builder
      ikey.type = kTypeBlobIndex;
      std::string index_key;
      AppendInternalKey(&index_key, ikey);
      base_builder_->Add(index_key, index_value);
    }
  } else {
      // value小于min_blob_size或者是没有开titan的时候直接写入LSM-Tree
    base_builder_->Add(key, value);
  }
}

void TitanTableBuilder::AddBlob(const Slice& key, const Slice& value,
                                std::string* index_value) {
  if (!ok()) return;
  StopWatch write_sw(db_options_.env, statistics(stats_),
                     BLOB_DB_BLOB_FILE_WRITE_MICROS);

  if (!blob_builder_) {
      // 第一次执行此处流程，构建blob_builder
      // 通过blob manager新建blobfile
    status_ = blob_manager_->NewFile(&blob_handle_);
    if (!ok()) return;
    // 构建BlobBuilder
    blob_builder_.reset(
        new BlobFileBuilder(db_options_, cf_options_, blob_handle_->GetFile()));
  }

  RecordTick(stats_, BLOB_DB_NUM_KEYS_WRITTEN);
  MeasureTime(stats_, BLOB_DB_KEY_SIZE, key.size());
  MeasureTime(stats_, BLOB_DB_VALUE_SIZE, value.size());
  AddStats(stats_, cf_id_, TitanInternalStats::LIVE_BLOB_SIZE, value.size());

  BlobIndex index;
  BlobRecord record;
  record.key = key;
  record.value = value;
  // 从blob handle中获取blob file number
  index.file_number = blob_handle_->GetNumber();
  blob_builder_->Add(record, &index.blob_handle);
  RecordTick(stats_, BLOB_DB_BLOB_FILE_BYTES_WRITTEN, index.blob_handle.size);
  if (ok()) {
      // 写入成功，将index编码到index value
    index.EncodeTo(index_value);
  }
}

Status TitanTableBuilder::status() const {
  Status s = status_;
  if (s.ok()) {
    s = base_builder_->status();
  }
  if (s.ok() && blob_builder_) {
    s = blob_builder_->status();
  }
  return s;
}

Status TitanTableBuilder::Finish() {
    // 先finish base builder
  base_builder_->Finish();
  if (blob_builder_) {
      // blob builder的finish
    blob_builder_->Finish();
    if (ok()) {
        // 构建BlobFileMeta
      std::shared_ptr<BlobFileMeta> file = std::make_shared<BlobFileMeta>(
          blob_handle_->GetNumber(), blob_handle_->GetFile()->GetFileSize());
      file->FileStateTransit(BlobFileMeta::FileEvent::kFlushOrCompactionOutput);
      // blob manager finish file
      status_ =
          blob_manager_->FinishFile(cf_id_, file, std::move(blob_handle_));
    } else {
      status_ = blob_manager_->DeleteFile(std::move(blob_handle_));
    }
  }
  return status();
}

void TitanTableBuilder::Abandon() {
  base_builder_->Abandon();
  if (blob_builder_) {
    blob_builder_->Abandon();
    status_ = blob_manager_->DeleteFile(std::move(blob_handle_));
  }
}

uint64_t TitanTableBuilder::NumEntries() const {
  return base_builder_->NumEntries();
}

uint64_t TitanTableBuilder::FileSize() const {
  return base_builder_->FileSize();
}

bool TitanTableBuilder::NeedCompact() const {
  return base_builder_->NeedCompact();
}

TableProperties TitanTableBuilder::GetTableProperties() const {
  return base_builder_->GetTableProperties();
}

}  // namespace titandb
}  // namespace rocksdb

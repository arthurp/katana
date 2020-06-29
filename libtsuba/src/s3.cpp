#include "s3.h"

#include <memory>
#include <regex>
#include <string_view>
#include <algorithm>
#include <unistd.h>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/transfer/TransferManager.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/core/utils/HashingUtils.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>

#include "galois/FileSystem.h"
#include "galois/Result.h"
#include "tsuba_internal.h"
#include "SegmentedBufferView.h"

namespace tsuba {

static Aws::SDKOptions sdk_options;

static constexpr const char* kDefaultS3Region = "us-east-1";
static constexpr const char* kAwsTag          = "TsubaS3Client";
static constexpr const char* kTmpTag          = "/tmp/tsuba_s3.";
static const std::regex kS3UriRegex("s3://([-a-z0-9.]+)/(.+)");
static constexpr const uint64_t kS3BufSize    = MB(5);
static constexpr const uint64_t kNumS3Threads = 36;

// Initialized in S3Init
static std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor>
    default_executor;
static std::shared_ptr<Aws::S3::S3Client> async_s3_client{nullptr};

/// GetS3Client returns a configured S3 client.
///
/// The client pulls its configuration from the environment using the same
/// environment variables and configuration paths as the AWS CLI, although with
/// a subset of the configurability.
///
/// The AWS region is determined by:
///
/// 1. The region associated with the default profile in $HOME/.aws/config The
///    location configuration file can be overriden by env[AWS_CONFIG_FILE].
/// 2. Otherwise, env[AWS_DEFAULT_REGION]
/// 3. Otherwise, us-east-1
///
/// The credentials are determined by:
///
/// 1. The credentials associcated with the default profile in
/// $HOME/.aws/credentials
/// 2. Otherwise, the machine account if in EC2
static inline std::shared_ptr<Aws::S3::S3Client>
GetS3Client(const std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor>&
                executor) {
  Aws::Client::ClientConfiguration cfg("default");

  const char* region = std::getenv("AWS_DEFAULT_REGION");
  if (region) {
    cfg.region = region;
  }

  if (cfg.region.empty()) {
    // The AWS SDK says the default region is us-east-1 but it appears we need
    // to set it ourselves.
    cfg.region = kDefaultS3Region;
  }

  cfg.executor = executor;
  return Aws::MakeShared<Aws::S3::S3Client>(kAwsTag, cfg);
}

static inline std::shared_ptr<Aws::S3::S3Client> GetS3Client() {
  return GetS3Client(default_executor);
}

int S3Init() {
  Aws::InitAPI(sdk_options);
  default_executor =
      Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(
          kAwsTag, kNumS3Threads);
  // Need context for async uploads
  async_s3_client = GetS3Client();
  return 0;
}

void S3Fini() { Aws::ShutdownAPI(sdk_options); }

static inline std::string BucketAndObject(const std::string& bucket,
                                          const std::string& object) {
  return bucket + "/" + object;
}

std::pair<std::string, std::string> S3SplitUri(const std::string& uri) {
  std::smatch sub_match;
  /* I wish regex was compatible with string_view but alas it is not */
  if (!std::regex_match(uri, sub_match, kS3UriRegex)) {
    return std::make_pair("", "");
  }
  return std::make_pair(sub_match[1], sub_match[2]);
}

galois::Result<int> S3Open(const std::string& bucket,
                           const std::string& object) {
  auto executor =
      Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(kAwsTag, 1);
  auto s3_client = GetS3Client(executor);

  Aws::Transfer::TransferManagerConfiguration transfer_config(executor.get());
  transfer_config.s3Client = s3_client;
  auto transfer_manager =
      Aws::Transfer::TransferManager::Create(transfer_config);

  auto open_result = galois::OpenUniqueFile(kTmpTag);
  if (!open_result) {
    return open_result.error();
  }
  auto [tmp_name, fd] = open_result.value();

  auto downloadHandle = transfer_manager->DownloadFile(
      ToAwsString(bucket), ToAwsString(object), ToAwsString(tmp_name));
  downloadHandle->WaitUntilFinished();

  assert(downloadHandle->GetBytesTotalSize() ==
         downloadHandle->GetBytesTransferred());
  unlink(tmp_name.c_str());
  return fd;
}

uint64_t S3GetSize(const std::string& bucket, const std::string& object,
                   uint64_t* size) {
  auto s3_client = GetS3Client();
  /* skip all of the thread management overhead if we only have one request */
  Aws::S3::Model::HeadObjectRequest request;
  request.SetBucket(ToAwsString(bucket));
  request.SetKey(ToAwsString(object));
  Aws::S3::Model::HeadObjectOutcome outcome = s3_client->HeadObject(request);
  if (!outcome.IsSuccess()) {
    const auto& error = outcome.GetError();
    std::cout << "ERROR: " << error.GetExceptionName() << ": "
              << error.GetMessage() << std::endl;
    return -1;
  }
  *size = outcome.GetResult().GetContentLength();
  return 0;
}

int S3UploadOverwrite(const std::string& bucket, const std::string& object,
                      const uint8_t* data, uint64_t size) {
  auto s3_client = GetS3Client();

  Aws::S3::Model::CreateMultipartUploadRequest createMpRequest;
  createMpRequest.WithBucket(ToAwsString(bucket));
  createMpRequest.WithContentType("application/octet-stream");
  createMpRequest.WithKey(ToAwsString(object));

  auto createMpResponse = s3_client->CreateMultipartUpload(createMpRequest);
  if (!createMpResponse.IsSuccess()) {
    std::cerr
        << "Transfer Failed to create a multi-part upload request. Bucket: ["
        << bucket << "] with Key: [" << object << "]. "
        << createMpResponse.GetError() << std::endl;
    return -1;
  }

  auto upload_id = createMpResponse.GetResult().GetUploadId();
  SegmentedBufferView bufView(0, (uint8_t*)data, size, kS3BufSize);
  std::vector<SegmentedBufferView::BufPart> parts(bufView.begin(),
                                                  bufView.end());
  if (parts.empty()) {
    std::cerr << "Parts empty, returning" << std::endl;
    return 0;
  }
  std::vector<std::string> part_e_tags(parts.size());

  std::mutex m;
  std::condition_variable cv;
  Aws::S3::Model::CompletedMultipartUpload completedUpload;
  uint64_t finished = 0;
  for (unsigned i = 0; i < parts.size(); ++i) {
    auto& part         = parts[i];
    auto lengthToWrite = part.end - part.start;
    auto streamBuf     = Aws::New<Aws::Utils::Stream::PreallocatedStreamBuf>(
        kAwsTag, part.dest, static_cast<size_t>(lengthToWrite));
    auto preallocatedStreamReader =
        Aws::MakeShared<Aws::IOStream>(kAwsTag, streamBuf);
    Aws::S3::Model::UploadPartRequest uploadPartRequest;

    uploadPartRequest.WithBucket(ToAwsString(bucket))
        .WithContentLength(static_cast<long long>(lengthToWrite))
        .WithKey(ToAwsString(object))
        .WithPartNumber(i + 1) /* part numbers start at 1 */
        .WithUploadId(upload_id);

    uploadPartRequest.SetBody(preallocatedStreamReader);
    uploadPartRequest.SetContentType("application/octet-stream");
    auto callback =
        [i, &part_e_tags, &cv, &m, &finished](
            const Aws::S3::S3Client* client,
            const Aws::S3::Model::UploadPartRequest& request,
            const Aws::S3::Model::UploadPartOutcome& outcome,
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&
                context) {
          /* we're not using these but they need to be here to preserve the
           * signature
           */
          (void)(client);
          (void)(request);
          (void)(context);
          if (outcome.IsSuccess()) {
            std::lock_guard<std::mutex> lk(m);
            part_e_tags[i] = outcome.GetResult().GetETag();
            finished++;
            cv.notify_one();
          } else {
            const auto& error = outcome.GetError();
            std::cerr << "UPLOAD ERROR: " << error.GetExceptionName() << ": "
                      << error.GetMessage() << std::endl;
            abort();
          }
        };
    s3_client->UploadPartAsync(uploadPartRequest, callback);
  }
  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [&] { return finished >= parts.size(); });

  for (unsigned i = 0; i < part_e_tags.size(); ++i) {
    Aws::S3::Model::CompletedPart completedPart;
    completedPart.WithPartNumber(i + 1).WithETag(ToAwsString(part_e_tags[i]));
    completedUpload.AddParts(completedPart);
  }

  Aws::S3::Model::CompleteMultipartUploadRequest completeMultipartUploadRequest;
  completeMultipartUploadRequest.WithBucket(ToAwsString(bucket))
      .WithKey(ToAwsString(object))
      .WithUploadId(upload_id)
      .WithMultipartUpload(completedUpload);

  auto completeUploadOutcome =
      s3_client->CompleteMultipartUpload(completeMultipartUploadRequest);

  if (!completeUploadOutcome.IsSuccess()) {
    std::cerr << "Failed to complete multipart upload" << std::endl;
    const auto& error = completeUploadOutcome.GetError();
    std::cerr << "UPLOAD ERROR: " << error.GetExceptionName() << ": "
              << error.GetMessage() << std::endl;
    return -1;
  }
  return 0;
}

int S3UploadOverwriteSync(const std::string& bucket, const std::string& object,
                          const uint8_t* data, uint64_t size) {
  auto s3_client = GetS3Client();
  // std::cerr << "S3UploadOverwriteSync: " << bucket << "/" << object
  // <<std::endl;
  // Set up request
  Aws::S3::Model::PutObjectRequest object_request;

  object_request.SetBucket(ToAwsString(bucket));
  object_request.SetKey(ToAwsString(object));
  auto streamBuf = Aws::New<Aws::Utils::Stream::PreallocatedStreamBuf>(
      kAwsTag, (uint8_t*)data, static_cast<size_t>(size));
  auto preallocatedStreamReader =
      Aws::MakeShared<Aws::IOStream>(kAwsTag, streamBuf);

  object_request.SetBody(preallocatedStreamReader);
  object_request.SetContentType("application/octet-stream");

  auto outcome = s3_client->PutObject(object_request);
  if (!outcome.IsSuccess()) {
    /* TODO there are likely some errors we can handle gracefully
     * i.e., with retries */
    const auto& error = outcome.GetError();
    std::cerr << "ERROR: " << error.GetExceptionName() << ": "
              << error.GetMessage() << std::endl;
    abort();
  }
  return 0;
}

static std::unordered_map<std::string, bool> bnodone;
static std::mutex bnomutex;
static std::condition_variable bnocv;

int S3UploadOverwriteAsync(const std::string& bucket, const std::string& object,
                           const uint8_t* data, uint64_t size) {
  // std::cerr << "S3UploadOverwriteAsync: " << bucket << "/" << object
  // <<std::endl;
  // Set up request
  Aws::S3::Model::PutObjectRequest object_request;

  object_request.SetBucket(ToAwsString(bucket));
  object_request.SetKey(ToAwsString(object));
  auto streamBuf = Aws::New<Aws::Utils::Stream::PreallocatedStreamBuf>(
      kAwsTag, (uint8_t*)data, static_cast<size_t>(size));
  auto preallocatedStreamReader =
      Aws::MakeShared<Aws::IOStream>(kAwsTag, streamBuf);

  object_request.SetBody(preallocatedStreamReader);
  object_request.SetContentType("application/octet-stream");

  std::string bno = BucketAndObject(bucket, object);
  {
    std::lock_guard<std::mutex> lck(bnomutex);
    bnodone[bno] = false;
  }

  // Copy bno because it is going out of scope
  auto callback = [&,
                   bno](const Aws::S3::S3Client* /*client*/,
                        const Aws::S3::Model::PutObjectRequest& /*request*/,
                        const Aws::S3::Model::PutObjectOutcome& outcome,
                        const std::shared_ptr<
                            const Aws::Client::AsyncCallerContext>& /*ctx*/) {
    if (outcome.IsSuccess()) {
      std::lock_guard<std::mutex> lck(bnomutex);
      bnodone[bno] = true;
      // Notify does not require lock
      bnocv.notify_one();
    } else {
      /* TODO there are likely some errors we can handle gracefully
       * i.e., with retries */
      const auto& error = outcome.GetError();
      std::cerr << "ERROR: " << error.GetExceptionName() << ": "
                << error.GetMessage() << std::endl;
      abort();
    }
  };
  async_s3_client->PutObjectAsync(object_request, callback);

  return 0;
}

int S3UploadOverwriteAsyncFinish(const std::string& bucket,
                                 const std::string& object) {
  std::string bno = BucketAndObject(bucket, object);
  // std::cerr << "S3UploadOverwriteAsyncFinish bno: " << bno << std::endl;
  if (bnodone.find(bno) == bnodone.end()) {
    std::cerr << "XXX S3UploadOverwriteAsyncFinish bno not present: " << bno
              << std::endl;
    return -1;
  }
  std::unique_lock<std::mutex> lck(bnomutex);
  bnocv.wait(lck, [&] { return bnodone[bno]; });
  return 0;
}

static void
PrepareObjectRequest(Aws::S3::Model::GetObjectRequest* object_request,
                     const std::string& bucket, const std::string& object,
                     SegmentedBufferView::BufPart part) {
  object_request->SetBucket(ToAwsString(bucket));
  object_request->SetKey(ToAwsString(object));
  std::ostringstream range;
  /* Knock one byte off the end because range in AWS S3 API is inclusive */
  range << "bytes=" << part.start << "-" << part.end - 1;
  object_request->SetRange(ToAwsString(range.str()));

  object_request->SetResponseStreamFactory([part]() {
    auto* bufferStream = Aws::New<Aws::Utils::Stream::DefaultUnderlyingStream>(
        kAwsTag, Aws::MakeUnique<Aws::Utils::Stream::PreallocatedStreamBuf>(
                     kAwsTag, part.dest, part.end - part.start + 1));
    if (bufferStream == nullptr) {
      abort();
    }
    return bufferStream;
  });
}

int S3DownloadRange(const std::string& bucket, const std::string& object,
                    uint64_t start, uint64_t size, uint8_t* result_buf) {
  auto s3_client = GetS3Client();
  SegmentedBufferView bufView(start, result_buf, size, kS3BufSize);
  std::vector<SegmentedBufferView::BufPart> parts(bufView.begin(),
                                                  bufView.end());
  if (parts.empty()) {
    return 0;
  }

  if (parts.size() == 1) {
    /* skip all of the thread management overhead if we only have one request */
    Aws::S3::Model::GetObjectRequest request;
    PrepareObjectRequest(&request, bucket, object, parts[0]);
    Aws::S3::Model::GetObjectOutcome outcome = s3_client->GetObject(request);
    if (outcome.IsSuccess()) {
      /* result_buf should have the data here */
    } else {
      /* TODO there are likely some errors we can handle gracefully
       * i.e., with retries */
      const auto& error = outcome.GetError();
      std::cerr << "ERROR: " << error.GetExceptionName() << ": "
                << error.GetMessage() << "\n";
      abort();
    }
    return 0;
  }

  std::mutex m;
  std::condition_variable cv;
  uint64_t finished = 0;
  auto callback =
      [&](const Aws::S3::S3Client* /*clnt*/,
          const Aws::S3::Model::GetObjectRequest& /*req*/,
          const Aws::S3::Model::GetObjectOutcome& get_object_outcome,
          const std::shared_ptr<
              const Aws::Client::AsyncCallerContext>& /*ctx*/) {
        if (get_object_outcome.IsSuccess()) {
          /* result_buf should have our data here */
          std::unique_lock<std::mutex> lk(m);
          finished++;
          cv.notify_one();
        } else {
          /* TODO there are likely some errors we can handle gracefully
           * i.e., with retries */
          const auto& error = get_object_outcome.GetError();
          std::cout << "ERROR: " << error.GetExceptionName() << ": "
                    << error.GetMessage() << std::endl;
          abort();
        }
      };
  for (auto& part : parts) {
    Aws::S3::Model::GetObjectRequest object_request;
    PrepareObjectRequest(&object_request, bucket, object, part);
    s3_client->GetObjectAsync(object_request, callback);
  }

  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [&] { return finished >= parts.size(); });

  return 0;
}

} /* namespace tsuba */
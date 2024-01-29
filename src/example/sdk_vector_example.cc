// Copyright (c) 2023 dingodb.com, Inc. All Rights Reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <memory>
#include <string>

#include "common/logging.h"
#include "common/synchronization.h"
#include "glog/logging.h"
#include "sdk/client.h"
#include "sdk/status.h"
#include "sdk/vector.h"
#include "sdk/vector/vector_common.h"
#include "sdk/vector/vector_index.h"
#include "sdk/vector/vector_index_cache.h"

using dingodb::sdk::Status;

DEFINE_string(coordinator_url, "", "coordinator url");
static std::shared_ptr<dingodb::sdk::Client> g_client;
static int64_t g_schema_id{2};
static int64_t g_index_id{0};
static std::string g_index_name = "example01";
static std::vector<int64_t> g_range_partition_seperator_ids{5, 10, 20};
static int32_t g_dimension = 2;
static dingodb::sdk::FlatParam g_flat_param(g_dimension, dingodb::sdk::MetricType::kL2);

static void PrepareVectorIndex() {
  dingodb::sdk::VectorIndexCreator* creator;
  Status built = g_client->NewVectorIndexCreator(&creator);
  CHECK(built.IsOK()) << "dingo creator build fail:" << built.ToString();
  CHECK_NOTNULL(creator);
  dingodb::ScopeGuard guard([&]() { delete creator; });

  Status create = creator->SetSchemaId(g_schema_id)
                      .SetName(g_index_name)
                      .SetReplicaNum(3)
                      .SetRangePartitions(g_range_partition_seperator_ids)
                      .SetFlatParam(g_flat_param)
                      .Create(g_index_id);
  DINGO_LOG(INFO) << "Create index status: " << create.ToString() << ", index_id:" << g_index_id;
  sleep(10);
}

void PostClean() {
  Status tmp = g_client->DropIndex(g_index_id);
  DINGO_LOG(INFO) << "drop index status: " << tmp.ToString() << ", index_id:" << g_index_id;
}

// TODO: remove
static void VectorIndexCacheSearch() {
  auto coordinator_proxy = std::make_shared<dingodb::sdk::CoordinatorProxy>();
  Status open = coordinator_proxy->Open(FLAGS_coordinator_url);
  CHECK(open.ok()) << "Fail to open coordinator_proxy, please check parameter --url=" << FLAGS_coordinator_url;

  dingodb::sdk::VectorIndexCache cache(*coordinator_proxy);
  {
    std::shared_ptr<dingodb::sdk::VectorIndex> index;
    Status got = cache.GetVectorIndexById(g_index_id, index);
    CHECK(got.ok()) << "Fail to get vector index, index_id:" << g_index_id << ", status:" << got.ToString();
    CHECK(index.get() != nullptr);
    CHECK_EQ(index->GetId(), g_index_id);
    CHECK_EQ(index->GetName(), g_index_name);
  }

  {
    std::shared_ptr<dingodb::sdk::VectorIndex> index;
    Status got = cache.GetVectorIndexByKey(dingodb::sdk::GetVectorIndexCacheKey(g_schema_id, g_index_name), index);
    CHECK(got.ok()) << "Fail to get vector index, index_name:" << g_index_name << ", status:" << got.ToString();
    CHECK(index.get() != nullptr);
    CHECK_EQ(index->GetId(), g_index_id);
    CHECK_EQ(index->GetName(), g_index_name);
  }

  {
    int64_t index_id{0};
    Status got = cache.GetIndexIdByKey(dingodb::sdk::GetVectorIndexCacheKey(g_schema_id, g_index_name), index_id);
    CHECK(got.ok()) << "Fail to get index_id, index_name" << g_index_name << ", status:" << got.ToString();
    CHECK_EQ(index_id, g_index_id);
  }

  {
    cache.RemoveVectorIndexById(g_index_id);
    {
      std::shared_ptr<dingodb::sdk::VectorIndex> index;
      Status got = cache.GetVectorIndexByKey(dingodb::sdk::GetVectorIndexCacheKey(g_schema_id, g_index_name), index);
      CHECK(got.ok()) << "Fail to get vector index, index_name:" << g_index_name << ", status:" << got.ToString();
      CHECK(index.get() != nullptr);
      CHECK_EQ(index->GetId(), g_index_id);
      CHECK_EQ(index->GetName(), g_index_name);
    }
  }
}

static void VectorAdd() {
  dingodb::sdk::VectorClient* client;
  Status built = g_client->NewVectorClient(&client);
  CHECK(built.IsOK()) << "dingo vector client build fail:" << built.ToString();
  CHECK_NOTNULL(client);
  dingodb::ScopeGuard guard([&]() { delete client; });

  std::vector<dingodb::sdk::VectorWithId> vectors;

  for (const auto& id : g_range_partition_seperator_ids) {
    dingodb::sdk::Vector tmp_vector{dingodb::sdk::ValueType::kFloat, g_dimension};
    tmp_vector.float_values.push_back(1.0);
    tmp_vector.float_values.push_back(2.0);
    dingodb::sdk::VectorWithId tmp(id, std::move(tmp_vector));

    vectors.push_back(std::move(tmp));
  }

  Status add = client->Add(g_index_id, vectors, false, false);
  DINGO_LOG(INFO) << "vector add:" << add.ToString();
}

static void VectorSearch() {
  dingodb::sdk::VectorClient* client;
  Status built = g_client->NewVectorClient(&client);
  CHECK(built.IsOK()) << "dingo vector client build fail:" << built.ToString();
  CHECK_NOTNULL(client);
  dingodb::ScopeGuard guard([&]() { delete client; });

  std::vector<dingodb::sdk::VectorWithId> target_vectors;
  {
    dingodb::sdk::Vector tmp_vector{dingodb::sdk::ValueType::kFloat, g_dimension};
    tmp_vector.float_values.push_back(1.5);
    tmp_vector.float_values.push_back(1.5);

    dingodb::sdk::VectorWithId tmp;
    tmp.vector = std::move(tmp_vector);
    target_vectors.push_back(std::move(tmp));
  }

  dingodb::sdk::SearchParameter param;
  param.topk = 10;
  // param.use_brute_force = true;
  param.extra_params.insert(std::make_pair(dingodb::sdk::kParallelOnQueries, 10));

  std::vector<dingodb::sdk::SearchResult> result;
  Status tmp = client->Search(g_index_id, param, target_vectors, result);

  DINGO_LOG(INFO) << "vector search status: " << tmp.ToString();
  for (const auto& r : result) {
    DINGO_LOG(INFO) << "vectr search result:" << dingodb::sdk::DumpToString(r);
  }
}

int main(int argc, char* argv[]) {
  FLAGS_minloglevel = google::GLOG_INFO;
  FLAGS_logtostdout = true;
  FLAGS_colorlogtostdout = true;
  FLAGS_logbufsecs = 0;
  FLAGS_v = dingodb::kGlobalValueOfDebug;

  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_coordinator_url.empty()) {
    DINGO_LOG(ERROR) << "coordinator url is empty, try to use file://./coor_list";
    FLAGS_coordinator_url = "file://./coor_list";
  }

  std::shared_ptr<dingodb::sdk::Client> client;
  Status built = dingodb::sdk::Client::Build(FLAGS_coordinator_url, client);
  if (!built.ok()) {
    DINGO_LOG(ERROR) << "Fail to build client, please check parameter --url=" << FLAGS_coordinator_url;
    return -1;
  }
  CHECK_NOTNULL(client.get());
  g_client = std::move(client);

  PrepareVectorIndex();
  VectorIndexCacheSearch();
  VectorAdd();
  VectorSearch();
  PostClean();
}

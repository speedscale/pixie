#include <memory>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/carnot/funcs/metadata/metadata_ops.h"
#include "src/carnot/udf/test_utils.h"
#include "src/common/base/base.h"
#include "src/common/testing/testing.h"
#include "src/shared/k8s/metadatapb/test_proto.h"
#include "src/shared/metadata/base_types.h"
#include "src/shared/metadata/pids.h"
#include "src/shared/metadata/state_manager.h"
#include "src/shared/metadata/test_utils.h"

namespace pl {
namespace carnot {
namespace funcs {
namespace metadata {

using ::pl::carnot::udf::FunctionContext;

using ResourceUpdate = pl::shared::k8s::metadatapb::ResourceUpdate;
using ::testing::AnyOf;

class MetadataOpsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    agent_id_ = sole::uuid4();
    metadata_state_ = std::make_shared<pl::md::AgentMetadataState>(/* hostname */ "myhost",
                                                                   /* asid */ 1, agent_id_);
    // Apply updates to metadata state.
    updates_ =
        std::make_unique<moodycamel::BlockingConcurrentQueue<std::unique_ptr<ResourceUpdate>>>();

    updates_->enqueue(pl::metadatapb::testutils::CreateRunningContainerUpdatePB());
    updates_->enqueue(pl::metadatapb::testutils::CreateRunningPodUpdatePB());
    updates_->enqueue(pl::metadatapb::testutils::CreateRunningServiceUpdatePB());
    updates_->enqueue(pl::metadatapb::testutils::CreateTerminatingContainerUpdatePB());
    updates_->enqueue(pl::metadatapb::testutils::CreateTerminatingPodUpdatePB());
    updates_->enqueue(pl::metadatapb::testutils::CreateTerminatingServiceUpdatePB());

    auto s = pl::md::AgentMetadataStateManager::ApplyK8sUpdates(10, metadata_state_.get(),
                                                                &md_filter_, updates_.get());

    // Apply PID updates to metadata state.
    auto upid1 = md::UPID(123, 567, 89101);
    auto pid1 = std::make_unique<md::PIDInfo>(upid1, "test", "pod1_container_1");
    metadata_state_->AddUPID(upid1, std::move(pid1));
    auto upid2 = md::UPID(123, 567, 468);
    auto pid2 = std::make_unique<md::PIDInfo>(upid2, "cmdline", "pod2_container_1");
    metadata_state_->AddUPID(upid2, std::move(pid2));

    ASSERT_OK(s);
  }
  sole::uuid agent_id_;
  std::shared_ptr<pl::md::AgentMetadataState> metadata_state_;
  std::unique_ptr<moodycamel::BlockingConcurrentQueue<std::unique_ptr<ResourceUpdate>>> updates_;
  md::TestAgentMetadataFilter md_filter_;
};

TEST_F(MetadataOpsTest, asid_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<ASIDUDF>(std::move(function_ctx));
  udf_tester.ForInput().Expect(1);
}

TEST_F(MetadataOpsTest, upid_to_asid_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto upid = types::UInt128Value(528280977975, 89101);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToASIDUDF>(std::move(function_ctx));
  udf_tester.ForInput(upid).Expect(123);
}

TEST_F(MetadataOpsTest, pod_id_to_pod_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<PodIDToPodNameUDF>(std::move(function_ctx));
  udf_tester.ForInput("1_uid").Expect("pl/running_pod");
  udf_tester.ForInput("2_uid").Expect("pl/terminating_pod");
}

TEST_F(MetadataOpsTest, pod_name_to_pod_id_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<PodNameToPodIDUDF>(std::move(function_ctx));
  udf_tester.ForInput("pl/running_pod").Expect("1_uid");
  udf_tester.ForInput("pl/terminating_pod").Expect("2_uid");
}

TEST_F(MetadataOpsTest, upid_to_pod_id_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToPodIDUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("1_uid");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("2_uid");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_pod_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToPodNameUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("pl/running_pod");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("pl/terminating_pod");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_namespace_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToNamespaceUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("pl");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("pl");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_container_id_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToContainerIDUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("pod1_container_1");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("pod2_container_1");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_container_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToContainerNameUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("running_container");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("terminating_container");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_service_id_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToServiceIDUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("3_uid");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("4_uid");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");

  // Terminate a service, and make sure that the upid no longer associates with that service.
  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedServiceUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  // upid2 previously was connected to 4_uid.
  udf_tester.ForInput(upid2).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_service_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToServiceNameUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("pl/running_service");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("pl/terminating_service");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");

  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedServiceUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  // upid2 previously was connected to pl/terminating_service.
  udf_tester.ForInput(upid2).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_node_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToNodeNameUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("test_node");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("test_node_tbt");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");

  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedPodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  // upid2 previously was connected to pl/terminating_pod.
  udf_tester.ForInput(upid2).Expect("");
}

TEST_F(MetadataOpsTest, pod_id_to_node_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<PodIDToNodeNameUDF>(std::move(function_ctx));
  udf_tester.ForInput("1_uid").Expect("test_node");
  // This pod is not available, should return empty.
  udf_tester.ForInput("123_uid").Expect("");
}

TEST_F(MetadataOpsTest, upid_to_hostname_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToHostnameUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("test_host");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("test_host_tbt");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");

  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedPodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  // upid2 previously was connected to pl/terminating_pod.
  udf_tester.ForInput(upid2).Expect("");
}

TEST_F(MetadataOpsTest, service_id_to_service_name_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<ServiceIDToServiceNameUDF>(std::move(function_ctx));
  udf_tester.ForInput("3_uid").Expect("pl/running_service");
  udf_tester.ForInput("4_uid").Expect("pl/terminating_service");
}

TEST_F(MetadataOpsTest, service_name_to_service_id_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<ServiceNameToServiceIDUDF>(std::move(function_ctx));
  udf_tester.ForInput("pl/running_service").Expect("3_uid");
  // Terminating service has not yet terminated.
  udf_tester.ForInput("pl/terminating_service").Expect("4_uid");
}

TEST_F(MetadataOpsTest, upid_to_service_id_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // auto udf_tester = pl::carnot::udf::UDFTester<UPIDToServiceIDUDF>(std::move(function_ctx));
  UPIDToServiceIDUDF udf;
  auto upid1 = types::UInt128Value(528280977975, 89101);
  EXPECT_THAT(udf.Exec(function_ctx.get(), upid1),
              AnyOf("[\"3_uid\",\"5_uid\"]", "[\"5_uid\",\"3_uid\"]"));
}

TEST_F(MetadataOpsTest, upid_to_service_name_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  UPIDToServiceNameUDF udf;
  auto upid1 = types::UInt128Value(528280977975, 89101);
  EXPECT_THAT(udf.Exec(function_ctx.get(), upid1),
              AnyOf("[\"pl/running_service\",\"pl/other_service_with_pod\"]",
                    "[\"pl/other_service_with_pod\",\"pl/running_service\"]"));
}

TEST_F(MetadataOpsTest, pod_id_to_service_name_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  PodIDToServiceNameUDF udf;
  EXPECT_THAT(udf.Exec(function_ctx.get(), "1_uid"),
              AnyOf("[\"pl/running_service\",\"pl/other_service_with_pod\"]",
                    "[\"pl/other_service_with_pod\",\"pl/running_service\"]"));
}

TEST_F(MetadataOpsTest, pod_id_to_service_id_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  PodIDToServiceIDUDF udf;
  EXPECT_THAT(udf.Exec(function_ctx.get(), "1_uid"),
              AnyOf("[\"3_uid\",\"5_uid\"]", "[\"5_uid\",\"3_uid\"]"));
}

TEST_F(MetadataOpsTest, pod_name_to_service_name_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  PodNameToServiceNameUDF udf;
  EXPECT_THAT(udf.Exec(function_ctx.get(), "pl/running_pod"),
              AnyOf("[\"pl/running_service\",\"pl/other_service_with_pod\"]",
                    "[\"pl/other_service_with_pod\",\"pl/running_service\"]"));
}

TEST_F(MetadataOpsTest, pod_name_to_service_id_test_multiple_services) {
  updates_->enqueue(pl::metadatapb::testutils::CreateServiceWithSamePodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  PodNameToServiceIDUDF udf;
  EXPECT_THAT(udf.Exec(function_ctx.get(), "pl/running_pod"),
              AnyOf("[\"3_uid\",\"5_uid\"]", "[\"5_uid\",\"3_uid\"]"));
}

TEST_F(MetadataOpsTest, upid_to_string) {
  UPIDToStringUDF udf;
  auto upid1 = md::UPID(123, 567, 89101);
  EXPECT_EQ(udf.Exec(nullptr, upid1.value()), absl::Substitute("$0:$1:$2", 123, 567, 89101));

  auto upid2 = md::UPID(255, 123, 11111);
  EXPECT_EQ(udf.Exec(nullptr, upid2.value()), absl::Substitute("$0:$1:$2", 255, 123, 11111));
}

TEST_F(MetadataOpsTest, upid_to_pid) {
  UPIDToPIDUDF udf;
  auto upid1 = md::UPID(123, 567, 89101);
  EXPECT_EQ(udf.Exec(nullptr, upid1.value()), 567);

  auto upid2 = md::UPID(255, 123, 11111);
  EXPECT_EQ(udf.Exec(nullptr, upid2.value()), 123);
}

TEST_F(MetadataOpsTest, pod_id_to_start_time) {
  PodIDToPodStartTimeUDF udf;
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // 1_uid is the Pod id for the currently running pod.
  EXPECT_EQ(udf.Exec(function_ctx.get(), "1_uid").val, 5);
  // 1234567_uid is a nonexistant Pod id, should return 0.
  EXPECT_EQ(udf.Exec(function_ctx.get(), "1234567_uid").val, 0);
}

TEST_F(MetadataOpsTest, pod_name_to_start_time) {
  PodNameToPodStartTimeUDF udf;
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // 1_uid is the Pod id for the currently running pod.
  EXPECT_EQ(udf.Exec(function_ctx.get(), "pl/running_pod").val, 5);
  // 1234567_uid is a nonexistant Pod id, should return 0.
  EXPECT_EQ(udf.Exec(function_ctx.get(), "pl/blah").val, 0);
}

TEST_F(MetadataOpsTest, pod_name_to_pod_status) {
  PodNameToPodStatusUDF status_udf;
  PodNameToPodStatusMessageUDF msg_udf;
  PodNameToPodStatusReasonUDF reason_udf;

  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedPodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // 1_uid is the Pod id for the currently running pod.
  EXPECT_EQ(status_udf.Exec(function_ctx.get(), "pl/running_pod"), "Running");
  EXPECT_EQ(status_udf.Exec(function_ctx.get(), "pl/terminating_pod"), "Failed");

  EXPECT_EQ(msg_udf.Exec(function_ctx.get(), "pl/running_pod"), "Running message");
  EXPECT_EQ(msg_udf.Exec(function_ctx.get(), "pl/terminating_pod"), "Failed message terminated");

  EXPECT_EQ(reason_udf.Exec(function_ctx.get(), "pl/running_pod"), "Running reason");
  EXPECT_EQ(reason_udf.Exec(function_ctx.get(), "pl/terminating_pod"), "Failed reason terminated");
}

TEST_F(MetadataOpsTest, container_id_to_container_status) {
  ContainerIDToContainerStatusUDF status_udf;
  ContainerIDToContainerStatusMessageUDF msg_udf;
  ContainerIDToContainerStatusReasonUDF reason_udf;

  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // 1_uid is the Pod id for the currently running pod.
  EXPECT_EQ(status_udf.Exec(function_ctx.get(), "pod1_container_1"), "Running");
  EXPECT_EQ(status_udf.Exec(function_ctx.get(), "pod2_container_1"), "Terminated");

  EXPECT_EQ(msg_udf.Exec(function_ctx.get(), "pod1_container_1"), "Running message");
  EXPECT_EQ(msg_udf.Exec(function_ctx.get(), "pod2_container_1"), "Terminating message pending");

  EXPECT_EQ(reason_udf.Exec(function_ctx.get(), "pod1_container_1"), "Running reason");
  EXPECT_EQ(reason_udf.Exec(function_ctx.get(), "pod2_container_1"), "Terminating reason pending");
}

TEST_F(MetadataOpsTest, upid_to_cmdline) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);

  UPIDToCmdLineUDF udf;
  auto upid1 = md::UPID(123, 567, 89101);
  EXPECT_EQ(udf.Exec(function_ctx.get(), upid1.value()), "test");
  auto upid2 = md::UPID(123, 567, 468);
  EXPECT_EQ(udf.Exec(function_ctx.get(), upid2.value()), "cmdline");
}

TEST_F(MetadataOpsTest, hostname) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);

  HostnameUDF udf;
  EXPECT_EQ(udf.Exec(function_ctx.get()), "myhost");
}

TEST_F(MetadataOpsTest, pod_ip) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);

  PodIPToPodIDUDF udf;
  EXPECT_EQ(udf.Exec(function_ctx.get(), "1.1.1.1"), "1_uid");
}

TEST_F(MetadataOpsTest, upid_to_qos) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<UPIDToPodQoSUDF>(std::move(function_ctx));
  auto upid1 = types::UInt128Value(528280977975, 89101);
  udf_tester.ForInput(upid1).Expect("kGuaranteed");
  auto upid2 = types::UInt128Value(528280977975, 468);
  udf_tester.ForInput(upid2).Expect("kBestEffort");
  auto upid3 = types::UInt128Value(528280977975, 123);
  udf_tester.ForInput(upid3).Expect("");
}

TEST_F(MetadataOpsTest, upid_to_pod_status) {
  UPIDToPodStatusUDF udf;
  updates_->enqueue(pl::metadatapb::testutils::CreateTerminatedPodUpdatePB());
  EXPECT_OK(pl::md::AgentMetadataStateManager::ApplyK8sUpdates(11, metadata_state_.get(),
                                                               &md_filter_, updates_.get()));
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  // 1_uid is the Pod id for the currently running pod.
  auto upid1 = types::UInt128Value(528280977975, 89101);
  EXPECT_EQ(udf.Exec(function_ctx.get(), upid1), "Running");
  auto upid2 = types::UInt128Value(528280977975, 468);
  EXPECT_EQ(std::string(udf.Exec(function_ctx.get(), upid2)), "Failed");
}

TEST_F(MetadataOpsTest, pod_id_to_namespace_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<PodIDToNamespaceUDF>(std::move(function_ctx));
  udf_tester.ForInput("1_uid").Expect("pl");
  udf_tester.ForInput("2_uid").Expect("pl");
}

TEST_F(MetadataOpsTest, pod_name_to_namespace_test) {
  auto function_ctx = std::make_unique<FunctionContext>(metadata_state_);
  auto udf_tester = pl::carnot::udf::UDFTester<PodNameToNamespaceUDF>(std::move(function_ctx));
  udf_tester.ForInput("pl/running_pod").Expect("pl");
  udf_tester.ForInput("px-sock-shop/terminating_pod").Expect("px-sock-shop");
}

}  // namespace metadata
}  // namespace funcs
}  // namespace carnot
}  // namespace pl

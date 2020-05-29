#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/common/base/base.h"
#include "src/common/system/system.h"
#include "src/shared/metadata/metadata_state.h"
#include "src/shared/types/types.h"
#include "src/stirling/data_table.h"
#include "src/stirling/info_class_manager.h"
#include "src/stirling/utils/proc_tracker.h"

/**
 * These are the steps to follow to add a new data source connector.
 * 1. If required, create a new SourceConnector class.
 * 2. Add a new Create function with the following signature:
 *    static std::unique_ptr<SourceConnector> Create().
 *    In this function create an InfoClassSchema (vector of DataElement)
 * 3. Register the data source in the appropriate registry.
 */

namespace pl {
namespace stirling {

class InfoClassManager;

#define DUMMY_SOURCE_CONNECTOR(NAME)                                        \
  class NAME : public SourceConnector {                                     \
   public:                                                                  \
    static constexpr bool kAvailable = false;                               \
    static constexpr std::array<DataTableSchema, 0> kTables = {};           \
    static std::unique_ptr<SourceConnector> Create(std::string_view name) { \
      PL_UNUSED(name);                                                      \
      return nullptr;                                                       \
    }                                                                       \
  }

/**
 * ConnectorContext is the information passed on every Transfer call to source connectors.
 */
class ConnectorContext {
 public:
  ConnectorContext() = default;
  ~ConnectorContext() = default;

  /**
   * ConntectorContext with metadata state.
   * @param agent_metadata_state A read-only snapshot view of the metadata state. This state
   * should not be held onto for extended periods of time.
   */
  explicit ConnectorContext(std::shared_ptr<const md::AgentMetadataState> agent_metadata_state)
      : agent_metadata_state_(std::move(agent_metadata_state)) {}

  uint32_t GetASID() const {
    if (agent_metadata_state_ == nullptr) {
      return 0;
    }
    return agent_metadata_state_->asid();
  }

  absl::flat_hash_set<md::UPID> GetUPIDs() const {
    if (agent_metadata_state_ == nullptr) {
      return ListUPIDs(system::Config::GetInstance().proc_path(), 0);
    }
    return agent_metadata_state_->upids();
  }

  const absl::flat_hash_map<md::UPID, md::PIDInfoUPtr>& GetPIDInfoMap() const {
    if (agent_metadata_state_ == nullptr) {
      static const absl::flat_hash_map<md::UPID, md::PIDInfoUPtr> kEmpty;
      return kEmpty;
    }
    return agent_metadata_state_->pids_by_upid();
  }

  // TODO(oazizi): Consider breaking up into GetPods() and GetContainers().
  const md::K8sMetadataState& GetK8SMetadata() {
    if (agent_metadata_state_ == nullptr) {
      static const md::K8sMetadataState kEmpty;
      return kEmpty;
    }
    return agent_metadata_state_->k8s_metadata_state();
  }

  std::vector<CIDRBlock> GetClusterCIDRs() {
    if (agent_metadata_state_ == nullptr) {
      return {};
    }

    std::vector<CIDRBlock> cluster_cidrs;

    // Copy Pod CIDRs.
    const std::vector<CIDRBlock>& pod_cidrs =
        agent_metadata_state_->k8s_metadata_state().pod_cidrs();
    for (const auto& pod_cidr : pod_cidrs) {
      cluster_cidrs.push_back(pod_cidr);
    }

    // Copy Service CIDRs.
    const std::optional<CIDRBlock>& service_cidr =
        agent_metadata_state_->k8s_metadata_state().service_cidr();
    if (service_cidr.has_value()) {
      cluster_cidrs.push_back(service_cidr.value());
    }

    return cluster_cidrs;
  }

 private:
  std::shared_ptr<const md::AgentMetadataState> agent_metadata_state_;
};

class SourceConnector : public NotCopyable {
 public:
  /**
   * @brief Defines whether the SourceConnector has an implementation.
   *
   * Default in the base class is true, and normally should not be changed in the derived class.
   *
   * However, a derived class may want to redefine to false in certain special circumstances:
   * 1) a SourceConnector that is just a placeholder (not yet implemented).
   * 2) a SourceConnector that is not compilable (e.g. on Mac). See DUMMY_SOURCE_CONNECTOR macro.
   */
  static constexpr bool kAvailable = true;

  SourceConnector() = delete;
  virtual ~SourceConnector() = default;

  /**
   * @brief Initializes the source connector. Can only be called once.
   * @return Status of whether initialization was successful.
   */
  Status Init();

  /**
   * Sets the initial context for the source connector.
   * Used for context specific init steps (e.g. deploying uprobes on PIDs).
   */
  void InitContext(ConnectorContext* ctx);

  /**
   * @brief Transfers any collected data, for the specified table, into the provided record_batch.
   * @param table_num The table number (id) of the data. See DataTableSchemas in individual
   * connectors.
   * @param record_batch The target to move the data into.
   */
  void TransferData(ConnectorContext* ctx, uint32_t table_num, DataTable* data_table);

  /**
   * @brief Stops the source connector and releases any acquired resources.
   * May only be called after a successful Init().
   *
   * @return Status of whether stop was successful.
   */
  Status Stop();

  const std::string& source_name() const { return source_name_; }

  uint32_t num_tables() const { return table_schemas_.size(); }

  const DataTableSchema& TableSchema(uint32_t table_num) const {
    DCHECK_LT(table_num, num_tables())
        << absl::Substitute("Access to table out of bounds: table_num=$0", table_num);
    return table_schemas_[table_num];
  }

  static constexpr uint32_t TableNum(ArrayView<DataTableSchema> tables,
                                     const DataTableSchema& key) {
    uint32_t i = 0;
    for (i = 0; i < tables.size(); i++) {
      if (tables[i].name() == key.name()) {
        break;
      }
    }

    // Check that we found the index. This prevents compilation if name is not found,
    // during constexpr evaluation (which is awesome!).
    COMPILE_TIME_ASSERT(i != tables.size(), "Could not find name");

    return i;
  }

  /**
   * @brief Utility function to convert time as recorded by in monotonic clock to real time.
   * This is especially useful for converting times from BPF, which are all in monotonic clock.
   */
  uint64_t ClockRealTimeOffset() const { return sysconfig_.ClockRealTimeOffset(); }
  uint64_t AdjustedSteadyClockNowNS() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() +
           ClockRealTimeOffset();
  }

  uint64_t AdjustedSteadyClockNow() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count() +
           ClockRealTimeOffset();
  }

 protected:
  explicit SourceConnector(std::string_view source_name,
                           const ArrayView<DataTableSchema>& table_schemas)
      : source_name_(source_name), table_schemas_(table_schemas) {}

  virtual Status InitImpl() = 0;

  // Provide a default InitContextImpl which does nothing.
  // SourceConnectors only need override if action is required on the initial context.
  virtual void InitContextImpl(ConnectorContext* /* ctx */) {}

  virtual void TransferDataImpl(ConnectorContext* ctx, uint32_t table_num,
                                DataTable* data_table) = 0;

  virtual Status StopImpl() = 0;

 protected:
  /**
   * Track state of connector. A connector's lifetime typically progresses sequentially
   * from kUninitialized -> kActive -> KStopped.
   *
   * kErrors is a special state to track a bad state.
   */
  enum class State { kUninitialized, kActive, kStopped, kErrors };

  // Sub-classes are allowed to inspect state.
  State state() const { return state_; }

  const system::Config& sysconfig_ = system::Config::GetInstance();

 private:
  std::atomic<State> state_ = State::kUninitialized;

  const std::string source_name_;
  const ArrayView<DataTableSchema> table_schemas_;
};

}  // namespace stirling
}  // namespace pl

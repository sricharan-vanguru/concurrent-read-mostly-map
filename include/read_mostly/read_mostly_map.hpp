#pragma once

#include "read_mostly/hazard_reader.hpp"
#include "read_mostly/snapshot_builder.hpp"
#include "read_mostly/telemetry.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace read_mostly {
enum class CommitStatus { committed, version_conflict, closed, memory_budget_exceeded };
struct CommitResult {
    CommitStatus status;
    // New version on success; currently published version on rejection.
    std::uint64_t version;
};

// Compiled façade. Readers retain immutable snapshots; writers serialize the
// entire copy/build/publish sequence. No application reader mutex is used.
class ReadMostlyMap final {
  public:
    using Entry = std::pair<std::string, std::string>;
    explicit ReadMostlyMap(SnapshotLimits limits = {}, MapOptions options = {});
    ~ReadMostlyMap();
    ReadMostlyMap(const ReadMostlyMap &) = delete;
    ReadMostlyMap &operator=(const ReadMostlyMap &) = delete;
    ReadMostlyMap(ReadMostlyMap &&) = delete;
    ReadMostlyMap &operator=(ReadMostlyMap &&) = delete;

    [[nodiscard]] Snapshot acquire_snapshot() const;
    // Experimental backend only; registration is outside the hot read path.
    [[nodiscard]] HazardReader register_reader() const;
    [[nodiscard]] std::optional<std::string> find_copy(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint64_t version() const;
    [[nodiscard]] CommitResult commit(const UpdateTransaction &transaction,
                                      std::optional<std::uint64_t> expected_version = std::nullopt);
    // Replace the entire table; duplicate keys use the last value.
    [[nodiscard]] CommitResult
    replace_all(std::span<const Entry> entries,
                std::optional<std::uint64_t> expected_version = std::nullopt);
    // Wait for active writer, reject future writes, keep reads available.
    void close();
    // Deadline applies to acquiring the writer lock, not interrupting a commit.
    [[nodiscard]] bool close_until(std::chrono::steady_clock::time_point deadline);
    // Requires retention tracking and a closed map. Current-version handles
    // remain valid; only older published versions are drained.
    [[nodiscard]] bool drain_retired_until(std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] MapStatistics statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace read_mostly

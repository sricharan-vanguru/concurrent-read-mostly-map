#include <read_mostly/snapshot_builder.hpp>
int main() {
    read_mostly::UpdateTransaction update;
    update.insert_or_assign("mode", "fast");
    const auto snapshot = read_mostly::SnapshotBuilder{}.build(read_mostly::Snapshot{}, update);
    return snapshot.find_copy("mode") == "fast" ? 0 : 1;
}

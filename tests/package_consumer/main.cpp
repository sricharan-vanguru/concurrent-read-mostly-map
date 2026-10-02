#include <read_mostly/read_mostly_map.hpp>
int main() {
    read_mostly::UpdateTransaction update;
    update.insert_or_assign("mode", "fast");
    read_mostly::ReadMostlyMap map;
    const auto result = map.commit(update);
    return result.status == read_mostly::CommitStatus::committed &&
                   map.acquire_snapshot().find_copy("mode") == "fast"
               ? 0
               : 1;
}

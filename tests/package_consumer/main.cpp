#include <read_mostly/read_mostly_map.hpp>
int main() {
    read_mostly::UpdateTransaction update;
    update.insert_or_assign("mode", "fast");
    read_mostly::ReadMostlyMap map;
    const auto result = map.commit(update);
    read_mostly::MapOptions options;
    options.backend = read_mostly::PublicationBackend::experimental_hazard;
    read_mostly::ReadMostlyMap alternate({}, options);
    (void)alternate.commit(update);
    auto reader = alternate.register_reader();
    if (reader.acquire().find_copy("mode") != "fast") {
        return 1;
    }
    return result.status == read_mostly::CommitStatus::committed &&
                   map.acquire_snapshot().find_copy("mode") == "fast"
               ? 0
               : 1;
}

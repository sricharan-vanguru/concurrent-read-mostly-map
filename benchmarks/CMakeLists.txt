add_executable(read_mostly_benchmark read_mostly_benchmark.cpp)
target_link_libraries(read_mostly_benchmark PRIVATE read_mostly::map)
read_mostly_warnings(read_mostly_benchmark)
add_executable(snapshot_cost_benchmark snapshot_cost_benchmark.cpp)
target_link_libraries(snapshot_cost_benchmark PRIVATE read_mostly::map)
read_mostly_warnings(snapshot_cost_benchmark)
if(READ_MOSTLY_BUILD_TESTS)
    add_test(NAME benchmark_smoke COMMAND read_mostly_benchmark --entries 32 --threads 2
        --iterations 1000 --write-permille 10 --repeats 1)
    add_test(NAME benchmark_batch_smoke COMMAND read_mostly_benchmark --entries 32 --threads 2
        --iterations 1000 --batch 8 --write-permille 1 --burst 1 --repeats 1)
    add_test(NAME snapshot_cost_smoke COMMAND snapshot_cost_benchmark 32 32 3)
    add_test(NAME benchmark_invalid COMMAND read_mostly_benchmark --threads 0)
    set_tests_properties(benchmark_invalid PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(benchmark_smoke benchmark_batch_smoke snapshot_cost_smoke
        benchmark_invalid PROPERTIES TIMEOUT 30)
endif()

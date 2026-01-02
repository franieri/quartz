# Extension Performance Quick Reference

**Cross-Platform**: ✅ Linux | ✅ macOS (Intel & ARM) | ✅ Windows

## 🚀 Performance Targets

| Operation | Target | Slow |
|-----------|--------|------|
| Value copy | <100ns | >500ns |
| Dict lookup | <500ns | >2μs |
| Dict build (small) | <5μs | >20μs |
| Dict build (large) | <50μs | >200μs |
| Queue operation | <100ns | >1μs |

## 🔧 Quick Fixes

### ❌ Blocking IO Thread
```cpp
// BAD
Value function() {
    auto dict = buildHugeDict();  // 100μs
    return dict;
}
```

### ✅ Offload to Worker
```cpp
// GOOD
Value function() {
    QZ_PROFILE_FUNCTION();
    pool.submit([](){ buildHugeDict(); });
    return requestId;
}
```

---

### ❌ Lock Contention
```cpp
// BAD
bool sendResponse(id, resp) {
    lock_guard lock(mutex_);
    connections_[id]->send(resp);
}
```

### ✅ Lock-Free Queue
```cpp
// GOOD
bool sendResponse(id, resp) {
    return queue_.try_enqueue({id, resp});
}
// Process in poll() - no contention
```

---

### ❌ String Copies
```cpp
// BAD
dict["body"] = Value(response.body);  // COPY
```

### ✅ Move Semantics
```cpp
// GOOD
dict["body"] = Value(std::move(response.body));  // MOVE
```

---

### ❌ No Profiling
```cpp
// BAD
Value function() {
    // How slow is this? 🤷
    return doWork();
}
```

### ✅ Instrumented
```cpp
// GOOD
Value function() {
    QZ_PROFILE_FUNCTION();
    return doWork();
}
```

## 📊 Profiling Macros

```cpp
QZ_PROFILE_FUNCTION()           // Profile entire function
QZ_PROFILE_SCOPE(name)          // Profile block
QZ_PROFILE_NAMED("custom.name") // Custom name
```

## 🔄 Queue Selection

| Use Case | Queue Type | Why |
|----------|-----------|-----|
| Worker → Event loop | SPSCQueue | Fastest, single threads |
| IO threads → Worker | MPSCQueue | Multiple producers |
| Response submission | MPSCQueue | Multiple async handlers |

## 📝 Dict Building Pattern

```cpp
Value buildDict(HttpRequest&& req, Runtime* rt) {
    QZ_PROFILE_NAMED("buildDict");
    
    unordered_map<string, Value> dict;
    dict.reserve(8);  // Preallocate!
    
    // Use emplace + move
    dict.emplace("method", Value(methodToString(req.method)));
    dict.emplace("body", Value(std::move(req.body)));
    
    return rt->makeDict(std::move(dict));
}
```

## 🎯 When to Offload

| Operation | Threshold | Action |
|-----------|-----------|--------|
| Dict fields | >10 | Consider offload |
| String size | >1KB | Offload |
| Nested dicts | >3 levels | Offload |
| CPU work | >10μs | Offload |

## 🔍 Debugging Slow Functions

```cpp
// Enable profiling
qz::ExtensionProfiler::instance().setEnabled(true);

// Run test
runWorkload();

// Get stats
auto slowest = qz::ExtensionProfiler::instance().getTopSlowest(10);
for (auto& [name, stats] : slowest) {
    if (stats.avgTimeNs() > 10000) {  // >10μs
        cout << "SLOW: " << name << endl;
    }
}
```

## 📦 Includes

```cpp
#include "qz/lockfree_queue.h"      // SPSC/MPSC queues
#include "qz/extension_profiler.h"  // Profiling
#include "http_worker_pool.h"       // Worker pool
#include "uwsgi_response_queue.h"   // Response queue
```

## 🎪 Worker Pool Usage

```cpp
// Initialize (once at startup)
auto& pool = http::getWorkerPool();
pool.start(2);  // 2 workers

// Submit work
pool.submit(WorkItem{
    .callback = []() {
        // Heavy work here
    }
});

// Or use async helper
buildRequestDictAsync(req, runtime, [](Value result) {
    // Handle result
});
```

## 📈 Benchmarking

```bash
# Run benchmarks
cd benchmark
./run_benchmarks.sh

# Profile specific test
./quartz --profile test.qz

# Check results
cat benchmark/results/latest
```

## 🎓 Best Practices

1. **Profile before optimizing** - Measure, don't guess
2. **Move, don't copy** - Use `std::move()` liberally
3. **Preallocate** - Use `.reserve()` for containers
4. **Avoid locks** - Use lock-free queues
5. **Offload heavy work** - Keep IO threads responsive
6. **Batch operations** - Process multiple items at once
7. **Monitor queue depth** - Detect backpressure early

## 🚨 Red Flags

- ⛔ Blocking in extension function
- ⛔ Lock held during serialization
- ⛔ String copies in hot path
- ⛔ No profiling data
- ⛔ No worker pool for CPU work
- ⛔ Mutex on every request
- ⛔ Unbounded allocations

## ✅ Green Lights

- ✅ Non-blocking everywhere
- ✅ Lock-free communication
- ✅ Move semantics
- ✅ Profiling enabled
- ✅ Worker pool for heavy ops
- ✅ Preallocated buffers
- ✅ Benchmarked performance

## 📖 Full Documentation

- `docs/EXTENSION_FFI_PERFORMANCE.md` - Complete guide
- `docs/EXTENSION_FFI_OPTIMIZATION_SUMMARY.md` - Summary
- `docs/EXTENSION_FFI_INTEGRATION_EXAMPLE.cpp` - Examples

## 🆘 Getting Help

If extension function is slow:

1. Enable profiling
2. Run workload
3. Check `topSlowest()`
4. Identify bottleneck
5. Apply pattern from this guide
6. Benchmark improvement
7. Iterate

**Target**: <10μs for most operations, <50μs for heavy ones

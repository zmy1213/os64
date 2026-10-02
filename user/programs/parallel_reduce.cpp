#include "os64.hpp"
#include "smp.hpp"
#include "bench_workload.hpp"

namespace {
constexpr uint64_t kMaxWorkers = 12, kMaxJobs = 64, kMaxItems = 16777216;
constexpr uint32_t kTaskMagic = 0x52454455;
constexpr uint32_t kHello = 0, kResult = 1, kDone = 2;
struct Task { uint32_t magic, job; uint64_t begin, end; };
struct Message {
  uint32_t kind, worker, job, cpu;
  uint64_t begin, end, checksum;
};
static_assert(sizeof(Task) == 24 && sizeof(Message) == 40, "pipe protocol sizes");
// Both complete streams fit in their 4096-byte pipes, even if no reader runs
// while the coordinator is feeding tasks. Increasing either bound needs a
// different protocol that drains results while feeding work.
static_assert(kMaxJobs * sizeof(Task) <= 4096, "bounded task stream");
static_assert((kMaxJobs + 2 * kMaxWorkers) * sizeof(Message) <= 4096,
              "bounded result stream");

bool equal(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
bool parse_bounded(const char* text, uint64_t limit, uint64_t* result) {
  if (*text == 0) return false;
  uint64_t value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const uint64_t digit = static_cast<uint64_t>(*text - '0');
    if (digit > limit || value > (limit - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *result = value;
  return true;
}
void decimal(uint64_t value, char* output) {
  char digits[21]; size_t count = 0;
  do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
  size_t offset = 0;
  while (count) output[offset++] = digits[--count];
  output[offset] = 0;
}
void field(const char* label, uint64_t value) { print(label); number(value); print("\n"); }
// Only the coordinator uses this accumulator: each result pipe has one reader.
// Task consumers instead request exactly one whole Task in a single read.
int read_message(int fd, Message* message) {
  size_t buffered = 0;
  while (buffered < sizeof(*message)) {
    const int64_t count = read(fd, reinterpret_cast<uint8_t*>(message) + buffered,
                               sizeof(*message) - buffered);
    if (count == 0) return buffered == 0 ? 0 : -1;
    if (count < 0) return -1;
    buffered += static_cast<size_t>(count);
  }
  return 1;
}
Task partition_task(uint32_t job, uint64_t jobs, uint64_t items) {
  return {kTaskMagic, job, items * job / jobs, items * (job + 1) / jobs};
}
uint64_t map_range(uint64_t begin, uint64_t end) {
  uint64_t sum = 0;
  for (uint64_t index = begin; index < end; ++index)
    sum += bench_compute(64, index + 123);
  return sum; // Unsigned addition is defined modulo 2^64.
}
uint64_t expected_range(uint64_t begin, uint64_t end) {
  // After 64 affine steps, f(seed) = factor*seed + bias (modulo 2^64).
  // Summing consecutive seeds lets us check a block without rerunning its loop.
  const uint64_t bias = bench_expected(64, 0);
  const uint64_t factor = bench_expected(64, 1) - bias;
  const uint64_t count = end - begin;
  const uint64_t triangle = count == 0 ? 0 : count * (count - 1) / 2;
  const uint64_t seed_sum = count * (begin + 123) + triangle;
  return factor * seed_sum + bias * count;
}
void close_ordinary_except(int first, int second) {
  // spawn inherits descriptors. Drop opposite pipe ends before processing so
  // only the coordinator owns the task writer, and only workers own results.
  for (int fd = 3; fd <= 18; ++fd) if (fd != first && fd != second) close(fd);
}
int worker(uint32_t id, int task_read, int result_write) {
  close_ordinary_except(task_read, result_write);
  SmpSnapshot initial{};
  if (smp_snapshot(&initial) != 0 || initial.current_cpu >= initial.online_cpus) return 10;
  Message message{kHello, id, 0, static_cast<uint32_t>(initial.current_cpu), 0, 0, 0};
  if (write(result_write, &message, sizeof(message)) != sizeof(message)) return 11;
  uint32_t completed = 0;
  for (;;) {
    Task task{};
    // All writers submit 24 bytes atomically; all readers consume 24 bytes.
    // Therefore this particular task stream stays on whole-record boundaries.
    const int64_t count = read(task_read, &task, sizeof(task));
    if (count == 0) break;
    if (count != sizeof(task) || task.magic != kTaskMagic || task.job >= kMaxJobs ||
        task.begin > task.end || task.end > kMaxItems) return 12;
    const uint64_t sum = map_range(task.begin, task.end);
    SmpSnapshot current{};
    if (smp_snapshot(&current) != 0 || current.current_cpu != initial.current_cpu) return 13;
    message = {kResult, id, task.job, static_cast<uint32_t>(current.current_cpu),
               task.begin, task.end, sum};
    if (write(result_write, &message, sizeof(message)) != sizeof(message)) return 14;
    ++completed;
  }
  message = {kDone, id, completed, static_cast<uint32_t>(initial.current_cpu), 0, 0, 0};
  const bool sent = write(result_write, &message, sizeof(message)) == sizeof(message);
  close(task_read); close(result_write);
  return sent ? 0 : 15;
}
int coordinator(const char* path, uint64_t workers, uint64_t jobs, uint64_t items) {
  PerformanceSnapshot before{}, after{};
  SmpSnapshot cpus{};
  if (perf_snapshot(&before) != 0 || smp_snapshot(&cpus) != 0 ||
      cpus.abi_version != 1 || cpus.online_cpus < 1 || cpus.online_cpus > 4) return 2;
  const uint64_t started_at = ticks();
  int32_t task_ends[2], result_ends[2];
  if (pipe(task_ends) != 0) return 3;
  if (pipe(result_ends) != 0) { close(task_ends[0]); close(task_ends[1]); return 3; }
  char task_text[21], result_text[21], id_text[kMaxWorkers][21];
  decimal(task_ends[0], task_text); decimal(result_ends[1], result_text);
  int64_t pids[kMaxWorkers]{}; size_t started = 0;
  for (; started < workers; ++started) {
    decimal(started, id_text[started]);
    const char* args[] = {path, "worker", id_text[started], task_text, result_text};
    pids[started] = spawn(path, args, 5);
    if (pids[started] < 0) break;
  }
  close(task_ends[0]); close(result_ends[1]);
  bool ok = started == workers;
  if (ok) {
    for (uint32_t job = 0; job < jobs; ++job) {
      const Task task = partition_task(job, jobs, items);
      if (write(task_ends[1], &task, sizeof(task)) != sizeof(task)) { ok = false; break; }
    }
  }
  close(task_ends[1]); // The final task writer disappears: drain, then EOF.
  bool hello[kMaxWorkers]{}, done[kMaxWorkers]{}, seen[kMaxJobs]{};
  uint32_t assigned[kMaxWorkers]{}, completed[kMaxWorkers]{};
  uint64_t sum = 0, received = 0, work_mask = 0;
  Message message{}; int read_status = 0;
  while (ok && (read_status = read_message(result_ends[0], &message)) == 1) {
    const uint32_t id = message.worker;
    if (id >= started || message.cpu >= cpus.online_cpus || done[id]) { ok = false; break; }
    if (message.kind == kHello) {
      if (hello[id] || message.job || message.begin || message.end || message.checksum) { ok = false; break; }
      hello[id] = true; assigned[id] = message.cpu;
    } else if (!hello[id] || assigned[id] != message.cpu) { ok = false; break; }
    else if (message.kind == kResult) {
      if (message.job >= jobs || seen[message.job]) { ok = false; break; }
      const Task expected = partition_task(message.job, jobs, items);
      if (message.begin != expected.begin || message.end != expected.end ||
          message.checksum != expected_range(expected.begin, expected.end)) { ok = false; break; }
      seen[message.job] = true; ++completed[id]; ++received; sum += message.checksum;
      if (expected.begin < expected.end) work_mask |= 1ULL << message.cpu;
    } else if (message.kind == kDone) {
      if (message.job != completed[id] || message.begin || message.end || message.checksum) { ok = false; break; }
      done[id] = true;
    } else { ok = false; break; }
  }
  close(result_ends[0]); // On a protocol error this also releases blocked writers.
  if (read_status < 0) ok = false;
  for (size_t id = 0; id < started; ++id) {
    int32_t status = -1;
    if (waitpid(pids[id], &status) != pids[id] || status != 0 || !hello[id] || !done[id]) ok = false;
  }
  const uint64_t expected = expected_range(0, items);
  const bool have_after = perf_snapshot(&after) == 0;
  if (received != jobs || sum != expected || !have_after ||
      after.free_pages != before.free_pages || after.heap_used_bytes != before.heap_used_bytes) ok = false;
  field("parallel_reduce workers=", workers); field("parallel_reduce jobs=", jobs);
  field("parallel_reduce items=", items); field("parallel_reduce online_cpus=", cpus.online_cpus);
  field("parallel_reduce work_cpu_mask=", work_mask); field("parallel_reduce checksum=", sum);
  field("parallel_reduce expected=", expected); field("parallel_reduce completed_jobs=", received);
  field("parallel_reduce elapsed_ticks=", ticks() - started_at);
  field("parallel_reduce free_pages_before=", before.free_pages);
  field("parallel_reduce free_pages_after=", after.free_pages);
  field("parallel_reduce heap_bytes_before=", before.heap_used_bytes);
  field("parallel_reduce heap_bytes_after=", after.heap_used_bytes);
  for (size_t id = 0; id < started; ++id) {
    print("parallel_reduce worker="); number(id); print(" cpu="); number(assigned[id]);
    print(" jobs="); number(completed[id]); print("\n");
  }
  print(ok ? "parallel_reduce tasks_once_full64_resources_ok\n" : "parallel_reduce: protocol, checksum or resource check failed\n");
  return ok ? 0 : 4;
}
}
extern "C" int main(int argc, char** argv) {
  if (argc == 5 && equal(argv[1], "worker")) {
    uint64_t id = 0, task_read = 0, result_write = 0;
    if (!parse_bounded(argv[2], kMaxWorkers - 1, &id) ||
        !parse_bounded(argv[3], 18, &task_read) || task_read < 3 ||
        !parse_bounded(argv[4], 18, &result_write) || result_write < 3 || task_read == result_write) return 1;
    return worker(static_cast<uint32_t>(id), static_cast<int>(task_read), static_cast<int>(result_write));
  }
  uint64_t workers = 4, jobs = 64, items = 1048576;
  if (argc != 1 && (argc != 4 || !parse_bounded(argv[1], kMaxWorkers, &workers) || workers == 0 ||
      !parse_bounded(argv[2], kMaxJobs, &jobs) || jobs == 0 ||
      !parse_bounded(argv[3], kMaxItems, &items) || items == 0)) {
    error("usage: parallel_reduce [workers(1..12) jobs(1..64) items(1..16777216)]\n");
    return 1;
  }
  return coordinator(argv[0], workers, jobs, items);
}

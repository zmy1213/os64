#include "os64.hpp"
#include "memory.hpp"
#include "bench_workload.hpp"
#include "smp.hpp"

namespace {
constexpr size_t kMaxWorkers = 8;
constexpr uint64_t kComputeIterations = 500000;
constexpr uint32_t kRecordMagic = 0x434f4f50; // ASCII 提示：COOP，协作。
constexpr size_t kSmallRecord = 257, kLargeRecord = 4096;
constexpr size_t kComputeInterval = 64;

// 这是测试自己的消息格式，不是管道接口。管道只认识字节。
struct RecordHeader {
  uint32_t magic, worker, sequence, bytes;
  uint64_t result, cpu;
};
static_assert(sizeof(RecordHeader) == 32, "cooperation test record header");

bool equal(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
bool integer(const char* text, uint64_t* output) {
  uint64_t value = 0;
  if (!*text) return false;
  while (*text) {
    if (*text < '0' || *text > '9' || value > (UINT64_MAX - (*text - '0')) / 10)
      return false;
    value = value * 10 + (*text++ - '0');
  }
  *output = value;
  return true;
}
void decimal(uint64_t value, char* output) {
  char reverse[21]; size_t count = 0;
  do { reverse[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
  size_t used = 0;
  while (count) output[used++] = reverse[--count];
  output[used] = 0;
}
void field(const char* label, uint64_t value) {
  print(label); number(value); print("\n");
}
size_t record_size(uint32_t sequence) {
  return sequence % 2 == 0 ? kSmallRecord : kLargeRecord;
}
unsigned char payload_byte(const RecordHeader& header, size_t position) {
  return static_cast<unsigned char>((position * 17 + header.worker * 31 + header.sequence * 7) ^
      (header.result >> ((position & 7) * 8)));
}

int worker(uint32_t id, int data_read, int data_write,
           int gate_read, int gate_write, uint32_t rounds) {
  // 关闭自己不需要的端点。留下 gate_write 会妨碍发现父进程中止启动。
  close(data_read); close(gate_write);
  char go = 0;
  const int64_t started = read(gate_read, &go, 1);
  close(gate_read);
  if (started != 1 || go != 'G') return 10;

  alignas(8) unsigned char record[kLargeRecord];
  uint64_t result = id + 123;
  SmpSnapshot location{};
  for (uint32_t sequence = 0; sequence < rounds; ++sequence) {
    if (sequence % kComputeInterval == 0) {
      if (smp_snapshot(&location) != 0 || location.abi_version != 1 ||
          location.current_cpu >= 4 ||
          !(location.online_mask & (1ULL << location.current_cpu))) return 11;
      // 无 sleep/yield：计算可在多个 CPU 上进行，管道满时才阻塞。
      result = bench_compute(kComputeIterations, result);
    }
    RecordHeader header{kRecordMagic, id, sequence,
                        static_cast<uint32_t>(record_size(sequence)), result,
                        location.current_cpu};
    memcpy(record, &header, sizeof(header));
    for (size_t i = sizeof(header); i < header.bytes; ++i)
      record[i] = payload_byte(header, i);
    // 一次 write <=4096B。拆成两次调用就不能验证完整记录的原子性。
    if (write(data_write, record, header.bytes) != header.bytes) return 12;
  }
  close(data_write);
  return 0;
}

int blocked_writer(int data_read, int data_write, int control_read, int control_write) {
  close(data_read); close(control_read);
  if (write(control_write, "S", 1) != 1) return 20;
  // 父进程已填满缓冲，这里必须等待；最后一个读端关闭后应以 EPIPE 返回。
  const int64_t result = write(data_write, "X", 1);
  close(data_write); close(control_write);
  return result == BROKEN_PIPE ? 0 : 21;
}

bool close_wakes_blocked_writer(const char* path) {
  int32_t data[2], control[2];
  if (pipe(data) != 0) return false;
  if (pipe(control) != 0) { close(data[0]); close(data[1]); return false; }
  char full[kLargeRecord]; memset(full, 'F', sizeof(full));
  bool ok = write(data[1], full, sizeof(full)) == sizeof(full);
  PerformanceSnapshot baseline{};
  if (perf_snapshot(&baseline) != 0) ok = false;
  char descriptors[4][21];
  decimal(data[0], descriptors[0]); decimal(data[1], descriptors[1]);
  decimal(control[0], descriptors[2]); decimal(control[1], descriptors[3]);
  const char* arguments[] = {path, "blockedwriter", descriptors[0], descriptors[1],
                             descriptors[2], descriptors[3]};
  const int64_t child = ok ? spawn(path, arguments, 6) : -1;
  close(control[1]); close(data[1]);
  if (child >= 0) {
    char started = 0;
    if (read(control[0], &started, 1) != 1 || started != 'S') ok = false;
    // 不用固定延时猜测子进程是否已阻塞：看调度器实际登记的 blocked 数量。
    const uint64_t deadline = ticks() + baseline.timer_hz * 5;
    bool observed_blocked = false;
    while (ok && ticks() < deadline) {
      PerformanceSnapshot current{};
      if (perf_snapshot(&current) != 0) { ok = false; break; }
      if (current.blocked_threads > baseline.blocked_threads) { observed_blocked = true; break; }
      sleep(1);
    }
    if (!observed_blocked) ok = false;
  } else ok = false;
  close(data[0]); close(control[0]);
  if (child >= 0) {
    int32_t status = -1;
    if (waitpid(child, &status) != child || status != 0) ok = false;
  }
  print(ok ? "coop_test blocked_writer_close_epipe_ok\n" :
             "coop_test: blocked writer close did not wake correctly\n");
  return ok;
}

bool check_record(const unsigned char* record, size_t bytes, uint32_t workers,
                  uint32_t rounds, uint32_t* next_sequence, uint64_t* cpu_mask) {
  RecordHeader header;
  memcpy(&header, record, sizeof(header));
  if (header.magic != kRecordMagic || header.worker >= workers || header.cpu >= 4 ||
      header.sequence >= rounds || header.sequence != next_sequence[header.worker] ||
      header.bytes != bytes || bytes != record_size(header.sequence)) return false;
  const uint64_t batches = header.sequence / kComputeInterval + 1;
  if (header.result != bench_expected(batches * kComputeIterations, header.worker + 123))
    return false;
  for (size_t i = sizeof(header); i < bytes; ++i)
    if (record[i] != payload_byte(header, i)) return false;
  ++next_sequence[header.worker];
  *cpu_mask |= 1ULL << header.cpu;
  return true;
}

int cooperate(const char* path, uint32_t workers, uint32_t rounds) {
  PerformanceSnapshot resources_before{}, resources_after{};
  SmpSnapshot cpus_before{}, cpus_after{};
  if (perf_snapshot(&resources_before) != 0 || smp_snapshot(&cpus_before) != 0) return 2;
  if (!close_wakes_blocked_writer(path)) return 5;
  int32_t data[2], gate[2];
  if (pipe(data) != 0) return 3;
  if (pipe(gate) != 0) { close(data[0]); close(data[1]); return 3; }

  char descriptors[4][21], round_text[21], identities[kMaxWorkers][21];
  decimal(data[0], descriptors[0]); decimal(data[1], descriptors[1]);
  decimal(gate[0], descriptors[2]); decimal(gate[1], descriptors[3]);
  decimal(rounds, round_text);
  int64_t pids[kMaxWorkers]; uint32_t started = 0; bool ok = true;
  for (uint32_t i = 0; i < workers; ++i) {
    decimal(i, identities[i]);
    const char* arguments[] = {path, "worker", identities[i], descriptors[0], descriptors[1],
                               descriptors[2], descriptors[3], round_text};
    pids[i] = spawn(path, arguments, 8);
    if (pids[i] >= 0) ++started; else ok = false;
  }
  close(data[1]); close(gate[0]);
  // 等全部 spawn 完成后才开闸。每个子进程只读一个 G。
  char tokens[kMaxWorkers]; memset(tokens, 'G', sizeof(tokens));
  if (write(gate[1], tokens, started) != started) ok = false;
  close(gate[1]);

  uint32_t next_sequence[kMaxWorkers]{};
  uint64_t cpu_mask = 0, total_bytes = 0, records = 0;
  alignas(8) unsigned char record[kLargeRecord];
  unsigned char incoming[777];
  size_t used = 0, expected = sizeof(RecordHeader);
  int64_t received = 0;
  while (ok && (received = read(data[0], incoming, sizeof(incoming))) > 0) {
    size_t cursor = 0;
    total_bytes += received;
    while (ok && cursor < static_cast<size_t>(received)) {
      const size_t remaining = static_cast<size_t>(received) - cursor;
      const size_t count = expected - used < remaining ? expected - used : remaining;
      memcpy(record + used, incoming + cursor, count);
      cursor += count; used += count;
      if (used != expected) continue;
      if (expected == sizeof(RecordHeader)) {
        RecordHeader header; memcpy(&header, record, sizeof(header));
        if (header.magic != kRecordMagic ||
            (header.bytes != kSmallRecord && header.bytes != kLargeRecord)) { ok = false; break; }
        expected = header.bytes;
      } else {
        ok = check_record(record, expected, workers, rounds, next_sequence, &cpu_mask);
        ++records; used = 0; expected = sizeof(RecordHeader);
      }
    }
  }
  close(data[0]); // 检测到坏记录时也唤醒阻塞写者，让它们以 EPIPE 退出。
  if (received < 0 || used != 0) ok = false;
  for (uint32_t i = 0; i < workers; ++i) {
    if (pids[i] >= 0) {
      int32_t status = -1;
      if (waitpid(pids[i], &status) != pids[i] || status != 0) ok = false;
    }
    if (next_sequence[i] != rounds) ok = false;
  }
  const uint64_t bytes_per_worker = uint64_t(rounds / 2) * (kSmallRecord + kLargeRecord) +
                                  (rounds % 2 ? kSmallRecord : 0);
  if (total_bytes != bytes_per_worker * workers || records != uint64_t(rounds) * workers ||
      perf_snapshot(&resources_after) != 0 || smp_snapshot(&cpus_after) != 0 ||
      resources_before.free_pages != resources_after.free_pages) ok = false;
  uint64_t dispatch_mask = 0;
  for (size_t i = 0; i < 4; ++i)
    if (cpus_after.user_dispatches[i] > cpus_before.user_dispatches[i]) dispatch_mask |= 1ULL << i;
  field("coop_test online_cpus=", cpus_after.online_cpus);
  field("coop_test online_mask=", cpus_after.online_mask);
  field("coop_test cpu_mask=", cpu_mask);
  field("coop_test dispatch_cpu_mask=", dispatch_mask);
  field("coop_test bytes=", total_bytes);
  field("coop_test records=", records);
  field("coop_test elapsed_ticks=", resources_after.ticks - resources_before.ticks);
  print(ok ? "coop_test records_atomic_compute_ok\n" : "coop_test: data, compute or resource check failed\n");
  return ok ? 0 : 4;
}
}

extern "C" int main(int argc, char** argv) {
  if (argc == 6 && equal(argv[1], "blockedwriter")) {
    uint64_t descriptors[4];
    for (size_t i = 0; i < 4; ++i)
      if (!integer(argv[i + 2], &descriptors[i]) || descriptors[i] > 18) return 1;
    return blocked_writer(descriptors[0], descriptors[1], descriptors[2], descriptors[3]);
  }
  if (argc == 8 && equal(argv[1], "worker")) {
    uint64_t args[6];
    for (size_t i = 0; i < 6; ++i) if (!integer(argv[i + 2], &args[i])) return 1;
    if (args[0] >= kMaxWorkers || args[5] < 1 || args[5] > 4096) return 1;
    for (size_t i = 1; i < 5; ++i) if (args[i] > 18) return 1;
    return worker(args[0], args[1], args[2], args[3], args[4], args[5]);
  }
  uint64_t workers = 4, rounds = 2048;
  if (argc > 3 || (argc > 1 && !integer(argv[1], &workers)) ||
      (argc > 2 && !integer(argv[2], &rounds)) ||
      workers < 1 || workers > kMaxWorkers || rounds < 1 || rounds > 4096) {
    error("usage: run /bin/coop_test [workers 1..8] [rounds 1..4096]\n"); return 1;
  }
  return cooperate(argv[0], workers, rounds);
}

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

void
assert_test(int condition, char *test_name)
{
  if (condition) {
    printf("[PASS] %s\n", test_name);
  } else {
    printf("[FAIL] %s\n", test_name);
  }
}

// Kiểm thử đọc nội dung file bất kỳ trong /proc
void
test_read_file(char *path, char *test_name)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    assert_test(0, test_name);
    return;
  }

  char buf[256];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n > 0) {
    buf[n] = '\0';
    assert_test(1, test_name);
  } else {
    assert_test(0, test_name);
  }
}

// Kiểm thử trường hợp đọc với buffer dung lượng nhỏ (Chunked read)
void
test_small_buffer(void)
{
  int fd = open("/proc/uptime", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Doc voi buffer nho (Mo file /proc/uptime)");
    return;
  }

  char buf[4];
  int total_read = 0;
  int n;

  while ((n = read(fd, buf, 3)) > 0) {
    total_read += n;
  }
  close(fd);

  assert_test(total_read > 0, "Doc voi buffer nho (Read chunking)");
}

// Kiểm thử phân giải cây thư mục 2 cấp và fstat
void
test_two_level_lookup(void)
{
  struct stat st;
  int fd;

  // Cấp 0: /proc
  fd = open("/proc", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_DIR || st.ino != 9000) {
    assert_test(0, "Lookup /proc va xac dinh T_DIR");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc va xac dinh T_DIR");
  }

  // Cấp 1: /proc/1
  fd = open("/proc/1", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_DIR || st.ino != 9100) {
    assert_test(0, "Lookup /proc/1 va xac dinh T_DIR");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1 va xac dinh T_DIR");
  }

  // Cấp 2: /proc/1/status
  fd = open("/proc/1/status", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_FILE || st.ino != 9200) {
    assert_test(0, "Lookup /proc/1/status va xac dinh T_FILE");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1/status va xac dinh T_FILE");
  }

  // Cấp 2: /proc/1/maps
  fd = open("/proc/1/maps", O_RDONLY);
  if (fd < 0 || fstat(fd, &st) < 0 || st.type != T_FILE || st.ino != 9300) {
    assert_test(0, "Lookup /proc/1/maps va xac dinh T_FILE");
    if (fd >= 0)
      close(fd);
  } else {
    close(fd);
    assert_test(1, "Lookup /proc/1/maps va xac dinh T_FILE");
  }

  // Từ chối đường dẫn không tồn tại hoặc vượt quá 2 cấp
  int bad1 = open("/proc/9999", O_RDONLY);
  int bad2 = open("/proc/1/status/extra", O_RDONLY);
  if (bad1 < 0 && bad2 < 0) {
    assert_test(1, "Tu choi duong dan khong hop le / vuot cap");
  } else {
    if (bad1 >= 0)
      close(bad1);
    if (bad2 >= 0)
      close(bad2);
    assert_test(0, "Tu choi duong dan khong hop le / vuot cap");
  }
}
void
test_proc_maps(void)
{
  char path[64];
  char buf_before[512];
  char buf_after[512];
  int pid = getpid();

  // Tạo đường dẫn "/proc/<pid>/maps" cho tiến trình hiện tại
  int pos = 0;
  const char *p1 = "/proc/";
  const char *p2 = "/maps";
  while (*p1)
    path[pos++] = *p1++;
  if (pid >= 10)
    path[pos++] = '0' + (pid / 10);
  path[pos++] = '0' + (pid % 10);
  while (*p2)
    path[pos++] = *p2++;
  path[pos] = '\0';

  // 1. Kiểm tra đọc /proc/<pid>/maps trước khi cấp phát thêm bộ nhớ
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("FAIL: cannot open %s\n", path);
    exit(1);
  }
  int n1 = read(fd, buf_before, sizeof(buf_before) - 1);
  close(fd);
  if (n1 <= 0) {
    printf("FAIL: empty maps output\n");
    exit(1);
  }
  buf_before[n1] = '\0';

  // 2. Gọi sbrk() mở rộng heap thêm 2 trang (8192 bytes) và kiểm tra thay đổi maps
  if (sbrk(8192) == (char *)-1) {
    printf("FAIL: sbrk failed\n");
    exit(1);
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("FAIL: cannot reopen %s after sbrk\n", path);
    exit(1);
  }
  int n2 = read(fd, buf_after, sizeof(buf_after) - 1);
  close(fd);
  if (n2 <= 0) {
    printf("FAIL: empty maps output after sbrk\n");
    exit(1);
  }
  buf_after[n2] = '\0';

  // Nội dung maps sau khi sbrk() phải thay đổi so với ban đầu
  if (strcmp(buf_before, buf_after) == 0) {
    printf("FAIL: maps did not update after sbrk()\n");
    exit(1);
  }

  // 3. Kiểm tra trường hợp biên: PID không tồn tại
  int bad_fd = open("/proc/99999/maps", O_RDONLY);
  if (bad_fd >= 0) {
    close(bad_fd);
    printf("FAIL: /proc/99999/maps should not exist\n");
    exit(1);
  }

  printf("PASS: test_proc_maps\n");
}
// Kiểm thử duyệt thư mục ảo
void
test_readdir(void)
{
  int fd = open("/proc", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Duyet thu muc /proc");
    return;
  }

  struct dirent de;
  int has_dot = 0, has_dotdot = 0, has_uptime = 0, has_meminfo = 0,
      has_pid1 = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (strcmp(de.name, ".") == 0)
      has_dot = 1;
    else if (strcmp(de.name, "..") == 0)
      has_dotdot = 1;
    else if (strcmp(de.name, "uptime") == 0)
      has_uptime = 1;
    else if (strcmp(de.name, "meminfo") == 0)
      has_meminfo = 1;
    else if (strcmp(de.name, "1") == 0)
      has_pid1 = 1;
  }
  close(fd);

  int proc_ok = has_dot && has_dotdot && has_uptime && has_meminfo && has_pid1;
  assert_test(proc_ok, "Duyet thu muc /proc (., .., uptime, meminfo, PID)");

  // Duyệt /proc/1
  fd = open("/proc/1", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Duyet thu muc /proc/1");
    return;
  }

  has_dot = 0;
  has_dotdot = 0;
  int has_status = 0, has_maps = 0, has_nice = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (strcmp(de.name, ".") == 0)
      has_dot = 1;
    else if (strcmp(de.name, "..") == 0)
      has_dotdot = 1;
    else if (strcmp(de.name, "status") == 0)
      has_status = 1;
    else if (strcmp(de.name, "maps") == 0)
      has_maps = 1;
    else if (strcmp(de.name, "nice") == 0)
      has_nice = 1;
  }
  close(fd);

  int pid1_ok = has_dot && has_dotdot && has_status && has_maps && has_nice;
  assert_test(pid1_ok, "Duyet thu muc /proc/1 (., .., status, maps, nice)");
}

// Kiểm thử từ chối các thao tác ghi / sửa đổi trên ProcFS và VFS hooks
void
test_read_only_protection(void)
{
  int w1 = open("/proc/uptime", O_WRONLY);
  int w2 = open("/proc/uptime", O_RDWR);
  int w3 = open("/proc/uptime", O_CREATE | O_RDONLY);
  int w4 = open("/proc/uptime", O_TRUNC | O_RDONLY);
  int w5 = open("/proc/1/status", O_WRONLY);
  int w6 = open("/proc/newfile", O_CREATE | O_RDWR);

  if (w1 >= 0)
    close(w1);
  if (w2 >= 0)
    close(w2);
  if (w3 >= 0)
    close(w3);
  if (w4 >= 0)
    close(w4);
  if (w5 >= 0)
    close(w5);
  if (w6 >= 0)
    close(w6);

  int open_flags_blocked =
    (w1 < 0 && w2 < 0 && w3 < 0 && w4 < 0 && w5 < 0 && w6 < 0);
  assert_test(open_flags_blocked, "Tu choi O_WRONLY/O_RDWR/O_CREATE/O_TRUNC");

  int unl = unlink("/proc/uptime");
  int mkd = mkdir("/proc/newdir");
  int lnk = link("/proc/uptime", "uptimelink");

  assert_test(unl < 0, "Chan thao tac unlink /proc");
  assert_test(mkd < 0, "Chan thao tac mkdir /proc");
  assert_test(lnk < 0, "Chan thao tac link /proc");
}

// Kiểm thử rò rỉ Inode cache (NINODE = 50, lặp 1.000 lần mở và đóng)
void
test_inode_cache_leak(void)
{
  int ok = 1;
  for (int i = 0; i < 1000; i++) {
    int fd1 = open("/proc/uptime", O_RDONLY);
    int fd2 = open("/proc/meminfo", O_RDONLY);
    int fd3 = open("/proc/1/status", O_RDONLY);
    int fd4 = open("/proc/1/maps", O_RDONLY);
    if (fd1 < 0 || fd2 < 0 || fd3 < 0 || fd4 < 0) {
      ok = 0;
      if (fd1 >= 0)
        close(fd1);
      if (fd2 >= 0)
        close(fd2);
      if (fd3 >= 0)
        close(fd3);
      if (fd4 >= 0)
        close(fd4);
      break;
    }
    close(fd1);
    close(fd2);
    close(fd3);
    close(fd4);
  }
  assert_test(ok, "Khong ro ri Inode cache (lap 1000 lan mo/dong file)");
}

// Kiểm thử tính năng sinh danh sách thư mục động theo p->state != UNUSED
void
test_dynamic_proc_listing(void)
{
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    assert_test(0, "Tao pipe cho kiem thu dong");
    return;
  }

  int pid = fork();
  if (pid < 0) {
    assert_test(0, "Fork tien trinh kiem thu dong");
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }

  if (pid == 0) {
    // Tiến trình con: đợi cha báo tín hiệu qua pipe rồi thoát
    close(pipefd[1]);
    char ch;
    read(pipefd[0], &ch, 1);
    close(pipefd[0]);
    exit(0);
  }

  // Tiến trình cha:
  close(pipefd[0]);

  // Chuyển số PID của con thành chuỗi
  char pid_str[16];
  int n = pid, i = 0, j = 0;
  char tmp[16];
  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }
  while (i > 0)
    pid_str[j++] = tmp[--i];
  pid_str[j] = '\0';

  // Bước 1: Khi tiến trình con đang chạy (state != UNUSED), quét /proc PHẢI thấy thư mục PID
  int fd = open("/proc", O_RDONLY);
  int found_child_while_running = 0;
  if (fd >= 0) {
    struct dirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (strcmp(de.name, pid_str) == 0) {
        found_child_while_running = 1;
        break;
      }
    }
    close(fd);
  }

  // Bước 2: Báo cho tiến trình con thoát và gọi wait(0) để dọn dẹp state về UNUSED
  write(pipefd[1], "x", 1);
  close(pipefd[1]);
  wait(0);

  // Bước 3: Sau khi con đã thoát hoàn toàn (state == UNUSED), quét /proc KHÔNG CÒN thấy PID
  fd = open("/proc", O_RDONLY);
  int found_child_after_exit = 0;
  if (fd >= 0) {
    struct dirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (strcmp(de.name, pid_str) == 0) {
        found_child_after_exit = 1;
        break;
      }
    }
    close(fd);
  }

  int dynamic_ok =
    (found_child_while_running == 1) && (found_child_after_exit == 0);
  assert_test(dynamic_ok, "Sinh danh sach dong /proc theo p->state != UNUSED");
}

static void
build_proc_entry_path(int pid, const char *entry, char *out)
{
  const char *prefix = "/proc/";
  char digits[16];
  int d = 0, pos = 0;

  while (*prefix)
    out[pos++] = *prefix++;

  if (pid == 0) {
    digits[d++] = '0';
  } else {
    int tmp = pid;
    while (tmp > 0 && d < (int)sizeof(digits)) {
      digits[d++] = '0' + (tmp % 10);
      tmp /= 10;
    }
  }
  while (d > 0)
    out[pos++] = digits[--d];

  out[pos++] = '/';
  while (*entry)
    out[pos++] = *entry++;
  out[pos] = '\0';
}

static int
read_proc_rtime(int pid)
{
  char path[64];
  char buf[256];
  int fd, n;

  build_proc_entry_path(pid, "status", path);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';

  const char *key = "Rtime:";
  for (int i = 0; buf[i] != '\0'; i++) {
    if (i == 0 || buf[i - 1] == '\n') {
      int j = 0;
      while (key[j] && buf[i + j] == key[j])
        j++;
      if (key[j] == '\0') {
        int k = i + j;
        while (buf[k] == ' ' || buf[k] == '\t')
          k++;
        if (buf[k] < '0' || buf[k] > '9')
          return -1;
        int val = 0;
        while (buf[k] >= '0' && buf[k] <= '9') {
          val = val * 10 + (buf[k] - '0');
          k++;
        }
        return val;
      }
    }
  }
  return -1;
}

void
test_proc_rtime(void)
{
  int mypid = getpid();
  int rt_before = read_proc_rtime(mypid);
  int bad_rt = read_proc_rtime(99999);
  assert_test(rt_before >= 0 && bad_rt < 0,
              "Doc truong Rtime trong /proc/<pid>/status");

  // Đốt CPU trong vài timer ticks để xác nhận clockintr cập nhật p->rtime
  volatile uint64 spin = 0;
  int t0 = uptime();
  while (uptime() - t0 < 6) {
    spin++;
  }
  (void)spin;
  int rt_after = read_proc_rtime(mypid);
  assert_test(rt_after > rt_before, "rtime tang khi tien trinh chay CPU");

  // Kiểm tra reset rtime = 0 khi allocproc() và không tăng khi đang SLEEPING
  int pfd[2];
  int sleep_ok = 0;
  if (pipe(pfd) == 0) {
    int cpid = fork();
    if (cpid == 0) {
      close(pfd[0]);
      write(pfd[1], "r", 1);
      close(pfd[1]);
      pause(6);
      exit(0);
    } else if (cpid > 0) {
      char ch = 0;
      close(pfd[1]);
      read(pfd[0], &ch, 1);
      close(pfd[0]);
      pause(1);
      int rt_s1 = read_proc_rtime(cpid);
      pause(3);
      int rt_s2 = read_proc_rtime(cpid);
      wait(0);
      if (rt_s1 >= 0 && rt_s1 <= 2 && rt_s2 >= rt_s1 && (rt_s2 - rt_s1) <= 1) {
        sleep_ok = 1;
      }
    }
  }
  assert_test(sleep_ok, "rtime duoc reset khi fork va khong tang khi sleep");

  // Kiểm thử đa tiến trình chạy CPU đồng thời (an toàn SMP)
  int wpipe[2];
  int smp_ok = 0;
  if (pipe(wpipe) == 0) {
    for (int w = 0; w < 2; w++) {
      int wpid = fork();
      if (wpid == 0) {
        close(wpipe[0]);
        volatile uint64 work = 0;
        int start = uptime();
        while (uptime() - start < 5) {
          work++;
        }
        (void)work;
        int rt = read_proc_rtime(getpid());
        write(wpipe[1], &rt, sizeof(rt));
        close(wpipe[1]);
        exit(0);
      }
    }
    close(wpipe[1]);
    int r1 = -1, r2 = -1;
    read(wpipe[0], &r1, sizeof(r1));
    read(wpipe[0], &r2, sizeof(r2));
    close(wpipe[0]);
    wait(0);
    wait(0);
    if (r1 > 0 && r2 > 0)
      smp_ok = 1;
  }
  assert_test(smp_ok, "Cap nhat rtime doc lap cho nhieu tien trinh SMP");
}

static int
read_proc_nice(int pid, int *out_val)
{
  char path[64];
  char buf[32];
  int fd, n;

  build_proc_entry_path(pid, "nice", path);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';

  int i = 0, neg = 0, val = 0;
  if (buf[i] == '-') {
    neg = 1;
    i++;
  }
  if (buf[i] < '0' || buf[i] > '9')
    return -1;
  while (buf[i] >= '0' && buf[i] <= '9') {
    val = val * 10 + (buf[i] - '0');
    i++;
  }
  *out_val = neg ? -val : val;
  return 0;
}

static int
write_proc_nice_flags(int pid, const char *str, int flags)
{
  char path[64];
  build_proc_entry_path(pid, "nice", path);
  int fd = open(path, flags);
  if (fd < 0)
    return -1;
  int len = strlen(str);
  int w = write(fd, str, len);
  close(fd);
  return (w == len) ? 0 : -1;
}

void
test_proc_nice(void)
{
  struct stat st;
  int fd = open("/proc/1/nice", O_RDONLY);
  int lookup_ok =
    (fd >= 0 && fstat(fd, &st) == 0 && st.type == T_FILE && st.ino == 9400);
  if (fd >= 0)
    close(fd);
  assert_test(lookup_ok, "Lookup /proc/1/nice va xac dinh T_FILE (ino 9400)");

  int mypid = getpid();
  int val = -999;
  int def_ok = (read_proc_nice(mypid, &val) == 0 && val == 0);
  assert_test(def_ok, "Doc gia tri nice mac dinh = 0");

  // Mô phỏng ghi qua echo redirection (O_WRONLY | O_CREATE | O_TRUNC) với số âm & dương
  int rw_ok = 1;
  if (write_proc_nice_flags(mypid, "-10\n", O_WRONLY | O_CREATE | O_TRUNC) <
        0 ||
      read_proc_nice(mypid, &val) < 0 || val != -10)
    rw_ok = 0;
  if (write_proc_nice_flags(mypid, "19\n", O_WRONLY | O_CREATE | O_TRUNC) < 0 ||
      read_proc_nice(mypid, &val) < 0 || val != 19)
    rw_ok = 0;

  // Kiểm tra hành vi 2 lần write() của user/echo.c: write("7") rồi write("\n")
  char path[64];
  build_proc_entry_path(mypid, "nice", path);
  int efd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
  if (efd < 0 || write(efd, "7", 1) != 1 || write(efd, "\n", 1) != 1)
    rw_ok = 0;
  if (efd >= 0)
    close(efd);
  if (read_proc_nice(mypid, &val) < 0 || val != 7)
    rw_ok = 0;
  assert_test(rw_ok, "Cap nhat /proc/<pid>/nice giong echo (> va so am/duong)");

  // Kiểm tra từ chối tham số sai biên / sai định dạng / PID không tồn tại
  int inv_ok = 1;
  if (write_proc_nice_flags(mypid, "20\n", O_WRONLY) == 0)
    inv_ok = 0;
  if (write_proc_nice_flags(mypid, "-21\n", O_WRONLY) == 0)
    inv_ok = 0;
  if (write_proc_nice_flags(mypid, "abc\n", O_WRONLY) == 0)
    inv_ok = 0;
  if (write_proc_nice_flags(mypid, "5xyz\n", O_WRONLY) == 0)
    inv_ok = 0;
  if (read_proc_nice(mypid, &val) < 0 || val != 7)
    inv_ok = 0;
  int bad_pid = open("/proc/99999/nice", O_WRONLY | O_CREATE | O_TRUNC);
  if (bad_pid >= 0) {
    close(bad_pid);
    inv_ok = 0;
  }
  assert_test(inv_ok,
              "Tu choi nice ngoai bien (-20..19), sai cu phap va PID ao");

  // Khôi phục nice = 0 trước khi chạy test lập lịch
  write_proc_nice_flags(mypid, "0\n", O_WRONLY);

  // Kiểm thử bộ lập lịch ưu tiên (3 worker nice=-15 vs 3 worker nice=15)
  // Dùng 6 worker (> 4 CPU) để tạo cạnh tranh CPU thật trên cả CPUS=1 và CPUS=4.
  int gate[2], res[2];
  int sched_ok = 0;
  if (pipe(gate) == 0 && pipe(res) == 0) {
    for (int i = 0; i < 6; i++) {
      int is_high = (i < 3);
      int pid = fork();
      if (pid == 0) {
        close(gate[1]);
        close(res[0]);
        write_proc_nice_flags(getpid(), is_high ? "-15\n" : "15\n", O_WRONLY);
        char go = 0;
        read(gate[0], &go, 1);
        close(gate[0]);

        volatile uint64 work = 0;
        int start = uptime();
        while (uptime() - start < 15) {
          work++;
        }
        (void)work;

        int msg[2];
        msg[0] = is_high;
        msg[1] = read_proc_rtime(getpid());
        write(res[1], msg, sizeof(msg));
        close(res[1]);
        exit(0);
      }
    }
    close(gate[0]);
    close(res[1]);
    for (int i = 0; i < 6; i++)
      write(gate[1], "g", 1);
    close(gate[1]);

    int sum_high = 0, sum_low = 0;
    int all_ran = 1;
    for (int i = 0; i < 6; i++) {
      int msg[2] = {-1, -1};
      if (read(res[0], msg, sizeof(msg)) != sizeof(msg) || msg[1] < 0)
        all_ran = 0;
      else if (msg[0])
        sum_high += msg[1];
      else
        sum_low += msg[1];
    }
    close(res[0]);
    for (int i = 0; i < 6; i++)
      wait(0);

    if (all_ran && sum_high > sum_low && sum_low >= 0)
      sched_ok = 1;
  }
  assert_test(sched_ok,
              "Bo lap lich scheduler uu tien tien trinh co nice thap");
}

int
main(int argc, char *argv[])
{
  printf("=======================================\n");
  printf("     BO KIEM THU PROCFS (XV6)          \n");
  printf("=======================================\n");

  test_read_file("/proc/uptime", "Doc file /proc/uptime");
  test_read_file("/proc/meminfo", "Doc file /proc/meminfo");
  test_read_file("/proc/processes", "Doc file /proc/processes");
  test_read_file("/proc/1/status", "Doc file /proc/1/status");
  test_read_file("/proc/1/maps", "Doc file /proc/1/maps");
  test_small_buffer();
  test_two_level_lookup();
  test_readdir();
  test_dynamic_proc_listing();
  test_read_only_protection();
  test_inode_cache_leak();
  test_proc_maps();
  test_proc_rtime();
  test_proc_nice();
  printf("=======================================\n");
  exit(0);
}

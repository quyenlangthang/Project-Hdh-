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
  int has_status = 0, has_maps = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (strcmp(de.name, ".") == 0)
      has_dot = 1;
    else if (strcmp(de.name, "..") == 0)
      has_dotdot = 1;
    else if (strcmp(de.name, "status") == 0)
      has_status = 1;
    else if (strcmp(de.name, "maps") == 0)
      has_maps = 1;
  }
  close(fd);

  int pid1_ok = has_dot && has_dotdot && has_status && has_maps;
  assert_test(pid1_ok, "Duyet thu muc /proc/1 (., .., status, maps)");
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

// Kiểm thử rò rỉ Inode cache (NINODE = 50, lặp 150 lần mở và đóng)
void
test_inode_cache_leak(void)
{
  int ok = 1;
  for (int i = 0; i < 150; i++) {
    int fd1 = open("/proc/uptime", O_RDONLY);
    int fd2 = open("/proc/meminfo", O_RDONLY);
    int fd3 = open("/proc/1/status", O_RDONLY);
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
      ok = 0;
      if (fd1 >= 0)
        close(fd1);
      if (fd2 >= 0)
        close(fd2);
      if (fd3 >= 0)
        close(fd3);
      break;
    }
    close(fd1);
    close(fd2);
    close(fd3);
  }
  assert_test(ok, "Khong ro ri Inode cache (lap 150 lan mo/dong file)");
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
  printf("=======================================\n");
  exit(0);
}

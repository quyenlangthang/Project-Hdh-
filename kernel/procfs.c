#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "memlayout.h"
#include "proc.h"

// Quy ước virtual_inum trong ProcFS
#define PROCFS_ROOT_INUM     9000
#define PROCFS_UPTIME_INUM   9001
#define PROCFS_MEMINFO_INUM  9002
#define PROCFS_PROC_DIR_BASE 9100
#define PROCFS_STATUS_BASE   9200
#define PROCFS_MAPS_BASE     9300

extern uint ticks;
extern struct spinlock tickslock;
extern struct proc proc[NPROC];

// Kiểm tra xem đường dẫn có thuộc hệ thống tệp ProcFS hay không
int
is_procfs_path(const char *path)
{
  if (path == 0)
    return 0;
  if (path[0] == '/') {
    return (strncmp(path, "/proc", 5) == 0 &&
            (path[5] == '\0' || path[5] == '/'));
  }
  return (strncmp(path, "proc", 4) == 0 && (path[4] == '\0' || path[4] == '/'));
}

// So sánh 2 chuỗi ký tự
static int
procfs_streq(const char *s1, const char *s2)
{
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return (uchar)*s1 - (uchar)*s2;
}

// Chuyển đổi số nguyên không âm (64-bit) sang chuỗi ASCII
static int
procfs_itoa(char *buf, uint64 n)
{
  char tmp[24];
  int i = 0, j = 0;

  if (n == 0) {
    buf[j++] = '0';
    buf[j] = '\0';
    return j;
  }

  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }

  while (i > 0) {
    buf[j++] = tmp[--i];
  }
  buf[j] = '\0';
  return j;
}

static int
is_all_digits(const char *s)
{
  if (*s == 0)
    return 0;
  while (*s) {
    if (*s < '0' || *s > '9')
      return 0;
    s++;
  }
  return 1;
}

static int
proc_atoi(const char *s)
{
  int n = 0;
  while (*s >= '0' && *s <= '9') {
    n = n * 10 + (*s - '0');
    s++;
  }
  return n;
}

// Tìm vị trí process trong mảng proc[] theo pid. Trả về idx hoặc -1 nếu không tồn tại.
static int
proc_find_idx_by_pid(int pid)
{
  if (pid <= 0)
    return -1;
  for (int i = 0; i < NPROC; i++) {
    acquire(&proc[i].lock);
    if (proc[i].state != UNUSED && proc[i].pid == pid) {
      release(&proc[i].lock);
      return i;
    }
    release(&proc[i].lock);
  }
  return -1;
}

// Cấp phát & Khởi tạo Inode trên RAM qua iget(ROOTDEV, virtual_inum)
static struct inode *
procfs_get_inode(uint inum)
{
  struct inode *ip = iget(ROOTDEV, inum);
  if (ip == 0)
    return 0;
  ip->type = T_PROC;
  ip->valid = 1;
  ip->nlink = 1;
  ip->size = 0;
  return ip;
}

// Phân giải chính xác cây thư mục ảo 2 cấp
struct inode *
procfs_lookup(char *path)
{
  char name1[DIRSIZ];
  char name2[DIRSIZ];
  char *p = path;

  // Bước 1: Lấy thành phần đầu tiên, bắt buộc phải là "proc"
  p = skipelem(p, name1);
  if (p == 0 || procfs_streq(name1, "proc") != 0)
    return 0;

  // Nếu đường dẫn dừng sau "proc" (e.g. "/proc", "proc", "/proc/")
  if (*p == '\0') {
    return procfs_get_inode(PROCFS_ROOT_INUM);
  }

  // Bước 2: Lấy thành phần thứ 2 (cấp 1)
  p = skipelem(p, name1);
  if (p == 0)
    return procfs_get_inode(PROCFS_ROOT_INUM);

  // Nếu kết thúc sau thành phần thứ 2 (e.g. "/proc/uptime", "/proc/1", "/proc/.")
  if (*p == '\0') {
    if (procfs_streq(name1, ".") == 0)
      return procfs_get_inode(PROCFS_ROOT_INUM);
    if (procfs_streq(name1, "..") == 0)
      return iget(ROOTDEV, ROOTINO);
    if (procfs_streq(name1, "uptime") == 0)
      return procfs_get_inode(PROCFS_UPTIME_INUM);
    if (procfs_streq(name1, "meminfo") == 0)
      return procfs_get_inode(PROCFS_MEMINFO_INUM);
    if (is_all_digits(name1)) {
      int pid = proc_atoi(name1);
      int idx = proc_find_idx_by_pid(pid);
      if (idx >= 0)
        return procfs_get_inode(PROCFS_PROC_DIR_BASE + idx);
    }
    return 0;
  }

  // Bước 3: Lấy thành phần thứ 3 (cấp 2)
  p = skipelem(p, name2);
  if (p == 0)
    return 0;

  // Nếu vẫn còn thành phần tiếp theo (vượt quá độ sâu 2 cấp của ProcFS)
  if (*p != '\0')
    return 0;

  // Nhánh A: Thành phần cấp 1 là PID
  if (is_all_digits(name1)) {
    int pid = proc_atoi(name1);
    int idx = proc_find_idx_by_pid(pid);
    if (idx < 0)
      return 0;

    if (procfs_streq(name2, ".") == 0)
      return procfs_get_inode(PROCFS_PROC_DIR_BASE + idx);
    if (procfs_streq(name2, "..") == 0)
      return procfs_get_inode(PROCFS_ROOT_INUM);
    if (procfs_streq(name2, "status") == 0)
      return procfs_get_inode(PROCFS_STATUS_BASE + idx);
    if (procfs_streq(name2, "maps") == 0)
      return procfs_get_inode(PROCFS_MAPS_BASE + idx);
    return 0;
  }

  // Nhánh B: Thành phần cấp 1 là "." (e.g. /proc/./uptime)
  if (procfs_streq(name1, ".") == 0) {
    if (procfs_streq(name2, ".") == 0)
      return procfs_get_inode(PROCFS_ROOT_INUM);
    if (procfs_streq(name2, "..") == 0)
      return iget(ROOTDEV, ROOTINO);
    if (procfs_streq(name2, "uptime") == 0)
      return procfs_get_inode(PROCFS_UPTIME_INUM);
    if (procfs_streq(name2, "meminfo") == 0)
      return procfs_get_inode(PROCFS_MEMINFO_INUM);
    if (is_all_digits(name2)) {
      int pid = proc_atoi(name2);
      int idx = proc_find_idx_by_pid(pid);
      if (idx >= 0)
        return procfs_get_inode(PROCFS_PROC_DIR_BASE + idx);
    }
    return 0;
  }

  return 0;
}

// Sinh từng dirent cho thư mục gốc /proc
static int
procfs_get_dirent_root(int idx, struct dirent *de)
{
  memset(de, 0, sizeof(*de));
  if (idx == 0) {
    de->inum = PROCFS_ROOT_INUM;
    safestrcpy(de->name, ".", sizeof(de->name));
    return 1;
  }
  if (idx == 1) {
    de->inum = ROOTINO;
    safestrcpy(de->name, "..", sizeof(de->name));
    return 1;
  }
  if (idx == 2) {
    de->inum = PROCFS_UPTIME_INUM;
    safestrcpy(de->name, "uptime", sizeof(de->name));
    return 1;
  }
  if (idx == 3) {
    de->inum = PROCFS_MEMINFO_INUM;
    safestrcpy(de->name, "meminfo", sizeof(de->name));
    return 1;
  }

  // Quét mảng proc[] sinh các thư mục số mang tên PID đang hoạt động
  int target = idx - 4;
  int current = 0;
  for (int i = 0; i < NPROC; i++) {
    acquire(&proc[i].lock);
    if (proc[i].state != UNUSED) {
      if (current == target) {
        de->inum = PROCFS_PROC_DIR_BASE + i;
        procfs_itoa(de->name, proc[i].pid);
        release(&proc[i].lock);
        return 1;
      }
      current++;
    }
    release(&proc[i].lock);
  }
  return 0;
}

// Duyệt thư mục ảo /proc
static int
procfs_readdir_root(int user_dst, uint64 dst, uint off, uint n)
{
  struct dirent de;
  uint total = 0;

  while (n > 0) {
    int entry_idx = (off + total) / sizeof(struct dirent);
    int de_off = (off + total) % sizeof(struct dirent);

    if (!procfs_get_dirent_root(entry_idx, &de))
      break;

    uint avail = sizeof(struct dirent) - de_off;
    uint m = (n < avail) ? n : avail;

    if (either_copyout(user_dst, dst + total, (char *)&de + de_off, m) == -1)
      return -1;

    total += m;
    n -= m;
  }

  return total;
}

// Sinh từng dirent cho thư mục tiến trình /proc/<pid>
static int
procfs_get_dirent_pid(int proc_idx, int idx, struct dirent *de)
{
  memset(de, 0, sizeof(*de));
  if (idx == 0) {
    de->inum = PROCFS_PROC_DIR_BASE + proc_idx;
    safestrcpy(de->name, ".", sizeof(de->name));
    return 1;
  }
  if (idx == 1) {
    de->inum = PROCFS_ROOT_INUM;
    safestrcpy(de->name, "..", sizeof(de->name));
    return 1;
  }
  if (idx == 2) {
    de->inum = PROCFS_STATUS_BASE + proc_idx;
    safestrcpy(de->name, "status", sizeof(de->name));
    return 1;
  }
  if (idx == 3) {
    de->inum = PROCFS_MAPS_BASE + proc_idx;
    safestrcpy(de->name, "maps", sizeof(de->name));
    return 1;
  }
  return 0;
}

// Duyệt thư mục ảo /proc/<pid>
static int
procfs_readdir_pid(int proc_idx, int user_dst, uint64 dst, uint off, uint n)
{
  struct dirent de;
  uint total = 0;

  while (n > 0) {
    int entry_idx = (off + total) / sizeof(struct dirent);
    int de_off = (off + total) % sizeof(struct dirent);

    if (!procfs_get_dirent_pid(proc_idx, entry_idx, &de))
      break;

    uint avail = sizeof(struct dirent) - de_off;
    uint m = (n < avail) ? n : avail;

    if (either_copyout(user_dst, dst + total, (char *)&de + de_off, m) == -1)
      return -1;

    total += m;
    n -= m;
  }

  return total;
}

// Đọc /proc/uptime
static int
procfs_read_uptime(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[32];

  acquire(&tickslock);
  uint cur_ticks = ticks;
  release(&tickslock);

  int len = procfs_itoa(buf, cur_ticks);
  buf[len++] = '\n';
  buf[len] = '\0';

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

// Đọc /proc/meminfo
static int
procfs_read_meminfo(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[128];
  uint64 total_b = PHYSTOP - KERNBASE;
  uint64 free_b = freemem();
  int len = 0;

  // Ghi MemTotal
  char *str1 = "MemTotal: ";
  while (*str1)
    buf[len++] = *str1++;
  len += procfs_itoa(buf + len, total_b);
  buf[len++] = ' ';
  buf[len++] = 'B';
  buf[len++] = '\n';

  // Ghi MemFree
  char *str2 = "MemFree: ";
  while (*str2)
    buf[len++] = *str2++;
  len += procfs_itoa(buf + len, free_b);
  buf[len++] = ' ';
  buf[len++] = 'B';
  buf[len++] = '\n';
  buf[len] = '\0';

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

// Đọc /proc/<pid>/status
static int
procfs_read_status(int idx, int user_dst, uint64 dst, uint off, uint n)
{
  if (idx < 0 || idx >= NPROC)
    return -1;

  acquire(&proc[idx].lock);
  if (proc[idx].state == UNUSED) {
    release(&proc[idx].lock);
    return 0;
  }
  int pid = proc[idx].pid;
  release(&proc[idx].lock);

  char buf[256];
  int len = proc_get_status_str(pid, buf, sizeof(buf));
  if (len < 0)
    return 0;

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

// Đọc /proc/<pid>/maps
static int
procfs_read_maps(int idx, int user_dst, uint64 dst, uint off, uint n)
{
  if (idx < 0 || idx >= NPROC)
    return -1;

  acquire(&proc[idx].lock);
  if (proc[idx].state == UNUSED) {
    release(&proc[idx].lock);
    return 0;
  }
  uint64 sz = proc[idx].sz;
  release(&proc[idx].lock);

  char buf[64];
  int len = 0;
  char *prefix = "0x0-0x";
  while (*prefix)
    buf[len++] = *prefix++;
  len += procfs_itoa(buf + len, sz);
  char *suffix = " rwxp\n";
  while (*suffix)
    buf[len++] = *suffix++;
  buf[len] = '\0';

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

// Hàm điều phối đọc chính của hệ thống tệp ảo ProcFS
int
procfs_read(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if (ip->inum == PROCFS_ROOT_INUM) {
    return procfs_readdir_root(user_dst, dst, off, n);
  }
  if (ip->inum == PROCFS_UPTIME_INUM) {
    return procfs_read_uptime(user_dst, dst, off, n);
  }
  if (ip->inum == PROCFS_MEMINFO_INUM) {
    return procfs_read_meminfo(user_dst, dst, off, n);
  }
  if (ip->inum >= PROCFS_PROC_DIR_BASE &&
      ip->inum < PROCFS_PROC_DIR_BASE + NPROC) {
    int idx = ip->inum - PROCFS_PROC_DIR_BASE;
    return procfs_readdir_pid(idx, user_dst, dst, off, n);
  }
  if (ip->inum >= PROCFS_STATUS_BASE && ip->inum < PROCFS_STATUS_BASE + NPROC) {
    int idx = ip->inum - PROCFS_STATUS_BASE;
    return procfs_read_status(idx, user_dst, dst, off, n);
  }
  if (ip->inum >= PROCFS_MAPS_BASE && ip->inum < PROCFS_MAPS_BASE + NPROC) {
    int idx = ip->inum - PROCFS_MAPS_BASE;
    return procfs_read_maps(idx, user_dst, dst, off, n);
  }
  return -1;
}

#include <ncursesw/ncurses.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>

typedef struct {
  unsigned long long total;
  unsigned long long idle;
} CpuStats;

typedef struct {
  unsigned long long total;
  unsigned long long available;
  unsigned long long swap_total;
  unsigned long long swap_free;
} MemStats;

typedef struct {
  unsigned long long rx_bytes;
  unsigned long long tx_bytes;
} NetStats;

typedef struct {
  int pid;
  char name[256];
  char state;
  unsigned long long rss_kb;
} ProcessInfo;

int compare_processes(const void *a, const void *b) {
  const ProcessInfo *p1 = (const ProcessInfo *)a;
  const ProcessInfo *p2 = (const ProcessInfo *)b;
  // Sort by RAM (RSS) descending (highest memory first, like btop/top)
  if (p2->rss_kb > p1->rss_kb) return 1;
  if (p2->rss_kb < p1->rss_kb) return -1;
  return 0;
}

int read_cpu(CpuStats *stats) {
  FILE *file = fopen("/proc/stat", "r");
  if (!file) return 0;
  unsigned long long v[8];
  int count = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
  fclose(file);
  if (count != 8) return 0;
  stats->total = 0;
  for (int i = 0; i < 8; i++) stats->total += v[i];
  stats->idle = v[3] + v[4];
  return 1;
}

int read_memory(MemStats *stats) {
  FILE *file = fopen("/proc/meminfo", "r");
  if (!file) return 0;
  char line[256];
  int found_total = 0, found_available = 0;
  stats->swap_total = 0;
  stats->swap_free = 0;

  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, "MemTotal: %llu kB", &stats->total) == 1) found_total = 1;
    if (sscanf(line, "MemAvailable: %llu kB", &stats->available) == 1) found_available = 1;
    sscanf(line, "SwapTotal: %llu kB", &stats->swap_total);
    sscanf(line, "SwapFree: %llu kB", &stats->swap_free);
  }
  fclose(file);
  return found_total && found_available;
}

int read_network(NetStats *stats) {
  FILE *file = fopen("/proc/net/dev", "r");
  if (!file) return 0;
  char line[256];
  stats->rx_bytes = 0;
  stats->tx_bytes = 0;

  if (!fgets(line, sizeof(line), file) || !fgets(line, sizeof(line), file)) {
    fclose(file);
    return 0;
  }

  while (fgets(line, sizeof(line), file)) {
    char iface[32];
    unsigned long long rx, tx, dummy;
    if (sscanf(line, " %31[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu",
               iface, &rx, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &tx) >= 10) {
      if (strcmp(iface, "lo") != 0) {
        stats->rx_bytes += rx;
        stats->tx_bytes += tx;
      }
    }
  }
  fclose(file);
  return 1;
}

int get_all_processes(ProcessInfo *procs, int max_procs) {
  DIR *dir = opendir("/proc");
  if (!dir) return 0;
  struct dirent *entry;
  int count = 0;

  while ((entry = readdir(dir)) != NULL && count < max_procs) {
    if (entry->d_name[0] >= '1' && entry->d_name[0] <= '9') {
      int pid = atoi(entry->d_name);
      if (pid > 0) {
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (f) {
          char comm[256];
          char state;
          int p;
          if (fscanf(f, "%d (%[^)]) %c", &p, comm, &state) == 3) {
            procs[count].pid = pid;
            snprintf(procs[count].name, sizeof(procs[count].name), "%s", comm);
            procs[count].state = state;
            procs[count].rss_kb = 0;

            char status_path[256];
            snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
            FILE *sf = fopen(status_path, "r");
            if (sf) {
              char sline[256];
              while (fgets(sline, sizeof(sline), sf)) {
                if (strncmp(sline, "VmRSS:", 6) == 0) {
                  unsigned long long rss = 0;
                  if (sscanf(sline + 6, "%llu", &rss) == 1) procs[count].rss_kb = rss;
                  break;
                }
              }
              fclose(sf);
            }
            count++;
          }
          fclose(f);
        }
      }
    }
  }
  closedir(dir);
  return count;
}

void get_cpu_details(char *name_buf, size_t name_sz, int *cores, char *arch_buf, size_t arch_sz) {
  snprintf(name_buf, name_sz, "Unknown CPU");
  *cores = 1;
  snprintf(arch_buf, arch_sz, "Unknown");

  struct utsname buffer;
  if (uname(&buffer) == 0) snprintf(arch_buf, arch_sz, "%s", buffer.machine);

  FILE *file = fopen("/proc/cpuinfo", "r");
  if (!file) return;

  char line[256];
  int core_count = 0;
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, "model name", 10) == 0) {
      char *p = strchr(line, ':');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        p[strcspn(p, "\r\n")] = 0;
        snprintf(name_buf, name_sz, "%s", p);
      }
    }
    if (strncmp(line, "processor", 9) == 0) core_count++;
  }
  fclose(file);
  if (core_count > 0) *cores = core_count;
}

void get_network_details(char *iface_buf, size_t iface_sz, char *ip_buf, size_t ip_sz, char *driver_buf, size_t driver_sz) {
  snprintf(iface_buf, iface_sz, "Unknown");
  snprintf(ip_buf, ip_sz, "N/A");
  snprintf(driver_buf, driver_sz, "Unknown Driver");

  struct ifaddrs *ifaddr, *ifa;
  if (getifaddrs(&ifaddr) == -1) return;

  int found = 0;
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;
    if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, "lo") != 0) {
      snprintf(iface_buf, iface_sz, "%s", ifa->ifa_name);
      struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
      char ip[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &(sa->sin_addr), ip, sizeof(ip)) != NULL) {
        snprintf(ip_buf, ip_sz, "%s", ip);
      }
      found = 1;
      break;
    }
  }

  if (!found) {
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_name && strcmp(ifa->ifa_name, "lo") != 0) {
        snprintf(iface_buf, iface_sz, "%s", ifa->ifa_name);
        found = 1;
        break;
      }
    }
  }
  freeifaddrs(ifaddr);

  if (found && strcmp(iface_buf, "Unknown") != 0) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/driver", iface_buf);
    char link_target[256];
    ssize_t len = readlink(path, link_target, sizeof(link_target) - 1);
    if (len != -1) {
      link_target[len] = '\0';
      char *base = strrchr(link_target, '/');
      snprintf(driver_buf, driver_sz, "%s", base ? base + 1 : link_target);
    } else {
      snprintf(path, sizeof(path), "/sys/class/net/%s/type", iface_buf);
      FILE *f = fopen(path, "r");
      if (f) {
        int type = 0;
        if (fscanf(f, "%d", &type) == 1) {
          if (type == 1) snprintf(driver_buf, driver_sz, "Ethernet");
          else if (type == 512 || type == 772) snprintf(driver_buf, driver_sz, "Wireless");
          else snprintf(driver_buf, driver_sz, "Generic");
        }
        fclose(f);
      }
    }
  }
}

void format_speed(double bytes_per_sec, char *buffer, size_t size) {
  if (bytes_per_sec >= 1024.0 * 1024.0) {
    snprintf(buffer, size, "%.2f MB/s", bytes_per_sec / (1024.0 * 1024.0));
  } else {
    snprintf(buffer, size, "%.2f KB/s", bytes_per_sec / 1024.0);
  }
}

void format_memory(unsigned long long kb, char *buffer, size_t size) {
  if (kb >= 1024 * 1024) {
    snprintf(buffer, size, "%.1f GB", (double)kb / (1024.0 * 1024.0));
  } else if (kb >= 1024) {
    snprintf(buffer, size, "%.1f MB", (double)kb / 1024.0);
  } else {
    snprintf(buffer, size, "%llu KB", kb);
  }
}

void draw_bar(int y, int x, int width, double percentage) {
  int filled = (int)(width * percentage / 100.0);
  if (filled > width) filled = width;
  if (filled < 0) filled = 0;

  for (int i = 0; i < width; i++) {
    if (i < filled) {
      attron(COLOR_PAIR(1));
      mvaddwstr(y, x + i, L"󰝤");
      attroff(COLOR_PAIR(1));
    } else {
      attron(COLOR_PAIR(2));
      mvaddwstr(y, x + i, L"󰝤");
      attroff(COLOR_PAIR(2));
    }
  }
}

void draw_box(int y, int x, int width, int height, int is_selected) {
  int color = is_selected ? 6 : 5;
  attron(COLOR_PAIR(color));
  mvaddch(y, x, ACS_ULCORNER);
  mvaddch(y, x + width - 1, ACS_URCORNER);
  mvaddch(y + height - 1, x, ACS_LLCORNER);
  mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
  mvhline(y, x + 1, ACS_HLINE, width - 2);
  mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
  mvvline(y + 1, x, ACS_VLINE, height - 2);
  mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
  attroff(COLOR_PAIR(color));
}

int main(void) {
  setlocale(LC_ALL, "");
  initscr();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  timeout(1000);

  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_MAGENTA, -1);
    init_pair(4, COLOR_YELLOW, -1);
    init_pair(5, COLOR_BLUE, -1);
    init_pair(6, COLOR_CYAN, -1);
  }

  CpuStats prev_cpu = {0}, curr_cpu = {0};
  MemStats memory = {0};
  NetStats prev_net = {0}, curr_net = {0};

  char cpu_name[128], cpu_arch[32];
  int cpu_cores = 1;
  get_cpu_details(cpu_name, sizeof(cpu_name), &cpu_cores, cpu_arch, sizeof(cpu_arch));

  int selected_box = 0;
  int cpu_expanded = 0;
  int ram_expanded = 0;
  int net_expanded = 0;
  int proc_active = 0;
  int proc_scroll_offset = 0;

  if (!read_cpu(&prev_cpu) || !read_network(&prev_net)) {
    endwin();
    fprintf(stderr, "Could not initialize system readings.\n");
    return 1;
  }

  ProcessInfo procs[2048];
  int proc_count = get_all_processes(procs, 2048);
  qsort(procs, proc_count, sizeof(ProcessInfo), compare_processes);

  while (1) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int ch = getch();

    if (ch == 'q' || ch == 'Q') {
      break;
    } else if (proc_active) {
      if (ch == 27) { 
        proc_active = 0;
      } else if (ch == 'j' || ch == KEY_DOWN) {
        int cpu_h = cpu_expanded ? 6 : 5;
        int ram_h = 7;
        int net_h = net_expanded ? 6 : 5;
        int proc_y_calc = 3 + cpu_h + 1 + ram_h + 1 + net_h + 1;
        int proc_height_calc = rows - proc_y_calc - 2;
        if (proc_height_calc < 6) proc_height_calc = 6;
        int visible_rows = proc_height_calc - 4;
        if (visible_rows < 1) visible_rows = 1;
        if (proc_scroll_offset < proc_count - visible_rows) proc_scroll_offset++;
      } else if (ch == 'k' || ch == KEY_UP) {
        if (proc_scroll_offset > 0) proc_scroll_offset--;
      }
    } else {
      if (ch == KEY_DOWN || ch == 'j' || ch == '\t') {
        selected_box = (selected_box + 1) % 4;
      } else if (ch == KEY_UP || ch == 'k') {
        selected_box = (selected_box + 3) % 4;
      } else if (ch == '\n' || ch == KEY_ENTER || ch == 13 || ch == 10) {
        if (selected_box == 0) cpu_expanded = !cpu_expanded;
        else if (selected_box == 1) ram_expanded = !ram_expanded;
        else if (selected_box == 2) net_expanded = !net_expanded;
        else if (selected_box == 3) {
          proc_active = 1;
          proc_scroll_offset = 0;
        }
      }
    }

    if (!read_cpu(&curr_cpu) || !read_memory(&memory) || !read_network(&curr_net)) continue;
    proc_count = get_all_processes(procs, 2048);
    qsort(procs, proc_count, sizeof(ProcessInfo), compare_processes);

    unsigned long long total_delta = curr_cpu.total - prev_cpu.total;
    unsigned long long idle_delta = curr_cpu.idle - prev_cpu.idle;
    double cpu_usage = total_delta > 0 ? 100.0 * (total_delta - idle_delta) / total_delta : 0.0;

    double ram_used = (double)(memory.total - memory.available);
    double ram_usage = 100.0 * ram_used / memory.total;
    unsigned long long swap_used = memory.swap_total > memory.swap_free ? (memory.swap_total - memory.swap_free) : 0;
    double swap_usage = memory.swap_total > 0 ? (100.0 * swap_used / memory.swap_total) : 0.0;

    double rx_speed = (double)(curr_net.rx_bytes - prev_net.rx_bytes);
    double tx_speed = (double)(curr_net.tx_bytes - prev_net.tx_bytes);
    char down_str[32], up_str[32];
    format_speed(rx_speed, down_str, sizeof(down_str));
    format_speed(tx_speed, up_str, sizeof(up_str));

    char net_iface[32], net_ip[64], net_driver[128];
    get_network_details(net_iface, sizeof(net_iface), net_ip, sizeof(net_ip), net_driver, sizeof(net_driver));

    erase();

    if (rows < 32 || cols < 65) {
      mvprintw(0, 0, "Terminal too small (need at least 65x32).");
      refresh();
      prev_cpu = curr_cpu;
      prev_net = curr_net;
      continue;
    }

    int bar_width = cols - 8;
    int bar_x = 4;

    attron(COLOR_PAIR(3));
    if (proc_active) {
      mvprintw(1, 3, "CTOP [Processes Active: Use Up/Down or j/k to scroll, Press Esc to exit]");
    } else {
      mvprintw(1, 3, "CTOP");
    }
    attroff(COLOR_PAIR(3));

    int cpu_height = cpu_expanded ? 6 : 5;
    int cpu_y = 3;
    int ram_height = 7;
    int ram_y = cpu_y + cpu_height + 1;
    int net_height = net_expanded ? 6 : 5;
    int net_y = ram_y + ram_height + 1;
    int proc_y = net_y + net_height + 1;
    int proc_height = rows - proc_y - 2;
    if (proc_height < 6) proc_height = 6;

    /* CPU box */
    draw_box(cpu_y, 2, cols - 4, cpu_height, selected_box == 0 && !proc_active);
    attron(COLOR_PAIR(4));
    mvprintw(cpu_y + 1, 5, "CPU %s", cpu_expanded ? "[Details]" : "[Bar]");
    attroff(COLOR_PAIR(4));
    if (cpu_expanded) {
      mvprintw(cpu_y + 2, 5, "Name:  %.*s", cols - 14, cpu_name);
      mvprintw(cpu_y + 3, 5, "Cores: %d", cpu_cores);
      mvprintw(cpu_y + 4, 5, "Arch:  %s", cpu_arch);
    } else {
      mvprintw(cpu_y + 2, 5, "Used Percentage: %5.1f%%", cpu_usage);
      draw_bar(cpu_y + 3, bar_x, bar_width, cpu_usage);
    }

    /* RAM box */
    draw_box(ram_y, 2, cols - 4, ram_height, selected_box == 1 && !proc_active);
    attron(COLOR_PAIR(4));
    mvprintw(ram_y + 1, 5, "RAM %s", ram_expanded ? "[Swap View]" : "[Bar]");
    attroff(COLOR_PAIR(4));
    if (ram_expanded) {
      mvprintw(ram_y + 2, 5, "Swap Usage:    %5.1f%%", swap_usage);
      mvprintw(ram_y + 3, 5, "Swap Used:     %.0f MiB", (double)swap_used / 1024.0);
      mvprintw(ram_y + 4, 5, "Swap Total:    %.0f MiB", (double)memory.swap_total / 1024.0);
      draw_bar(ram_y + 5, bar_x, bar_width, swap_usage);
    } else {
      mvprintw(ram_y + 2, 5, "Used Percentage: %5.1f%%", ram_usage);
      mvprintw(ram_y + 3, 5, "Used:  %.0f MiB", ram_used / 1024.0);
      mvprintw(ram_y + 4, 5, "Total: %.0f MiB", memory.total / 1024.0);
      draw_bar(ram_y + 5, bar_x, bar_width, ram_usage);
    }

    /* NETWORK box */
    draw_box(net_y, 2, cols - 4, net_height, selected_box == 2 && !proc_active);
    attron(COLOR_PAIR(4));
    mvprintw(net_y + 1, 5, "NETWORK %s", net_expanded ? "[Details]" : "[Speeds]");
    attroff(COLOR_PAIR(4));
    if (net_expanded) {
      mvprintw(net_y + 2, 5, "Interface: %s", net_iface);
      mvprintw(net_y + 3, 5, "IP:        %s", net_ip);
      mvprintw(net_y + 4, 5, "Driver:    %.*s", cols - 16, net_driver);
    } else {
      mvprintw(net_y + 2, 5, "Download:  %-12s", down_str);
      mvprintw(net_y + 3, 5, "Upload:    %-12s", up_str);
    }

    /* PROCESSES box */
    draw_box(proc_y, 2, cols - 4, proc_height, selected_box == 3);
    attron(COLOR_PAIR(4));
    if (proc_active) {
      mvprintw(proc_y + 1, 5, "PROCESSES [Active]");
    } else {
      mvprintw(proc_y + 1, 5, "PROCESSES [Press Enter to Scroll]");
    }
    attroff(COLOR_PAIR(4));
    
    int name_width = cols - 38;
    if (name_width < 15) name_width = 15;
    
    char header_fmt[64];
    snprintf(header_fmt, sizeof(header_fmt), "%%-8s  %%-%d.%ds  %%-6s  %%-10s", name_width, name_width);
    mvprintw(proc_y + 2, 5, header_fmt, "PID", "NAME", "STATE", "RAM");

    int visible_rows = proc_height - 4;
    if (visible_rows < 1) visible_rows = 1;

    char row_fmt[64];
    snprintf(row_fmt, sizeof(row_fmt), "%%-8d  %%-%d.%ds  %%-6c  %%-10s", name_width, name_width);

    for (int i = 0; i < visible_rows; i++) {
      int idx = proc_scroll_offset + i;
      if (idx < proc_count) {
        char ram_str[32];
        format_memory(procs[idx].rss_kb, ram_str, sizeof(ram_str));
        mvprintw(proc_y + 3 + i, 5, row_fmt, 
                 procs[idx].pid, procs[idx].name, procs[idx].state, ram_str);
      }
    }

    /* Footer */
    attron(A_BOLD);
    mvprintw(rows - 2, 3, "Q");
    attroff(A_BOLD);
    printw("  Quit");

    refresh();
    prev_cpu = curr_cpu;
    prev_net = curr_net;
  }

  endwin();
  return 0;
}

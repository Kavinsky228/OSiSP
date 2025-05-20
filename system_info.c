#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    
#include <unistd.h>
#include <sys/statvfs.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/types.h>

struct opts {
    int os, user, shell, pkgs, res, uptime, load, cpu, mem, disk, net;
    int color;
    short color_value;
    char unit[4];  // B, KB, MB, GB, TB
} options;

short parse_color(const char* name) {
    if (!strcasecmp(name, "black"))   return COLOR_BLACK;
    if (!strcasecmp(name, "red"))     return COLOR_RED;
    if (!strcasecmp(name, "green"))   return COLOR_GREEN;
    if (!strcasecmp(name, "yellow"))  return COLOR_YELLOW;
    if (!strcasecmp(name, "blue"))    return COLOR_BLUE;
    if (!strcasecmp(name, "magenta")) return COLOR_MAGENTA;
    if (!strcasecmp(name, "cyan"))    return COLOR_CYAN;
    if (!strcasecmp(name, "white"))   return COLOR_WHITE;
    return -1;
}

const char* format_size(unsigned long long bytes, const char* unit) {
    static char buf[64];
    double val = (double)bytes;
    if (!strcmp(unit, "KB")) val /= 1024.0;
    else if (!strcmp(unit, "MB")) val /= (1024.0 * 1024);
    else if (!strcmp(unit, "GB")) val /= (1024.0 * 1024 * 1024);
    else if (!strcmp(unit, "TB")) val /= (1024.0 * 1024 * 1024 * 1024);
    snprintf(buf, sizeof(buf), "%.2f %s", val, unit);
    return buf;
}

char* get_system_info(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return NULL;
    static char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    if (fgets(buffer, sizeof(buffer), pipe) == NULL) {
        pclose(pipe);
        return NULL;
    }
    pclose(pipe);
    buffer[strcspn(buffer, "\n")] = '\0';
    return buffer;
}

void get_cpu_info(char* cpu_info, size_t size) {
    FILE* file = fopen("/proc/cpuinfo", "r");
    if (!file) {
        snprintf(cpu_info, size, "Error opening /proc/cpuinfo");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "model name", 10) == 0) {
            char* colon = strchr(line, ':');
            if (colon) {
                snprintf(cpu_info, size, "%s", colon + 2);
                fclose(file);
                return;
            }
        }
    }
    fclose(file);
    snprintf(cpu_info, size, "CPU info not found");
}

void get_memory_info(char* memory_info, size_t size) {
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) {
        snprintf(memory_info, size, "Error opening /proc/meminfo");
        return;
    }
    char line[256]; unsigned long total = 0, available = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) break;
    }
    fclose(file);
    unsigned long used = total - available;
    snprintf(memory_info, size,
             "Used: %s / Available: %s",
             format_size((unsigned long long)used * 1024, options.unit),
             format_size((unsigned long long)available * 1024, options.unit));
}

void get_disk_info(char* disk_info, size_t size) {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        snprintf(disk_info, size, "Error getting disk info");
        return;
    }
    unsigned long long total = stat.f_blocks * stat.f_frsize;
    unsigned long long free  = stat.f_bfree  * stat.f_frsize;
    snprintf(disk_info, size,
             "Total: %s, Free: %s",
             format_size(total, options.unit),
             format_size(free, options.unit));
}

void get_uptime_info(char* uptime_info, size_t size) {
    FILE* file = fopen("/proc/uptime", "r");
    if (!file) {
        snprintf(uptime_info, size, "Error opening /proc/uptime");
        return;
    }
    double sec = 0.0;
    if (fscanf(file, "%lf", &sec) != 1) sec = 0.0;
    fclose(file);
    int days = sec / 86400;
    int hrs  = ((int)sec % 86400) / 3600;
    int mins = ((int)sec % 3600) / 60;
    int secs = (int)sec % 60;
    snprintf(uptime_info, size, "%d days, %02d:%02d:%02d",
             days, hrs, mins, secs);
}

void get_load_info(char* load_info, size_t size) {
    FILE* file = fopen("/proc/loadavg", "r");
    if (!file) {
        snprintf(load_info, size, "Error opening /proc/loadavg");
        return;
    }
    double l1=0, l5=0, l15=0;
    if (fscanf(file, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        l1 = l5 = l15 = 0;
    }
    fclose(file);
    snprintf(load_info, size, "1min: %.2f, 5min: %.2f, 15min: %.2f",
             l1, l5, l15);
}

void get_user_host_info(char* user_host, size_t size) {
    const char* user = getlogin();
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "unknown", sizeof(hostname));
    }
    snprintf(user_host, size, "User: %s, Host: %s",
             user ? user : "unknown", hostname);
}

void get_network_info(char* net_info, size_t size) {
    FILE* pipe = popen("ip -brief address | sed '/^$/d'", "r");
    if (!pipe) {
        snprintf(net_info, size, "Error getting network info");
        return;
    }
    char line[256];
    net_info[0] = '\0';
    while (fgets(line, sizeof(line), pipe)) {
        if (strlen(line) > 1) {
            strncat(net_info, line, size - strlen(net_info) - 1);
        }
    }
    pclose(pipe);
}

void get_shell_info(char* shell_info, size_t size) {
    const char* shell = getenv("SHELL");
    snprintf(shell_info, size, "Shell: %s",
             shell ? shell : "unknown");
}

void get_packages_info(char* pkg_info, size_t size) {
    unsigned long count = 0;
    FILE* pipe = popen("dpkg -l 2>/dev/null | wc -l", "r");
    if (pipe) {
        if (fscanf(pipe, "%lu", &count) != 1) count = 0;
        pclose(pipe);
    }
    if (count > 0) {
        snprintf(pkg_info, size, "Packages (dpkg): %lu", count);
    } else {
        pipe = popen("rpm -qa 2>/dev/null | wc -l", "r");
        if (pipe) {
            if (fscanf(pipe, "%lu", &count) != 1) count = 0;
            pclose(pipe);
        }
        snprintf(pkg_info, size, "Packages (rpm): %lu", count);
    }
}

void get_resolution_info(char* res_info, size_t size) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    snprintf(res_info, size, "Terminal: %d cols x %d rows", cols, rows);
}

void draw_screen() {
    char os_info[256] = {0}, cpu_info[256] = {0}, memory_info[256] = {0};
    char disk_info[256] = {0}, uptime_info[256] = {0}, load_info[256] = {0};
    char user_host[256] = {0}, net_info[1024] = {0}, shell_info[256] = {0};
    char pkg_info[256] = {0}, res_info[256] = {0};

    if (options.os) {
        char* sys = get_system_info("uname -a");
        if (!sys) sys = "unknown";
        snprintf(os_info, sizeof(os_info), "OS: %s", sys);
    }
    if (options.user)  get_user_host_info(user_host, sizeof(user_host));
    if (options.shell) get_shell_info(shell_info, sizeof(shell_info));
    if (options.pkgs)  get_packages_info(pkg_info, sizeof(pkg_info));
    if (options.res)   get_resolution_info(res_info, sizeof(res_info));
    if (options.uptime) get_uptime_info(uptime_info, sizeof(uptime_info));
    if (options.load)  get_load_info(load_info, sizeof(load_info));
    if (options.cpu)   get_cpu_info(cpu_info, sizeof(cpu_info));
    if (options.mem)   get_memory_info(memory_info, sizeof(memory_info));
    if (options.disk)  get_disk_info(disk_info, sizeof(disk_info));
    if (options.net)   get_network_info(net_info, sizeof(net_info));

    clear();
    int row = 1;
    mvprintw(row++, 2, "System Information (unit=%s)", options.unit);
    mvprintw(row++, 2, "-------------------");
    if (options.os)    mvprintw(row++, 4, "%s", os_info);
    if (options.user)  mvprintw(row++, 4, "%s", user_host);
    if (options.shell) mvprintw(row++, 4, "%s", shell_info);
    if (options.pkgs)  mvprintw(row++, 4, "%s", pkg_info);
    if (options.res)   mvprintw(row++, 4, "%s", res_info);
    if (options.uptime)mvprintw(row++, 4, "Uptime: %s", uptime_info);
    if (options.load)  mvprintw(row++, 4, "Load Average: %s", load_info);
    if (options.cpu)   mvprintw(row++, 4, "CPU: %s", cpu_info);
    if (options.mem)   mvprintw(row++, 4, "%s", memory_info);
    if (options.disk)  mvprintw(row++, 4, "Disk: %s", disk_info);
    if (options.net) {
        mvprintw(row++, 4, "Network Interfaces:");
        char* line = strtok(net_info, "\n");
        while (line) {
            mvprintw(row++, 6, "%s", line);
            line = strtok(NULL, "\n");
        }
    }

    mvprintw(row+1, 2, "Press ':' to refresh, 'q' to exit...");
    refresh();
}

void show_instructions() {
    clear();
    mvprintw(2, 2, "Welcome to the System Info Utility!");
    mvprintw(4, 4, "Instructions:");
    mvprintw(6, 6, "Press '.' to start the utility.");
    mvprintw(7, 6, "Within the utility:");
    mvprintw(9, 8, ":   Refresh the displayed information");
    mvprintw(10, 8, "q   Exit the utility");
    refresh();
}

int main(int argc, char** argv) {

    memset(&options, 0, sizeof(options));
    options.color = 0;
    strcpy(options.unit, "GB");

    int any_flag = 0;

    static struct option long_opts[] = {
        {"all" ,    no_argument,       0, 'a'},
        {"os" ,     no_argument,       0, 'o'},
        {"user",    no_argument,       0, 'u'},
        {"shell",   no_argument,       0, 's'},
        {"pkgs",    no_argument,       0, 'p'},
        {"res",     no_argument,       0, 'r'},
        {"uptime",  no_argument,       0, 't'},
        {"load",    no_argument,       0, 'l'},
        {"cpu",     no_argument,       0, 'c'},
        {"mem",     no_argument,       0, 'm'},
        {"disk",    no_argument,       0, 'd'},
        {"net",     no_argument,       0, 'n'},
        {"color",   required_argument, 0, 'C'},
        {"unit",    required_argument, 0, 'U'},
        {0,0,0,0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "aousprtlcmdnC:U:", long_opts, NULL)) != -1) {
        any_flag = 1;  
        switch(opt) {
            case 'a':
                memset(&options, 1, sizeof(options));
                options.color = 1; 
                break;
            case 'o': options.os = 1; break;
            case 'u': options.user = 1; break;
            case 's': options.shell = 1; break;
            case 'p': options.pkgs = 1; break;
            case 'r': options.res = 1; break;
            case 't': options.uptime = 1; break;
            case 'l': options.load = 1; break;
            case 'c': options.cpu = 1; break;
            case 'm': options.mem = 1; break;
            case 'd': options.disk = 1; break;
            case 'n': options.net = 1; break;
            case 'C': {
                short v = parse_color(optarg);
                if (v >= 0) { options.color = 1; options.color_value = v; }
                break;
            }
            case 'U': {
                char u[4];
                strncpy(u, optarg, 3);
                u[3] = '\0';
                for (size_t i = 0; i < strlen(u); ++i) {
                    u[i] = toupper((unsigned char)u[i]);
                }
                strncpy(options.unit, u, sizeof(options.unit));
                break;
            }
        }
    }

    
    if (!any_flag) {
        memset(&options, 1, sizeof(options));
        options.color = 0;         
        strcpy(options.unit, "GB"); 
    }

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE);
    start_color(); use_default_colors();
    if (options.color) init_pair(1, options.color_value, COLOR_BLACK);
    else               init_pair(1, COLOR_WHITE, COLOR_BLACK);
    attron(COLOR_PAIR(1)); bkgd(COLOR_PAIR(1));

    show_instructions();
    int ch;
    while ((ch = getch()) != '.') { /* ждём '.' */ }
    draw_screen();
    while ((ch = getch()) != 'q') {
        if (ch == ':') draw_screen();
    }
    endwin();
    return 0;
}


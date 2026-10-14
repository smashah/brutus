#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <locale.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#define MIB (1024.0 * 1024.0)
#define GIB (1024.0 * 1024.0 * 1024.0)
#ifndef BRUTUS_VERSION
#define BRUTUS_VERSION "0.1.1"
#endif

typedef struct {
    pid_t pid, parent;
    uid_t uid;
    uint64_t start_sec, start_usec, cpu_ticks, swapin_bytes;
    double resident, footprint, initial_footprint, growth, compressed, memory[3], cpu[3], swapin[3], score;
    int samples, swap_samples, status;
    bool readable, swap_readable, app_main, protected;
    char name[64];
} Process;

typedef struct {
    double time, ram, free_ram, wired, compressor, swap_used, swap_total, free_disk;
    double cpu_user, cpu_system, cpu_idle, paging_in, paging_out, compress, decompress;
    double disk_read_rate, disk_write_rate, disk_read, disk_write, seconds, self_footprint;
    double load[3];
    int pressure, cores, total, yours, sleeping;
    bool disk_known, space_known;
    vm_statistics64_data_t vm;
    host_cpu_load_info_data_t cpu;
} State;

typedef struct { Process *items; size_t count; State state; } Snapshot;
typedef struct { bool dry, force, verbose, json, interactive, no_animation; int count, target_count; const char *action, *targets[3]; } Options;
static uid_t user_uid;
static host_t host;
static double ticks_to_ns;
static volatile sig_atomic_t terminal_active;
static struct termios saved_terminal;
static void end_terminal(void);

static bool colors_enabled(void) {
    const char *term = getenv("TERM");
    return isatty(STDOUT_FILENO) && !getenv("NO_COLOR") && (!term || strcmp(term, "dumb"));
}
static const char *ink(const char *color) { return colors_enabled() ? color : ""; }
#define RESET "\033[0m"
#define MUTED "\033[90m"
#define CYAN "\033[1;36m"
#define BOLD "\033[1m"
#define RED "\033[1;31m"
#define SELECTED "\033[1;30;46m"
#define SELECTOR "🗡️"
static const char *health_ink(int level) {
    const char *colors[] = {"\033[1;32m", "\033[1;33m", "\033[1;31m"};
    return colors[level];
}
static int terminal_rows(void) {
    struct winsize size = {0};
    return ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row ? size.ws_row : 24;
}
static int panel_width(void) {
    struct winsize size = {0};
    int columns = ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col ? size.ws_col : 80;
    return columns > 80 ? 76 : columns > 12 ? columns - 4 : 8;
}
// Decode names and paths as text, never terminal control sequences. Count display
// columns so accented and wide names cannot push a box out of alignment.
static size_t text_character(const char *text, int *columns) {
    wchar_t wc; mbstate_t state = {0};
    size_t length = mbrtowc(&wc, text, MB_CUR_MAX, &state);
    if (length == (size_t)-1 || length == (size_t)-2 || !length) { *columns = 1; return 1; }
    *columns = wcwidth(wc);
    if (*columns < 0) *columns = 1;
    // The dagger's emoji presentation occupies two terminal cells. Consume its
    // variation selector with the base character so padding stays aligned.
    if (wc == 0x1F5E1 && !strncmp(text + length, "\xEF\xB8\x8F", 3)) {
        *columns = 2;
        length += 3;
    }
    return length;
}
static void panel_text(const char *text, int width) {
    int used = 0;
    while (*text) {
        int columns; size_t length = text_character(text, &columns);
        if (used + columns > width || (used + columns == width && text[length])) {
            while (used++ < width - 1) putchar(' ');
            fputs("…", stdout); used = width; break;
        }
        if ((unsigned char)*text < 32 || (unsigned char)*text == 127) putchar(' ');
        else fwrite(text, 1, length, stdout);
        text += length; used += columns;
    }
    while (used++ < width) putchar(' ');
}
static void panel_edge(bool top, const char *color) {
    printf("  %s%s", ink(color), top ? "╭" : "╰");
    for (int i = 0; i < panel_width() - 2; i++) fputs("─", stdout);
    printf("%s%s\n", top ? "╮" : "╯", ink(RESET));
}
static void panel_row(const char *border, const char *color, const char *format, ...) {
    char text[8192]; va_list arguments;
    va_start(arguments, format); vsnprintf(text, sizeof(text), format, arguments); va_end(arguments);
    printf("  %s│%s%s ", ink(border), ink(RESET), ink(color));
    panel_text(text, panel_width() - 4);
    printf(" %s%s│%s\n", ink(RESET), ink(border), ink(RESET));
}
static void panel_notice(const char *title, const char *message, const char *color) {
    if (!isatty(STDOUT_FILENO)) { printf("%s: %s\n", title, message); return; }
    panel_edge(true, color); panel_row(color, color, "%s", title);
    panel_row(color, "", "%s", message); panel_edge(false, color); fflush(stdout);
}
static void scan_progress(int completed) {
    if (!isatty(STDOUT_FILENO)) {
        if (!completed) { puts("Checking your Mac..."); fflush(stdout); }
        return;
    }
    if (terminal_active) printf("\033[H\033[2J");
    else if (completed) printf("\033[4A");
    panel_edge(true, CYAN);
    panel_row(CYAN, CYAN, "%s", completed == 3 ? "CHECK COMPLETE" : "CHECKING YOUR MAC");
    const char *bars[] = {"○ ○ ○", "● ○ ○", "● ● ○", "● ● ●"};
    panel_row(CYAN, "", "%s  %d / 3 checks  ·  Nothing closed", bars[completed], completed);
    panel_edge(false, CYAN); fflush(stdout);
}

static void restore_terminal(void) {
    if (!terminal_active) return;
    terminal_active = false;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
    const char reset[] = "\033[0m\033[?25h\033[?1049l";
    write(STDOUT_FILENO, reset, sizeof(reset) - 1);
}
static void terminal_signal(int signal_number) { restore_terminal(); _exit(128 + signal_number); }
static void fail(const char *message) { restore_terminal(); fprintf(stderr, "brutus: %s\n", message); exit(1); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void pause_seconds(double seconds) {
    struct timespec delay = {(time_t)seconds, (long)((seconds - (time_t)seconds) * 1e9)};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}
static void sys_value(const char *name, void *value, size_t size) {
    if (sysctlbyname(name, value, &size, NULL, 0) < 0) fail(name);
}
static const char *pressure(int value) { return value == 1 ? "normal" : value == 2 ? "warning" : value == 4 ? "critical" : "unknown"; }
static const char *thrashing(const State *s) {
    if (s->paging_in + s->paging_out < 32 * MIB) return "not detected";
    return s->pressure >= 2 || s->cpu_system >= 30 ? "likely" : "heavy paging";
}
static int traffic_light(const State *s) {
    double paging = s->paging_in + s->paging_out;
    if (s->pressure >= 4 || (s->space_known && s->free_disk < 5 * GIB) || (paging >= 128 * MIB && (s->pressure >= 2 || s->cpu_system >= 30)) || s->cpu_user + s->cpu_system >= 95) return 2;
    if (s->pressure >= 2 || (s->space_known && s->free_disk < 20 * GIB) || paging >= 32 * MIB || s->cpu_user + s->cpu_system >= 85) return 1;
    return 0;
}
static const char *health_message(const State *s) {
    int level = traffic_light(s);
    if (level == 2) {
        if (s->pressure >= 4) return "Memory pressure is critical. Your Mac may freeze or pause.";
        if (s->space_known && s->free_disk < 5 * GIB) return "SSD space is almost full. Apps and swap need free disk space.";
        if (s->paging_in + s->paging_out >= 128 * MIB) return "Heavy memory transfers may be making your Mac freeze or pause.";
        return "Your processor is almost fully busy. Apps may pause.";
    }
    if (s->paging_in + s->paging_out >= 32 * MIB) return "Memory is moving to and from disk a lot. Your Mac may slow down.";
    if (s->pressure >= 2) return "Memory is tight, but there is no heavy swapping right now.";
    if (s->space_known && s->free_disk < 20 * GIB) return "SSD space is low. Apps and swap need free disk space.";
    if (s->cpu_user + s->cpu_system >= 85) return "Your processor is very busy. Apps may respond slowly.";
    return "No heavy slowdown detected right now.";
}
static void show_health(const State *s) {
    const char *names[] = {"GREEN", "AMBER", "RED"};
    int level = traffic_light(s);
    printf("\n%s[%s]%s %s\n", ink(health_ink(level)), names[level], ink(RESET), health_message(s));
}
static void health_panel(const State *s) {
    int level = traffic_light(s);
    const char *names[] = {"GREEN", "AMBER", "RED"};
    const char *title = "YOUR MAC LOOKS OK", *hint = "No heavy slowdown detected. Closing a helper is optional.";
    if (s->pressure >= 4) { title = "MEMORY PRESSURE IS HIGH"; hint = "Apps may freeze or pause. Closing a helper may help."; }
    else if (s->space_known && s->free_disk < 5 * GIB) { title = "SSD SPACE IS VERY LOW"; hint = "Apps and swap need free disk space."; }
    else if (s->paging_in + s->paging_out >= 128 * MIB && level == 2) { title = "HEAVY SWAPPING"; hint = "Memory transfers may be making apps freeze or pause."; }
    else if (s->cpu_user + s->cpu_system >= 95) { title = "PROCESSOR IS VERY BUSY"; hint = "Apps may pause. Closing a busy helper may help."; }
    else if (s->paging_in + s->paging_out >= 32 * MIB) { title = "MEMORY IS MOVING TO DISK"; hint = "Your Mac may slow down. Closing a helper is optional."; }
    else if (s->pressure >= 2) { title = "MEMORY IS TIGHT"; hint = "No heavy swapping. Closing a helper is optional."; }
    else if (s->space_known && s->free_disk < 20 * GIB) { title = "SSD SPACE IS LOW"; hint = "Apps and swap need free disk space."; }
    else if (level == 1) { title = "PROCESSOR IS BUSY"; hint = "Your Mac may slow down. Closing a helper is optional."; }
    if (panel_width() < 64) {
        if (s->pressure >= 4) hint = "Apps may freeze or pause.";
        else if (s->space_known && s->free_disk < 5 * GIB) hint = "Apps and swap need disk space.";
        else if (s->paging_in + s->paging_out >= 128 * MIB && level == 2) hint = "Swapping may be causing pauses.";
        else if (s->cpu_user + s->cpu_system >= 95) hint = "Apps may pause while CPU is busy.";
        else if (s->paging_in + s->paging_out >= 32 * MIB) hint = "Memory transfers may slow apps.";
        else if (s->pressure >= 2) hint = "No heavy swapping right now.";
        else if (s->space_known && s->free_disk < 20 * GIB) hint = "Apps and swap need disk space.";
        else if (level == 1) hint = "Apps may respond slowly.";
        else hint = "Closing a helper is optional.";
    }
    const char *color = health_ink(level);
    panel_edge(true, color); panel_row(color, color, "● %s  ·  %s", names[level], title);
    if (s->paging_in + s->paging_out >= 32 * MIB) panel_row(color, "", "Memory moving to / from disk: %.0f MB/s", (s->paging_in + s->paging_out) / MIB);
    else panel_row(color, "", "%s", hint);
    if (panel_width() < 64) {
        panel_row(color, "", "Memory %.1f/%.0f GB  ·  CPU %.0f%%", (s->ram - s->free_ram) / GIB, s->ram / GIB, s->cpu_user + s->cpu_system);
    } else panel_row(color, "", "Memory %.1f / %.0f GB  ·  Processor %.0f%% busy", (s->ram - s->free_ram) / GIB, s->ram / GIB, s->cpu_user + s->cpu_system);
    const char *storage_ink = s->space_known && s->free_disk < 5 * GIB ? health_ink(2) : s->space_known && s->free_disk < 20 * GIB ? health_ink(1) : "";
    if (s->space_known) panel_row(color, storage_ink, "Swap %.1f GB  ·  SSD %.1f GB free%s", s->swap_used / GIB, s->free_disk / GIB, s->free_disk < 5 * GIB ? " — VERY LOW" : s->free_disk < 20 * GIB ? " — LOW" : "");
    else panel_row(color, "", "Swap %.1f GB  ·  SSD space unavailable", s->swap_used / GIB);
    panel_edge(false, color);
}
static const char *size_text(double bytes, char buffer[32]) {
    if (bytes >= GIB || bytes <= -GIB) snprintf(buffer, 32, "%.2f GiB", bytes / GIB);
    else snprintf(buffer, 32, "%.0f MiB", bytes / MIB);
    return buffer;
}
static const char *memory_status(int value) { return value == 1 ? "normal" : value == 2 ? "tight" : value == 4 ? "very tight" : "unknown"; }
static const char *friendly_size(double bytes, char buffer[32]) {
    if (bytes >= GIB) snprintf(buffer, 32, "%.1f GB", bytes / GIB);
    else snprintf(buffer, 32, "%.0f MB", bytes / MIB);
    return buffer;
}
static void friendly_name(const Process *p, char output[128]) {
    if (strstr(p->name, "Codex") && strstr(p->name, "Renderer")) { snprintf(output, 128, "Codex window helper"); return; }
    if (!strcmp(p->name, "codex") || (strstr(p->name, "Codex") && strstr(p->name, "Service"))) { snprintf(output, 128, "Codex background task"); return; }
    if (strstr(p->name, "Google Chrome Helper")) { snprintf(output, 128, "Chrome %s helper", strstr(p->name, "Renderer") ? "tab" : "background"); return; }
    bool build = !strcmp(p->name, "esbuild"), server = !strcmp(p->name, "workerd");
    if (build || server || !strcmp(p->name, "node") || !strcmp(p->name, "Python")) {
        struct proc_vnodepathinfo vnode;
        if (proc_pidinfo(p->pid, PROC_PIDVNODEPATHINFO, 0, &vnode, sizeof(vnode)) == sizeof(vnode)) {
            char path[PATH_MAX], git[PATH_MAX]; snprintf(path, sizeof(path), "%s", vnode.pvi_cdir.vip_path);
            for (int depth = 0; depth < 16 && path[0]; depth++) {
                if (snprintf(git, sizeof(git), "%s/.git", path) < sizeof(git) && access(git, F_OK) == 0) {
                    const char *base = strrchr(path, '/'); char project[64]; snprintf(project, sizeof(project), "%s", base ? base + 1 : path);
                    snprintf(output, 128, "%s %s", project, build ? "build helper" : server ? "local server" : "background task"); return;
                }
                char *slash = strrchr(path, '/'); if (!slash || slash == path) break; *slash = 0;
            }
        }
    }
    snprintf(output, 128, "%s%s", p->name, build ? " build helper" : server ? " local server" : "");
}
static bool bsd_info(pid_t pid, struct proc_bsdinfo *info) {
    return proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, info, sizeof(*info)) == sizeof(*info);
}
static bool same_process(const Process *p, bool alive_only) {
    struct proc_bsdinfo current;
    return bsd_info(p->pid, &current) && current.pbi_uid == p->uid &&
        current.pbi_start_tvsec == p->start_sec && current.pbi_start_tvusec == p->start_usec &&
        (!alive_only || current.pbi_status != SZOMB);
}
typedef struct { char *full; char preview[2048]; const char *unavailable; } Command;
static char *quote_argument(char *output, const char *argument) {
    bool simple = *argument != 0, controls = false;
    for (const unsigned char *c = (const unsigned char *)argument; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || strchr("-_/.:@%+=,", *c))) simple = false;
        if (*c < 32 || *c == 127) controls = true;
    }
    if (simple) { size_t length = strlen(argument); memcpy(output, argument, length); return output + length; }
    if (controls) *output++ = '$';
    *output++ = '\'';
    for (const unsigned char *c = (const unsigned char *)argument; *c; c++) {
        if (controls && (*c < 32 || *c == 127)) {
            const char hex[] = "0123456789abcdef";
            *output++ = '\\'; *output++ = 'x'; *output++ = hex[*c >> 4]; *output++ = hex[*c & 15];
        } else if (controls && (*c == '\'' || *c == '\\')) { *output++ = '\\'; *output++ = *c; }
        else if (*c == '\'') { memcpy(output, "'\\''", 4); output += 4; }
        else *output++ = *c;
    }
    *output++ = '\'';
    return output;
}
static Command process_command(const Process *p) {
    Command command = {.unavailable = "macOS could not read this process's arguments"};
    if (!same_process(p, true)) { command.unavailable = "process stopped or changed; press R to check again"; return command; }
    // KERN_PROCARGS2: argc, executable path, NUL alignment, then exactly argc
    // argument strings. Stop there: the following strings are environment data.
    int mib[] = {CTL_KERN, KERN_PROCARGS2, p->pid}; size_t bytes = 0;
    if (sysctl(mib, 3, NULL, &bytes, NULL, 0) < 0 || bytes <= sizeof(int) || bytes > 16 * MIB) return command;
    char *data = malloc(bytes);
    if (!data) { command.unavailable = "not enough memory to read the command"; return command; }
    if (sysctl(mib, 3, data, &bytes, NULL, 0) < 0 || bytes <= sizeof(int) || !same_process(p, true)) { free(data); return command; }
    int argc; memcpy(&argc, data, sizeof(argc));
    char *end = data + bytes, *argument = memchr(data + sizeof(argc), 0, bytes - sizeof(argc));
    if (!argument || argc < 1 || (size_t)argc > bytes - sizeof(argc)) { free(data); return command; }
    while (argument < end && !*argument) argument++;
    char *first = argument;
    for (int i = 0; i < argc; i++) {
        char *nul = argument < end ? memchr(argument, 0, (size_t)(end - argument)) : NULL;
        if (!nul) { free(data); return command; }
        argument = nul + 1;
    }
    size_t capacity = (size_t)(argument - first) * 4 + (size_t)argc * 3 + 1;
    command.full = malloc(capacity);
    if (!command.full) { command.unavailable = "not enough memory to display the command"; free(data); return command; }
    char *output = command.full, *first_end = NULL; argument = first;
    for (int i = 0; i < argc; i++) {
        if (i) *output++ = ' ';
        output = quote_argument(output, argument);
        if (!i) first_end = output;
        argument += strlen(argument) + 1;
    }
    *output = 0;
    const char *base = strrchr(first, '/'); base = base && base[1] ? base + 1 : first;
    if (strlen(base) < 400) {
        char *preview_end = quote_argument(command.preview, base); *preview_end = 0;
        strlcat(command.preview, first_end, sizeof(command.preview));
    } else strlcpy(command.preview, command.full, sizeof(command.preview));
    free(data); return command;
}
static int finish_menu(Command commands[3], int result) {
    for (int i = 0; i < 3; i++) free(commands[i].full);
    return result;
}
static void print_process_command(const Process *p) {
    Command command = process_command(p);
    if (command.full) { printf("   Command: %s\n", command.full); free(command.full); }
    else printf("   Command unavailable: %s.\n", command.unavailable);
}
static int compare_pid(const void *left, const void *right) {
    pid_t a = ((const Process *)left)->pid, b = ((const Process *)right)->pid;
    return (a > b) - (a < b);
}
static Process *find_process(Snapshot *snapshot, pid_t pid) {
    Process key = {.pid = pid};
    return bsearch(&key, snapshot->items, snapshot->count, sizeof(Process), compare_pid);
}
static bool main_app(const Process *process) {
    // GUI apps are launchd-owned. CLI workers can use embedded .app executables
    // (Homebrew Python is one), so a .app path alone must not protect them.
    if (process->parent != 1 && strcmp(process->name, "ChatGPT") && strcmp(process->name, "Codex")) return false;
    char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if (proc_pidpath(process->pid, path, sizeof(path)) <= 0) return false;
    char *suffix = strstr(path, ".app/");
    if (!suffix || strncmp(suffix + 5, "Contents/MacOS/", 15)) return false;
    return strchr(suffix + 20, '/') == NULL;
}
static uint64_t dictionary_number(CFDictionaryRef dictionary, CFStringRef key) {
    CFTypeRef value = CFDictionaryGetValue(dictionary, key);
    int64_t number = 0;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) CFNumberGetValue(value, kCFNumberSInt64Type, &number);
    return number > 0 ? (uint64_t)number : 0;
}
static void disk_counters(State *s) {
    io_iterator_t iterator;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOBlockStorageDriverClass), &iterator) != KERN_SUCCESS) return;
    io_object_t driver;
    while ((driver = IOIteratorNext(iterator))) {
        CFTypeRef statistics = IORegistryEntryCreateCFProperty(driver, CFSTR(kIOBlockStorageDriverStatisticsKey), kCFAllocatorDefault, 0);
        if (statistics && CFGetTypeID(statistics) == CFDictionaryGetTypeID()) {
            s->disk_read += dictionary_number(statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesReadKey));
            s->disk_write += dictionary_number(statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesWrittenKey));
            s->disk_known = true;
        }
        if (statistics) CFRelease(statistics);
        IOObjectRelease(driver);
    }
    IOObjectRelease(iterator);
}
static State read_state(void) {
    State s = {0};
    uint64_t ram;
    sys_value("hw.memsize", &ram, sizeof(ram)); s.ram = ram;
    sys_value("hw.logicalcpu", &s.cores, sizeof(s.cores));
    sys_value("kern.memorystatus_vm_pressure_level", &s.pressure, sizeof(s.pressure));
    struct xsw_usage swap;
    sys_value("vm.swapusage", &swap, sizeof(swap)); s.swap_used = swap.xsu_used; s.swap_total = swap.xsu_total;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&s.vm, &count) != KERN_SUCCESS) fail("cannot read VM counters");
    count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO, (host_info_t)&s.cpu, &count) != KERN_SUCCESS) fail("cannot read CPU counters");
    s.free_ram = (double)s.vm.free_count * vm_kernel_page_size;
    s.wired = (double)s.vm.wire_count * vm_kernel_page_size;
    s.compressor = (double)s.vm.compressor_page_count * vm_kernel_page_size;
    getloadavg(s.load, 3);
    struct statfs disk;
    if (statfs("/System/Volumes/Data", &disk) == 0) { s.free_disk = (double)disk.f_bavail * disk.f_bsize; s.space_known = true; }
    disk_counters(&s);
    s.time = now();
    return s;
}
static void read_process_swap(Process *process) {
    mach_port_t name_port = MACH_PORT_NULL;
    if (task_name_for_pid(mach_task_self(), process->pid, &name_port) != KERN_SUCCESS || name_port == MACH_PORT_NULL) return;
    task_vm_info_data_t vm = {0};
    mach_msg_type_number_t count = TASK_VM_INFO_REV6_COUNT;
    kern_return_t result = task_info(name_port, TASK_VM_INFO, (task_info_t)&vm, &count);
    mach_port_deallocate(mach_task_self(), name_port);
    if (result != KERN_SUCCESS || count < TASK_VM_INFO_REV6_COUNT || vm.ledger_swapins < 0) return;
    process->swap_readable = true;
    process->swapin_bytes = (uint64_t)vm.ledger_swapins;
    process->compressed = vm.compressed;
}
static Snapshot read_snapshot(void) {
    int capacity = proc_listallpids(NULL, 0) + 512;
    if (capacity <= 512) fail("cannot enumerate processes");
    pid_t *pids = calloc((size_t)capacity, sizeof(pid_t));
    Process *items = calloc((size_t)capacity, sizeof(Process));
    if (!pids || !items) fail("not enough memory to enumerate processes");
    int total = proc_listallpids(pids, capacity * sizeof(pid_t));
    if (total < 0 || total >= capacity) fail("process list changed too quickly; run again");
    Snapshot result = {.items = items};
    for (int index = 0; index < total; index++) {
        struct proc_bsdinfo info;
        if (!bsd_info(pids[index], &info)) continue;
        Process *p = &items[result.count++];
        p->pid = info.pbi_pid; p->parent = info.pbi_ppid; p->uid = info.pbi_uid;
        p->start_sec = info.pbi_start_tvsec; p->start_usec = info.pbi_start_tvusec; p->status = info.pbi_status;
        snprintf(p->name, sizeof(p->name), "%.*s", info.pbi_name[0] ? (int)sizeof(info.pbi_name) : (int)sizeof(info.pbi_comm), info.pbi_name[0] ? info.pbi_name : info.pbi_comm);
        if (p->uid == user_uid) {
            struct rusage_info_v4 usage;
            if (proc_pid_rusage(p->pid, RUSAGE_INFO_V4, (rusage_info_t *)&usage) == 0) {
                p->readable = true; p->cpu_ticks = usage.ri_user_time + usage.ri_system_time;
                p->resident = usage.ri_resident_size; p->footprint = usage.ri_phys_footprint;
                read_process_swap(p);
            }
            p->app_main = main_app(p);
        }
    }
    free(pids);
    qsort(items, result.count, sizeof(Process), compare_pid);
    result.state = read_state(); result.state.total = total;
    for (size_t i = 0; i < result.count; i++) {
        if (items[i].uid == user_uid) result.state.yours++;
        if (items[i].status == SSLEEP) result.state.sleeping++;
        items[i].protected = items[i].app_main;
    }
    pid_t ancestor = getpid();
    for (size_t depth = 0; ancestor > 0 && depth < result.count; depth++) {
        Process *p = find_process(&result, ancestor);
        if (!p) break;
        p->protected = true;
        if (ancestor == p->parent) break;
        ancestor = p->parent;
    }
    return result;
}
static double counter_rate(uint64_t after, uint64_t before, double seconds) {
    return after >= before ? (double)(after - before) * vm_kernel_page_size / seconds : 0;
}
static double median(double values[3]) {
    double a = values[0], b = values[1], c = values[2];
    return a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));
}
static Snapshot sample(bool announce) {
    if (announce) scan_progress(0);
    Snapshot previous = read_snapshot(); State first = previous.state;
    for (int interval = 0; interval < 3; interval++) {
        pause_seconds(1);
        Snapshot current = read_snapshot();
        double elapsed = current.state.time - previous.state.time;
        for (size_t i = 0; i < current.count; i++) {
            Process *p = &current.items[i], *old = find_process(&previous, p->pid);
            if (!p->readable || !old || !old->readable || p->start_sec != old->start_sec || p->start_usec != old->start_usec) continue;
            p->samples = old->samples;
            p->initial_footprint = old->samples ? old->initial_footprint : old->footprint;
            memcpy(p->memory, old->memory, sizeof(p->memory)); memcpy(p->cpu, old->cpu, sizeof(p->cpu));
            p->swap_samples = old->swap_samples;
            memcpy(p->swapin, old->swapin, sizeof(p->swapin));
            if (p->swap_readable && old->swap_readable && p->swap_samples < 3 && p->swapin_bytes >= old->swapin_bytes) {
                p->swapin[p->swap_samples++] = (double)(p->swapin_bytes - old->swapin_bytes) / elapsed;
            }
            if (p->samples < 3 && p->cpu_ticks >= old->cpu_ticks) {
                p->memory[p->samples] = p->footprint;
                p->cpu[p->samples] = (p->cpu_ticks - old->cpu_ticks) * ticks_to_ns / 1e9 / elapsed * 100;
                p->samples++;
            }
        }
        free(previous.items); previous = current;
        if (announce) scan_progress(interval + 1);
    }
    State *s = &previous.state;
    s->seconds = s->time - first.time;
    s->paging_in = counter_rate(s->vm.swapins, first.vm.swapins, s->seconds);
    s->paging_out = counter_rate(s->vm.swapouts, first.vm.swapouts, s->seconds);
    s->compress = counter_rate(s->vm.compressions, first.vm.compressions, s->seconds);
    s->decompress = counter_rate(s->vm.decompressions, first.vm.decompressions, s->seconds);
    double ticks[CPU_STATE_MAX], total_ticks = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) { ticks[i] = (uint32_t)(s->cpu.cpu_ticks[i] - first.cpu.cpu_ticks[i]); total_ticks += ticks[i]; }
    if (total_ticks > 0) {
        s->cpu_user = (ticks[CPU_STATE_USER] + ticks[CPU_STATE_NICE]) / total_ticks * 100;
        s->cpu_system = ticks[CPU_STATE_SYSTEM] / total_ticks * 100; s->cpu_idle = ticks[CPU_STATE_IDLE] / total_ticks * 100;
    }
    if (first.disk_known && s->disk_known) {
        s->disk_read_rate = s->disk_read >= first.disk_read ? (s->disk_read - first.disk_read) / s->seconds : 0;
        s->disk_write_rate = s->disk_write >= first.disk_write ? (s->disk_write - first.disk_write) / s->seconds : 0;
    } else s->disk_known = false;
    double cpu_weight = s->pressure >= 2 || s->paging_in + s->paging_out >= 32 * MIB ? 0.25 : 1;
    for (size_t i = 0; i < previous.count; i++) {
        Process *p = &previous.items[i];
        if (p->samples == 3) {
            p->growth = (p->footprint - p->initial_footprint) / s->seconds;
            p->footprint = median(p->memory); p->score = p->footprint / s->ram + cpu_weight * (p->cpu[0] + p->cpu[1] + p->cpu[2]) / 3 / (100 * s->cores);
            if (s->pressure >= 2 || s->paging_in + s->paging_out >= 32 * MIB) {
                if (p->swap_samples == 3) p->score += (p->swapin[0] + p->swapin[1] + p->swapin[2]) / 3 / (32 * MIB);
                if (p->growth > 0) p->score += p->growth / (64 * MIB);
            }
        }
    }
    struct rusage_info_v4 own;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&own) == 0) s->self_footprint = own.ri_phys_footprint;
    return previous;
}
static double mean_cpu(const Process *p) { return p->samples ? (p->cpu[0] + p->cpu[1] + p->cpu[2]) / p->samples : 0; }
static double mean_swapin(const Process *p) { return p->swap_samples == 3 ? (p->swapin[0] + p->swapin[1] + p->swapin[2]) / 3 : 0; }
static const char *process_swap_rate(const Process *p, char buffer[32]) {
    if (p->swap_samples != 3) snprintf(buffer, 32, "unavailable");
    else if (mean_swapin(p) == 0) snprintf(buffer, 32, "0 MB/s");
    else if (mean_swapin(p) < 0.1 * MIB) snprintf(buffer, 32, "<0.1 MB/s");
    else snprintf(buffer, 32, "%.1f MB/s", mean_swapin(p) / MIB);
    return buffer;
}
static int candidates(Snapshot *s, Process out[3], int count) {
    int found = 0;
    for (int rank = 0; rank < count; rank++) {
        Process *best = NULL;
        for (size_t i = 0; i < s->count; i++) {
            Process *p = &s->items[i]; bool used = false;
            for (int j = 0; j < found; j++) if (out[j].pid == p->pid) used = true;
            if (!used && p->uid == user_uid && !p->protected && p->samples == 3 && p->score >= 0.01 && (!best || p->score > best->score)) best = p;
        }
        if (!best) break;
        out[found++] = *best;
    }
    return found;
}
static void show_state(const State *s, bool verbose) {
    if (!verbose) {
        show_health(s);
        if (traffic_light(s) < 2) puts("Nothing urgently needs closing.");
        printf("Memory in use: %.1f of %.0f GB. Processor: %.0f%% busy.\n", (s->ram - s->free_ram) / GIB, s->ram / GIB, s->cpu_user + s->cpu_system);
        return;
    }
    printf("\nBRUTUS — thrashing %s (%s pressure)\n", thrashing(s), pressure(s->pressure));
    printf("RAM %.1f/%.0f GiB · swap %.1f GiB · paging %.0f in/%.0f out MiB/s\n", (s->ram - s->free_ram) / GIB, s->ram / GIB, s->swap_used / GIB, s->paging_in / MIB, s->paging_out / MIB);
    printf("Kernel CPU %.0f%% · %d processes · 3 samples in %.1fs\n", s->cpu_system, s->total, s->seconds);
    if (verbose) {
        printf("Free RAM %.0f MiB · wired %.1f GiB · compressor %.1f GiB · swap allocation %.1f GiB\n", s->free_ram / MIB, s->wired / GIB, s->compressor / GIB, s->swap_total / GIB);
        printf("CPU user/system/idle %.1f/%.1f/%.1f%% · load %.2f/%.2f/%.2f · your processes %d · sleeping %d\n", s->cpu_user, s->cpu_system, s->cpu_idle, s->load[0], s->load[1], s->load[2], s->yours, s->sleeping);
        printf("Compress/decompress %.0f/%.0f MiB/s · internal disk free %.1f GiB\n", s->compress / MIB, s->decompress / MIB, s->free_disk / GIB);
        if (s->disk_known) printf("Disk read/write %.0f/%.0f MiB/s\n", s->disk_read_rate / MIB, s->disk_write_rate / MIB);
        printf("Sampler footprint %.1f MiB; direct native counters, no helper processes.\n", s->self_footprint / MIB);
    }
}
static void show_process(const Process *p, int number, bool verbose, int cores) {
    if (!verbose) {
        char name[128], bytes[32]; friendly_name(p, name);
        if (number > 0) printf("%d. ", number);
        printf("%s\n", name);
        double busy = mean_cpu(p) / cores;
        printf("   Memory: %s. Processor: ", friendly_size(p->footprint, bytes));
        if (busy < 1) printf("less than 1%% of your Mac."); else printf("%.0f%% of your Mac.", busy);
        char swap_rate[32]; printf(" Swap in: %s.\n", process_swap_rate(p, swap_rate));
        print_process_command(p);
        return;
    }
    char bytes[32];
    if (number > 0) printf("[%d] ", number);
    char swap_rate[32];
    printf("%s · PID %d · %s · CPU %.1f%% · Swap in %s\n", p->name, p->pid, size_text(p->footprint, bytes), mean_cpu(p), process_swap_rate(p, swap_rate));
    print_process_command(p);
    if (verbose) {
        char path[PROC_PIDPATHINFO_MAXSIZE] = {0}; struct proc_vnodepathinfo vnode;
        printf("    parent PID %d · resident %.0f MiB · state %d%s\n", p->parent, p->resident / MIB, p->status, p->protected ? " · protected main app/current session" : "");
        if (proc_pidpath(p->pid, path, sizeof(path)) > 0) printf("    %s\n", path);
        if (proc_pidinfo(p->pid, PROC_PIDVNODEPATHINFO, 0, &vnode, sizeof(vnode)) == sizeof(vnode)) printf("    cwd: %s\n", vnode.pvi_cdir.vip_path);
    }
}
static void json_string(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(file, "\\%c", *p);
        else if (*p < 32) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
    fputc('"', file);
}
static void json_state(FILE *f, const State *s) {
    const char *health[] = {"green", "amber", "red"};
    fprintf(f, "{\"samples\":3,\"sample_seconds\":%.3f,\"ram\":%.0f,\"free_ram\":%.0f,\"wired\":%.0f,\"compressor\":%.0f,", s->seconds, s->ram, s->free_ram, s->wired, s->compressor);
    fprintf(f, "\"health\":\"%s\",", health[traffic_light(s)]);
    fprintf(f, "\"pressure\":%d,\"thrashing\":", s->pressure); json_string(f, thrashing(s));
    fprintf(f, ",\"swap_used\":%.0f,\"swap_total\":%.0f,\"swap_in_rate\":%.3f,\"swap_out_rate\":%.3f,\"compression_rate\":%.3f,\"decompression_rate\":%.3f,", s->swap_used, s->swap_total, s->paging_in, s->paging_out, s->compress, s->decompress);
    fprintf(f, "\"cpu_user\":%.3f,\"cpu_system\":%.3f,\"cpu_idle\":%.3f,\"load\":[%.3f,%.3f,%.3f],\"total_processes\":%d,\"user_processes\":%d,\"sleeping_processes\":%d,", s->cpu_user, s->cpu_system, s->cpu_idle, s->load[0], s->load[1], s->load[2], s->total, s->yours, s->sleeping);
    fprintf(f, "\"free_disk\":%.0f,\"disk_space_available\":%s,\"disk_counters_available\":%s,\"disk_read_rate\":%.3f,\"disk_write_rate\":%.3f,\"brutus_footprint\":%.0f}", s->free_disk, s->space_known ? "true" : "false", s->disk_known ? "true" : "false", s->disk_read_rate, s->disk_write_rate, s->self_footprint);
}
static void json_report(const Snapshot *s, Process offered[3], int count) {
    printf("{\"metrics\":"); json_state(stdout, &s->state); printf(",\"candidates\":[");
    for (int i = 0; i < count; i++) {
        if (i) printf(",");
        printf("{\"pid\":%d,\"parent_pid\":%d,\"name\":", offered[i].pid, offered[i].parent); json_string(stdout, offered[i].name);
        printf(",\"memory\":%.0f,\"resident\":%.0f,\"cpu\":%.3f,\"samples\":%d,\"memory_growth_rate\":%.3f,\"swap_in_samples\":%d,\"swap_in_rate\":", offered[i].footprint, offered[i].resident, mean_cpu(&offered[i]), offered[i].samples, offered[i].growth, offered[i].swap_samples);
        if (offered[i].swap_samples == 3) printf("%.3f", mean_swapin(&offered[i])); else printf("null");
        printf(",\"compressed_logical_bytes\":");
        if (offered[i].swap_readable) printf("%.0f", offered[i].compressed); else printf("null");
        printf("}");
    }
    printf("]}\n");
}
static void compare_states(const State *before, const State *after, bool verbose) {
    if (!verbose) {
        if (isatty(STDOUT_FILENO)) {
            health_panel(after);
            panel_edge(true, CYAN); panel_row(CYAN, CYAN, "RESULT  ·  BEFORE → AFTER");
            panel_row(CYAN, BOLD, "Free RAM change       %+.0f MB", (after->free_ram - before->free_ram) / MIB);
            panel_row(CYAN, "", "Memory pressure       %s → %s", memory_status(before->pressure), memory_status(after->pressure));
            panel_row(CYAN, "", "Thrashing             %s → %s", thrashing(before), thrashing(after));
            panel_row(CYAN, "", "Memory in use         %.1f → %.1f GB", (before->ram - before->free_ram) / GIB, (after->ram - after->free_ram) / GIB);
            panel_row(CYAN, "", "Swap used             %.1f → %.1f GB", before->swap_used / GIB, after->swap_used / GIB);
            if (before->space_known && after->space_known) panel_row(CYAN, "", "SSD space free        %.1f → %.1f GB", before->free_disk / GIB, after->free_disk / GIB);
            panel_row(CYAN, "", "Memory/disk transfers %.0f → %.0f MB/s", (before->paging_in + before->paging_out) / MIB, (after->paging_in + after->paging_out) / MIB);
            panel_row(CYAN, "", "Processor busy        %.0f%% → %.0f%% (%+.0f points)", before->cpu_user + before->cpu_system, after->cpu_user + after->cpu_system, after->cpu_user + after->cpu_system - before->cpu_user - before->cpu_system);
            panel_row(CYAN, "", "Total processes       %d → %d (%+d)", before->total, after->total, after->total - before->total);
            panel_edge(false, CYAN); return;
        }
        show_health(after);
        puts("\nWhat changed:");
        printf("  Free RAM change:        %+.0f MB\n", (after->free_ram - before->free_ram) / MIB);
        printf("  Memory pressure:        %s -> %s\n", memory_status(before->pressure), memory_status(after->pressure));
        printf("  Memory in use:          %.1f -> %.1f GB\n", (before->ram - before->free_ram) / GIB, (after->ram - after->free_ram) / GIB);
        printf("  Disk used as memory:    %.1f -> %.1f GB\n", before->swap_used / GIB, after->swap_used / GIB);
        if (before->space_known && after->space_known) printf("  SSD space free:         %.1f -> %.1f GB\n", before->free_disk / GIB, after->free_disk / GIB);
        printf("  Memory/disk transfers:  %.0f -> %.0f MB/second\n", (before->paging_in + before->paging_out) / MIB, (after->paging_in + after->paging_out) / MIB);
        printf("  Processor busy:         %.0f%% -> %.0f%%\n", before->cpu_user + before->cpu_system, after->cpu_user + after->cpu_system);
        printf("  Total processes:        %d -> %d\n", before->total, after->total);
        return;
    }
    printf("\nBEFORE → AFTER\nThrashing %s → %s · pressure %s → %s\n", thrashing(before), thrashing(after), pressure(before->pressure), pressure(after->pressure));
    printf("Free RAM change: %+.0f MiB\n", (after->free_ram - before->free_ram) / MIB);
    printf("Swap %.2f → %.2f GiB · paging %.0f → %.0f MiB/s\n", before->swap_used / GIB, after->swap_used / GIB, (before->paging_in + before->paging_out) / MIB, (after->paging_in + after->paging_out) / MIB);
    printf("RAM %.1f → %.1f GiB · kernel CPU %.0f → %.0f%% · processes %d → %d\n", (before->ram - before->free_ram) / GIB, (after->ram - after->free_ram) / GIB, before->cpu_system, after->cpu_system, before->total, after->total);
    if (verbose) { puts("\nFull before state:"); show_state(before, true); puts("\nFull after state:"); show_state(after, true); }
}
static void save_report(const State *before, const State *after, Process selected[3], int count, int term, int killed, int survivors, int skipped, int errors) {
    const char *home_dir = getenv("HOME"); if (!home_dir) fail("HOME is unavailable for saving the report");
    char dir[PATH_MAX];
    const char *suffixes[] = {"/.local", "/.local/state", "/.local/state/brutus"};
    for (int i = 0; i < 3; i++) {
        if (snprintf(dir, sizeof(dir), "%s%s", home_dir, suffixes[i]) >= sizeof(dir)) fail("report directory path is too long");
        if (mkdir(dir, 0700) < 0 && errno != EEXIST) fail("cannot create report directory");
    }
    char stamp[32], filename[PATH_MAX]; time_t wall = time(NULL); struct tm local;
    localtime_r(&wall, &local); strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    if (snprintf(filename, sizeof(filename), "%s/%s-%d.json", dir, stamp, getpid()) >= sizeof(filename)) fail("report path is too long");
    int descriptor = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0600); if (descriptor < 0) fail("cannot create report file");
    FILE *f = fdopen(descriptor, "w"); if (!f) fail("cannot open report stream");
    fprintf(f, "{\"before\":"); json_state(f, before); fprintf(f, ",\"after\":"); json_state(f, after);
    fprintf(f, ",\"target_pids\":["); for (int i = 0; i < count; i++) fprintf(f, "%s%d", i ? "," : "", selected[i].pid);
    fprintf(f, "],\"results\":{\"term_signals\":%d,\"kill_signals\":%d,\"survivors\":%d,\"gone_or_reused\":%d,\"errors\":%d}}\n", term, killed, survivors, skipped, errors);
    if (fclose(f) != 0) fail("could not finish writing report");
    printf("Report: %s\n", filename);
}
static void close_scene(const Process *process, bool preview, const char *status) {
    char name[128], memory[32]; friendly_name(process, name);
    printf("\033[H\033[2J  %sBRUTUS · %s%s\n\n", ink(CYAN), preview ? "PREVIEW · NOTHING CLOSED" : "TARGETED CLOSE", ink(RESET));
    panel_edge(true, CYAN);
    for (int row = 0; row < 7; row++) panel_row(CYAN, "", "");
    panel_edge(false, CYAN);
    panel_edge(true, CYAN);
    panel_row(CYAN, BOLD, "%s · PID %d", name, process->pid);
    panel_row(CYAN, "", "Process footprint: %s", friendly_size(process->footprint, memory));
    panel_row(CYAN, "", "%s", status);
    panel_edge(false, CYAN);
    printf("\033[7;7H%s", ink(BOLD)); panel_text(name, panel_width() - 10);
    fputs(ink(RESET), stdout);
}
static void dagger_sweep(const Process *process, bool preview) {
    int span = panel_width() - 14;
    for (int frame = 0; frame < 10; frame++) {
        close_scene(process, preview, preview ? "Previewing the dagger sweep..." : "Requesting this process to close...");
        int column = 7 + span * frame / 9;
        printf("\033[7;%dH%s━━%s%s", column, ink(RED), SELECTOR, ink(RESET));
        fflush(stdout); pause_seconds(0.025);
    }
}
static void game_over_banner(void) {
    int left = 3 + panel_width() / 2 - 10;
    printf("\033[7;4H"); panel_text("", panel_width() - 2);
    printf("\033[6;%dH%s╭──────────────────╮", left, ink(RED));
    printf("\033[7;%dH│    GAME OVER     │", left);
    printf("\033[8;%dH╰──────────────────╯%s", left, ink(RESET));
}
static void dagger_pop(const Process *process, bool preview) {
    int center = 3 + panel_width() / 2;
    for (int frame = 1; frame <= 12; frame++) {
        close_scene(process, preview, preview ? "Animation preview. Nothing was closed." : "Process stopped. Checking resource changes...");
        // Erase the process line, then scatter a small deterministic blood burst.
        printf("\033[7;4H"); panel_text("", panel_width() - 2);
        for (int particle = 0; particle < 30; particle++) {
            int column = center + ((particle * 17 % 37) - 18) * frame / 10;
            int row = 7 + ((particle * 7 % 11) - 5) * frame / 16 + frame * frame / 120;
            if (row >= 4 && row <= 10 && column >= 5 && column < panel_width()) {
                const char *drop[] = {"●", "·", "*"};
                printf("\033[%d;%dH%s%s%s", row, column, ink(RED), drop[particle % 3], ink(RESET));
            }
        }
        if (frame >= 8) game_over_banner();
        fflush(stdout); pause_seconds(0.025);
    }
    pause_seconds(0.45);
}
static void wait_for_close(Process selected[3], int count) {
    double deadline = now() + 3;
    while (now() < deadline) {
        bool running = false;
        for (int i = 0; i < count; i++) if (same_process(&selected[i], true)) running = true;
        if (!running) return;
        pause_seconds(0.05);
    }
}
static int terminate(Process selected[3], int count, const Options *options) {
    for (int i = 0; i < count; i++) {
        if (selected[i].protected || selected[i].uid != user_uid) fail("this is a main app or your current session. Run brutus to choose an individual helper instead");
    }
    bool animate = terminal_active && !options->no_animation && colors_enabled() && panel_width() >= 36 && terminal_rows() >= 17;
    if (!animate && terminal_active) end_terminal();
    if (!animate) {
        for (int i = 0; i < count; i++) {
            char name[128]; friendly_name(&selected[i], name);
            printf("%s %s", options->dry ? "Would close" : "Selected:", name);
            if (options->verbose) printf(" (PID %d)", selected[i].pid);
            puts(".");
        }
    }
    if (options->dry) {
        if (animate) {
            for (int i = 0; i < count; i++) { dagger_sweep(&selected[i], true); dagger_pop(&selected[i], true); }
            end_terminal();
        }
        panel_notice("PREVIEW COMPLETE", "Nothing was closed.", CYAN); return 0;
    }
    if (animate) close_scene(&selected[0], false, "Measuring resources before closing...");
    else printf("\n%s[1/3] BEFORE%s  Measuring your Mac before closing...\n", ink(CYAN), ink(RESET));
    fflush(stdout);
    Snapshot before = sample(false); int term = 0, killed = 0, skipped = 0, errors = 0;
    bool requested[3] = {false};
    if (!animate) printf("%s[2/3] CLOSING%s  Requesting only the selected processes...\n", ink(CYAN), ink(RESET));
    fflush(stdout);
    for (int i = 0; i < count; i++) {
        if (!same_process(&selected[i], true)) { skipped++; continue; }
        if (animate) dagger_sweep(&selected[i], false);
        if (!same_process(&selected[i], true)) { skipped++; continue; }
        if (kill(selected[i].pid, SIGTERM) == 0) { term++; requested[i] = true; }
        else if (errno != ESRCH) { fprintf(stderr, "PID %d: %s\n", selected[i].pid, strerror(errno)); errors++; }
    }
    if (animate) { close_scene(&selected[count - 1], false, "Waiting for the selected process to stop..."); fflush(stdout); }
    wait_for_close(selected, count);
    if (options->force) {
        for (int i = 0; i < count; i++) if (same_process(&selected[i], true)) {
            if (kill(selected[i].pid, SIGKILL) == 0) { killed++; requested[i] = true; }
            else if (errno != ESRCH) { fprintf(stderr, "PID %d: %s\n", selected[i].pid, strerror(errno)); errors++; }
        }
        if (killed) wait_for_close(selected, count);
    }
    if (animate) {
        for (int i = 0; i < count; i++) if (requested[i] && !same_process(&selected[i], true)) dagger_pop(&selected[i], false);
        bool stopped = requested[count - 1] && !same_process(&selected[count - 1], true);
        close_scene(&selected[count - 1], false, stopped ? "Process stopped. Measuring resource changes..." : "Measuring resources after the close request...");
        if (stopped) game_over_banner();
    }
    else printf("%s[3/3] AFTER%s  Checking your Mac again...\n", ink(CYAN), ink(RESET));
    fflush(stdout);
    Snapshot after = sample(false); int survivors = 0;
    if (animate) end_terminal();
    for (int i = 0; i < count; i++) {
        char name[128]; friendly_name(&selected[i], name);
        if (same_process(&selected[i], true)) {
            printf("%s is still running. To force it to close:\n  brutus close %d --force\n", name, selected[i].pid); survivors++;
        } else if (!same_process(&selected[i], false)) printf("%s has stopped.\n", name);
        else printf("%s has stopped and is waiting for its parent to finish cleanup.\n", name);
    }
    compare_states(&before.state, &after.state, options->verbose);
    if (options->verbose) printf("%d TERM, %d KILL, %d survivors.\n", term, killed, survivors);
    save_report(&before.state, &after.state, selected, count, term, killed, survivors, skipped, errors);
    free(before.items); free(after.items);
    return survivors || errors ? 1 : 0;
}
static bool number(const char *value) {
    if (!value || !*value) return false;
    for (const char *p = value; *p; p++) if (*p < '0' || *p > '9') return false;
    return true;
}
enum { KEY_UP = 1001, KEY_DOWN, MENU_QUIT = 0, MENU_CLOSE, MENU_REFRESH };
static int terminal_key(void) {
    unsigned char first;
    if (read(STDIN_FILENO, &first, 1) != 1) return 'q';
    if (first != 27) return first == '\r' || first == '\n' ? '\n' : tolower(first);
    struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
    if (poll(&input, 1, 80) <= 0) return 27;
    unsigned char next;
    if (read(STDIN_FILENO, &next, 1) != 1 || (next != '[' && next != 'O')) return 27;
    if (poll(&input, 1, 80) <= 0 || read(STDIN_FILENO, &next, 1) != 1) return 27;
    return next == 'A' ? KEY_UP : next == 'B' ? KEY_DOWN : 27;
}
static void begin_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &saved_terminal) != 0) fail("could not open keyboard controls");
    struct termios raw = saved_terminal;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) fail("could not enable keyboard controls");
    terminal_active = true;
    signal(SIGINT, terminal_signal); signal(SIGTERM, terminal_signal); signal(SIGHUP, terminal_signal); signal(SIGQUIT, terminal_signal);
    printf("\033[?1049h\033[?25l");
}
static void end_terminal(void) {
    fflush(stdout); restore_terminal();
    signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL); signal(SIGQUIT, SIG_DFL);
}
static void menu_header(const State *state, const Options *options) {
    char heading[128]; snprintf(heading, sizeof(heading), "BRUTUS %s  ·  %s  ·  sampled %.1fs", BRUTUS_VERSION, options->dry ? "PREVIEW" : "CHECKED", state->seconds);
    printf("\033[H\033[2J  %s", ink(CYAN)); panel_text(heading, panel_width()); printf("%s\n", ink(RESET));
    health_panel(state);
}
static void menu_footer(bool details, bool commands, const Options *options, bool can_close) {
    const char *action = !can_close ? "—" : options->dry ? "Preview" : options->force ? "Force close" : "Close";
    panel_edge(true, CYAN);
    if (details) {
        if (panel_width() < 40) {
            panel_row(CYAN, BOLD, "↑↓ Scroll  ·  Q Quit");
            panel_row(CYAN, "", "I / Esc Back");
            panel_row(CYAN, "", "R Check  ·  M More");
        } else {
            panel_row(CYAN, BOLD, "↑↓ Scroll · I / Esc Back · Q Quit");
            panel_row(CYAN, "", "R Check again  ·  M More commands");
        }
    } else if (commands) {
        panel_row(CYAN, BOLD, "↑↓ Choose  ·  Enter Confirm");
        panel_row(CYAN, "", "Esc Back  ·  I Info  ·  Q Quit");
    } else if (panel_width() < 64) {
        panel_row(CYAN, BOLD, "↑↓ Choose  ·  Enter %s", action);
        if (panel_width() < 36) {
            panel_row(CYAN, "", "I Info  ·  R Check");
            panel_row(CYAN, "", "M More  ·  Q Quit");
        } else panel_row(CYAN, "", "I Info  R Check  M More  Q Quit");
    } else {
        panel_row(CYAN, BOLD, "↑↓ Choose · Enter %s · I Info · R Check · M More · Q Quit", action);
    }
    panel_edge(false, CYAN);
}
typedef struct { char text[512]; bool heading; } InfoLine;
typedef struct { InfoLine lines[30]; int count, first, visible; } Information;
static void info_text(Information *info, bool heading, const char *text) {
    int width_limit = panel_width() - 4;
    do {
        InfoLine line = {.heading = heading};
        int columns = 0; size_t bytes = 0;
        while (text[bytes]) {
            int width; size_t length = text_character(text + bytes, &width);
            if (columns + width > width_limit || bytes + length >= sizeof(line.text)) break;
            bytes += length; columns += width;
        }
        bool word_wrap = false;
        if (text[bytes]) {
            size_t boundary = 0;
            for (size_t i = 0; i < bytes; i++) if (text[i] == ' ') boundary = i;
            if (boundary > bytes / 3) { bytes = boundary; word_wrap = true; }
        }
        memcpy(line.text, text, bytes); line.text[bytes] = 0;
        if (info->count >= info->first && info->count < info->first + info->visible) info->lines[info->count - info->first] = line;
        info->count++;
        text += bytes + (word_wrap ? 1 : 0);
        if (!bytes) break;
    } while (*text);
}
static void info_add(Information *info, bool heading, const char *format, ...) {
    char buffer[8192]; va_list arguments;
    va_start(arguments, format); vsnprintf(buffer, sizeof(buffer), format, arguments); va_end(arguments);
    info_text(info, heading, buffer);
}
static int show_information(const State *s, const Process *p, const Command *command, int *offset) {
    int visible = terminal_rows() - 15 - (panel_width() < 40);
    if (visible < 1) visible = 1;
    if (visible > 30) visible = 30;
    Information info = {.first = *offset, .visible = visible};
    info_add(&info, true, "SELECTED PROCESS");
    if (p) {
        char name[128]; friendly_name(p, name);
        info_add(&info, false, "%s · PID %d · Parent %d", name, p->pid, p->parent);
        info_add(&info, true, "LAUNCH COMMAND");
        if (command && command->full) info_text(&info, false, command->full);
        else info_add(&info, false, "Unavailable: %s", command ? command->unavailable : "no command was read");
        info_add(&info, false, "");
        info_add(&info, false, "Native name: %s", p->name);
        info_add(&info, false, "Memory footprint: %.2f GB · Resident: %.0f MB", p->footprint / GIB, p->resident / MIB);
        info_add(&info, false, "CPU: %.2f%% of your Mac · %.1f%% of one core", mean_cpu(p) / s->cores, mean_cpu(p));
        info_add(&info, false, "3 CPU samples (one core): %.1f%% / %.1f%% / %.1f%%", p->cpu[0], p->cpu[1], p->cpu[2]);
        info_add(&info, false, "3 memory samples (GB): %.2f / %.2f / %.2f", p->memory[0] / GIB, p->memory[1] / GIB, p->memory[2] / GIB);
        char swap_rate[32];
        info_add(&info, true, "SWAP EVIDENCE");
        info_add(&info, false, "Swap-in readback: %s · %d / 3 intervals", process_swap_rate(p, swap_rate), p->swap_samples);
        if (p->swap_readable) info_add(&info, false, "Compressed/swapped pages: %.2f GB before compression", p->compressed / GIB);
        info_add(&info, false, "Memory footprint change: %+.1f MB/s", p->growth / MIB);
        info_add(&info, false, "Swap-in measures this process reclaiming previously swapped pages.");
        info_add(&info, false, "It does not identify who forced pages out of RAM.");
        info_add(&info, false, "Scope: only PID %d will receive a close request.", p->pid);
        if (same_process(p, true)) {
            char path[PROC_PIDPATHINFO_MAXSIZE] = {0}; struct proc_vnodepathinfo vnode;
            if (proc_pidpath(p->pid, path, sizeof(path)) > 0) info_add(&info, false, "Program: %s", path);
            if (proc_pidinfo(p->pid, PROC_PIDVNODEPATHINFO, 0, &vnode, sizeof(vnode)) == sizeof(vnode)) info_add(&info, false, "Folder: %s", vnode.pvi_cdir.vip_path);
        } else info_add(&info, false, "This process has stopped or changed. Press R to check again.");
    } else info_add(&info, false, "No selectable process was found.");
    info_add(&info, false, "");
    info_add(&info, true, "YOUR MAC · sampled values, not a live monitor");
    info_add(&info, false, "Thrashing: %s · Memory pressure: %s", thrashing(s), memory_status(s->pressure));
    info_add(&info, false, "RAM: %.2f / %.0f GB · Free: %.0f MB", (s->ram - s->free_ram) / GIB, s->ram / GIB, s->free_ram / MIB);
    info_add(&info, false, "Wired: %.2f GB · Compressed: %.2f GB", s->wired / GIB, s->compressor / GIB);
    info_add(&info, false, "Swap used / allocated: %.2f / %.2f GB", s->swap_used / GIB, s->swap_total / GIB);
    info_add(&info, false, "Swap in / out: %.1f / %.1f MB/s", s->paging_in / MIB, s->paging_out / MIB);
    info_add(&info, false, "Compress / decompress: %.1f / %.1f MB/s", s->compress / MIB, s->decompress / MIB);
    info_add(&info, false, "CPU user / kernel / idle: %.1f / %.1f / %.1f%%", s->cpu_user, s->cpu_system, s->cpu_idle);
    info_add(&info, false, "CPU cores: %d · Load (1 / 5 / 15 min): %.2f / %.2f / %.2f", s->cores, s->load[0], s->load[1], s->load[2]);
    info_add(&info, false, "Processes: %d total · %d yours · %d sleeping", s->total, s->yours, s->sleeping);
    if (s->space_known) info_add(&info, false, "SSD space left for apps and swap: %.1f GB", s->free_disk / GIB);
    else info_add(&info, false, "SSD free space unavailable.");
    if (s->disk_known) info_add(&info, false, "Disk read / write: %.1f / %.1f MB/s", s->disk_read_rate / MIB, s->disk_write_rate / MIB);
    else info_add(&info, false, "Disk read / write counters unavailable.");
    info_add(&info, false, "Brutus memory: %.1f MB · 3 checks in %.1fs", s->self_footprint / MIB, s->seconds);
    info_add(&info, false, "");
    info_add(&info, true, "HOW TO READ THIS");
    info_add(&info, false, "Swap is memory kept on disk; active transfers cause pauses.");
    info_add(&info, false, "Used swap already occupies disk space; SSD free is what remains.");
    info_add(&info, false, "SSD space flags: LOW below 20 GB; VERY LOW below 5 GB.");
    info_add(&info, false, "Footprint includes compressed memory; resident is RAM only.");
    info_add(&info, false, "Candidates have sustained resource use across three checks.");
    info_add(&info, false, "The ranking does not prove this process caused a freeze.");
    if (visible > info.count) visible = info.count;
    int maximum = info.count - visible;
    if (*offset > maximum || *offset < 0) {
        *offset = *offset > maximum ? maximum : 0;
        return show_information(s, p, command, offset);
    }
    panel_edge(true, CYAN);
    panel_row(CYAN, CYAN, "INFO  ·  lines %d–%d of %d%s", *offset + 1, *offset + visible, info.count, maximum ? "  ·  ↑↓ scroll" : "");
    for (int i = 0; i < visible; i++) panel_row(CYAN, info.lines[i].heading ? CYAN : "", "%s", info.lines[i].text);
    panel_edge(false, CYAN);
    return maximum;
}
static int keyboard_menu(Snapshot *snapshot, Process offered[3], int count, Options *options, Process selected[3], int *selected_count) {
    int cursor = 0, command_cursor = 0, info_offset = 0;
    bool commands = false, details = options->verbose;
    Command launch[3] = {0};
    for (int i = 0; i < count; i++) launch[i] = process_command(&offered[i]);
    for (;;) {
        menu_header(&snapshot->state, options);
        int max_scroll = 0;
        if (details) {
            max_scroll = show_information(&snapshot->state, count ? &offered[cursor] : NULL, count ? &launch[cursor] : NULL, &info_offset);
        } else if (commands) {
            panel_edge(true, CYAN); panel_row(CYAN, CYAN, "MORE COMMANDS");
            const char *labels[] = {"Check your Mac again", "Show the top process", "Show the top three processes", "See technical details", "Close all shown processes", "Force close the selected helper", "Back"};
            for (int i = 0; i < 7; i++) panel_row(CYAN, i == command_cursor ? SELECTED : "", "%s %s%s", i == command_cursor ? SELECTOR : "  ", labels[i], (i == 4 || i == 5) && !count ? " (none available)" : i == 4 && count == 1 ? " (one shown)" : "");
            if (count) { char name[128]; friendly_name(&offered[cursor], name); panel_row(CYAN, "", "Selected: %s", name); }
            panel_edge(false, CYAN);
        } else {
            printf("  %s%s%s\n", ink(BOLD), options->dry ? "CHOOSE A HELPER TO PREVIEW" : options->force ? "CHOOSE A HELPER TO FORCE CLOSE" : traffic_light(&snapshot->state) == 2 ? "CHOOSE A HELPER TO CLOSE" : "OPTIONAL — CHOOSE A HELPER TO CLOSE", ink(RESET));
            int required_rows = 1 + 6 + 1 + count * 5 + (panel_width() < 36 ? 5 : panel_width() < 64 ? 4 : 3);
            bool compact = terminal_rows() <= required_rows;
            for (int i = 0; i < count; i++) {
                char name[128], memory[32], swap_rate[32]; friendly_name(&offered[i], name);
                const char *border = i == cursor ? CYAN : MUTED;
                if (!compact || i == 0) panel_edge(true, border);
                panel_row(border, i == cursor ? SELECTED : "", "%s %d. %s", i == cursor ? SELECTOR : "  ", i + 1, name);
                double cpu = mean_cpu(&offered[i]) / snapshot->state.cores;
                char processor[32];
                if (cpu < 1) snprintf(processor, sizeof(processor), "<1%%");
                else snprintf(processor, sizeof(processor), "%.0f%%", cpu);
                if (panel_width() >= 64) panel_row(border, "", "   Memory %s · CPU %s · Swap in %s", friendly_size(offered[i].footprint, memory), processor, process_swap_rate(&offered[i], swap_rate));
                else panel_row(border, "", "   Mem %s CPU %s Swap in %s", friendly_size(offered[i].footprint, memory), processor, process_swap_rate(&offered[i], swap_rate));
                panel_row(border, MUTED, "   Command: %s", launch[i].full ? launch[i].preview : launch[i].unavailable);
                if (!compact || i == count - 1) panel_edge(false, border);
            }
            if (!count) panel_notice("NO HELPER STOOD OUT", "Nothing closed. Press R to check again.", CYAN);
        }
        menu_footer(details, commands, options, count > 0);
        fflush(stdout);
        int key = terminal_key();
        if (key == 'q') { end_terminal(); return finish_menu(launch, MENU_QUIT); }
        if (key == 'r') return finish_menu(launch, MENU_REFRESH);
        if (key == 'i') { details = !details; commands = false; info_offset = 0; continue; }
        if (key == 'm') { commands = !commands; details = false; continue; }
        if (details) {
            if (key == 27 || key == '\n') details = false;
            else if (key == KEY_UP && info_offset) info_offset--;
            else if (key == KEY_DOWN && info_offset < max_scroll) info_offset++;
            continue;
        }
        if (key == 27) { commands = false; continue; }
        int rows = commands ? 7 : count;
        int *position = commands ? &command_cursor : &cursor;
        if (rows && key == KEY_UP) { *position = (*position + rows - 1) % rows; continue; }
        if (rows && key == KEY_DOWN) { *position = (*position + 1) % rows; continue; }
        if (key != '\n') continue;
        if (commands) {
            switch (command_cursor) {
                case 0: return finish_menu(launch, MENU_REFRESH);
                case 1: options->count = 1; return finish_menu(launch, MENU_REFRESH);
                case 2: options->count = 3; return finish_menu(launch, MENU_REFRESH);
                case 3: details = true; commands = false; info_offset = 0; continue;
                case 4:
                    if (!count) continue;
                    *selected_count = count;
                    for (int i = 0; i < count; i++) selected[i] = offered[i];
                    return finish_menu(launch, MENU_CLOSE);
                case 5: if (!count) continue; options->force = true; break;
                case 6: commands = false; continue;
            }
        }
        if (count) { *selected_count = 1; selected[0] = offered[cursor]; return finish_menu(launch, MENU_CLOSE); }
    }
}
static void usage(void) {
    puts("Find what may be slowing your Mac:\n"
         "  brutus                 Check your Mac and choose from the top three\n"
         "  brutus 1               Show only the top process\n"
         "  brutus 3               Show up to three (the default)\n"
         "  brutus scan            Print a report and exit\n\n"
         "  brutus --version       Print the installed version\n\n"
         "  brutus --no-animation  Skip the dagger animation\n\n"
         "Close a specific process using the number in its suggested command:\n"
         "  brutus close 12345\n"
         "  brutus close 12345 --dry-run   Preview without closing anything\n"
         "  brutus close 12345 --force     Force a stubborn process to close\n\n"
         "Keyboard: arrows choose, Enter closes, I shows info, R checks again,\n"
         "          M shows commands, Q quits. In info: arrows scroll, I/Esc back.\n"
         "More information: --verbose, --json, or brutus inspect 12345.\n"
         "Main apps stay protected. Only the chosen processes receive signals.");
}
int main(int argc, char **argv) {
    setlocale(LC_CTYPE, "");
    Options options = {.count = 3, .action = "scan", .interactive = true}; const char *positionals[4]; int positions = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dry-run")) options.dry = true;
        else if (!strcmp(argv[i], "--force")) options.force = true;
        else if (!strcmp(argv[i], "--verbose")) options.verbose = true;
        else if (!strcmp(argv[i], "--json")) options.json = true;
        else if (!strcmp(argv[i], "--no-animation")) options.no_animation = true;
        else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) { printf("brutus %s\n", BRUTUS_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (argv[i][0] == '-' || positions == 4) { usage(); return 1; }
        else positionals[positions++] = argv[i];
    }
    if (positions) {
        if (number(positionals[0])) {
            if (positions != 1) fail("use brutus, brutus 3, or brutus close followed by a process number");
            options.count = atoi(positionals[0]);
        } else {
            options.interactive = false;
            options.action = positionals[0];
            if (!strcmp(options.action, "scan")) {
                if (positions > 2) fail("choose one count: 1, 2, or 3");
                if (positions == 2) { if (!number(positionals[1])) fail("candidate count must be 1, 2, or 3"); options.count = atoi(positionals[1]); }
            } else if (!strcmp(options.action, "inspect") || !strcmp(options.action, "close")) {
                if (positions < 2 || (!strcmp(options.action, "inspect") && positions != 2)) fail("copy a process number from the command shown by brutus");
                for (int i = 1; i < positions; i++) {
                    if (!number(positionals[i]) || strtol(positionals[i], NULL, 10) <= 0 || strtol(positionals[i], NULL, 10) > INT32_MAX) fail("use the process number shown by brutus");
                    options.targets[options.target_count++] = positionals[i];
                }
            } else { usage(); return 1; }
        }
    }
    if (options.count < 1 || options.count > 3) fail("candidate count must be 1, 2, or 3");
    user_uid = getuid(); host = mach_host_self(); mach_timebase_info_data_t timebase;
    atexit(restore_terminal);
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.denom) fail("cannot read CPU clock conversion");
    ticks_to_ns = (double)timebase.numer / timebase.denom;
    bool menu = options.interactive && !options.json && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (menu) begin_terminal();
    for (;;) {
        Snapshot snapshot = sample(!options.json); Process offered[3], selected[3]; int offered_count = candidates(&snapshot, offered, options.count);
        if (options.json) { json_report(&snapshot, offered, offered_count); free(snapshot.items); return 0; }
        if (menu) {
            int selected_count = 0;
            int result = keyboard_menu(&snapshot, offered, offered_count, &options, selected, &selected_count);
            free(snapshot.items);
            if (result == MENU_REFRESH) continue;
            if (result == MENU_CLOSE) return terminate(selected, selected_count, &options);
            panel_notice("FINISHED", "Nothing was closed.", CYAN); return 0;
        }
        show_state(&snapshot.state, options.verbose);
        if (options.target_count) {
            int selected_count = 0;
            for (int i = 0; i < options.target_count; i++) {
                pid_t pid = (pid_t)strtol(options.targets[i], NULL, 10);
                Process *target = find_process(&snapshot, pid);
                if (!target) { printf("Process %d has already stopped.\n", pid); continue; }
                if (target->uid != user_uid) fail("this process belongs to another user. Run brutus to choose one of your own");
                if (!strcmp(options.action, "inspect")) { show_process(target, 0, true, snapshot.state.cores); free(snapshot.items); return 0; }
                bool duplicate = false;
                for (int j = 0; j < selected_count; j++) if (selected[j].pid == pid) duplicate = true;
                if (!duplicate) selected[selected_count++] = *target;
            }
            free(snapshot.items);
            if (!selected_count) { puts("Nothing needs closing. Run brutus again to check the current state."); return 0; }
            return terminate(selected, selected_count, &options);
        }
        if (offered_count) puts(traffic_light(&snapshot.state) == 2 ? "\nSuggested process to close:" : "\nOptional, if you want to free up memory:");
        for (int i = 0; i < offered_count; i++) {
            show_process(&offered[i], offered_count > 1 ? i + 1 : 0, options.verbose, snapshot.state.cores);
            printf("   Close just this process: brutus close %d\n", offered[i].pid);
        }
        if (!offered_count) puts("\nNo process stood out across all three checks. Nothing was closed.");
        if (offered_count > 1) {
            printf("\nTo close all %d shown: brutus close", offered_count);
            for (int i = 0; i < offered_count; i++) printf(" %d", offered[i].pid);
            puts("");
        }
        if (offered_count) puts("\nNothing was closed. Run a command above only if you want to close it.");
        free(snapshot.items); return 0;
    }
}

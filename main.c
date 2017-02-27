/* Check available memory and swap in a loop and start killing
 * processes if they get too low */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "meminfo.h"
#include "kill.h"
#include "log.h"

#define LOG_INTERVAL 100
#define THROTTLE_INTERVAL 5

int enable_debug = 0;

int main(int argc, char *argv[])
{
	int kernel_oom_killer = 0;
	/* If the available memory goes below this percentage, we start killing
	 * processes. 10 is a good start. */
	int mem_min_percent = 10, swap_min_percent = 10;
	long mem_min, swap_min; /* Same thing in kiB */
	int dry_run = 0;
	int ignore_oom_score_adj = 0;

	/* request line buffering for stdout - otherwise the output
	 * may lag behind stderr */
	setlinebuf(stdout);

	char *v = VERSION;
	if(strcmp(v, "")==0) {
		v = "(unknown version)";
	}
	fprintf(stderr, "earlyoom %s\n", v);

	if(chdir("/proc")!=0)
	{
		perror("Could not cd to /proc");
		exit(4);
	}

	DIR *procdir = opendir(".");
	if(procdir==NULL)
	{
		perror("Could not open /proc");
		exit(5);
	}

	int c;
	while((c = getopt (argc, argv, "m:s:kirdvh")) != -1)
	{
		switch(c)
		{
			case 'm':
				mem_min_percent = strtol(optarg, NULL, 10);
				if(mem_min_percent <= 0) {
					fprintf(stderr, "-m: Invalid percentage\n");
					exit(15);
				}
				break;
			case 's':
				swap_min_percent = strtol(optarg, NULL, 10);
				if(swap_min_percent <= 0 || swap_min_percent > 100) {
					fprintf(stderr, "-s: Invalid percentage\n");
					exit(16);
				}
				break;
			case 'k':
				kernel_oom_killer = 1;
				fprintf(stderr, "Using kernel oom killer\n");
				break;
			case 'i':
				ignore_oom_score_adj = 1;
				fprintf(stderr, "Ignoring OOM score adjustment\n");
				break;
			case 'r':
				dry_run = 1;
				fprintf(stderr, "Enabling dry-run mode\n");
				break;
			case 'd':
				enable_debug = 1;
				break;
			case 'v':
				// The version has already been printed above
				exit(0);
			case 'h':
				fprintf(stderr,
					"Usage: earlyoom [-m PERCENT] [-s PERCENT] [-k|-i] [-h]\n"
					"-m ... set available memory minimum to PERCENT of total (default 10 %%)\n"
					"-s ... set free swap minimum to PERCENT of total (default 10 %%)\n"
					"-k ... use kernel oom killer instead of own user-space implementation\n"
					"-i ... user-space oom killer should ignore positive oom_score_adj values\n"
					"-r ... user-space oom killer should not really signal (i.e. dry-run)\n"
					"-d ... enable debugging messages\n"
					"-v ... print version information and exit\n"
					"-h ... this help text\n");
				exit(1);
			case '?':
				exit(13);
		}
	}

	if(kernel_oom_killer && ignore_oom_score_adj) {
		fprintf(stderr, "Kernel oom killer does not support -i\n");
		exit(2);
	}

	if(kernel_oom_killer && dry_run) {
		fprintf(stderr, "Kernel oom killer does not support -r\n");
		exit(2);
	}

	struct meminfo m = parse_meminfo();
	mem_min = m.MemTotal * mem_min_percent / 100;
	swap_min = m.SwapTotal * swap_min_percent / 100;

	LOG("mem total: %lu MiB, min: %lu MiB (%d %%)", m.MemTotal / 1024, mem_min / 1024, mem_min_percent);
	LOG("swap total: %lu MiB, min: %lu MiB (%d %%)", m.SwapTotal / 1024, swap_min / 1024, swap_min_percent);

	/* Dry-run oom kill to make sure stack grows to maximum size before
	 * calling mlockall()
	 */
	handle_oom(procdir, 0, kernel_oom_killer, ignore_oom_score_adj, 0);

	if(mlockall(MCL_CURRENT|MCL_FUTURE) !=0 )
		perror("Could not lock memory - continuing anyway");

	struct timespec last_kill, this_kill;

	memset(&last_kill, 0, sizeof(last_kill));
	memset(&this_kill, 0, sizeof(this_kill));

	c = 1; // Start at 1 so we do not print another status line immediately
	while(1)
	{
		m = parse_meminfo();

		if(c % LOG_INTERVAL == 0)
		{
			int swap_free_percent = 0;
			if (m.SwapTotal > 0)
				swap_free_percent = m.SwapFree * 100 / m.SwapTotal;

			LOG(
				"mem avail: %lu MiB (%ld %%), swap free: %lu MiB (%d %%)",
				m.MemAvailable / 1024, m.MemAvailable * 100 / m.MemTotal,
				m.SwapFree / 1024, swap_free_percent
			);
			c=0;
		}

		c++;

		if(m.MemAvailable <= mem_min && m.SwapFree <= swap_min)
		{
			if (!clock_gettime(CLOCK_MONOTONIC, &this_kill) && ((this_kill.tv_sec - last_kill.tv_sec) < THROTTLE_INTERVAL)) {
				// Throttled
			} else {
				LOG("Out of memory! avail: %lu MiB < min: %lu MiB", m.MemAvailable / 1024, mem_min / 1024);

				handle_oom(procdir, 9, kernel_oom_killer, ignore_oom_score_adj, dry_run);
				if (clock_gettime(CLOCK_MONOTONIC, &last_kill)) {
					memset(&last_kill, 0, sizeof(last_kill));
				};
			}
		}
		
		usleep(100000); // 100ms
	}
	
	return 0;
}

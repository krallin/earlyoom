#include <stdio.h>
#include <time.h>

#define TIME_BUFSIZE 60

void print_date(FILE *stream) {
	time_t t;
	time(&t);
	struct tm * now = gmtime(&t);
	if (now == NULL) {
		return;
	}

	char buf[TIME_BUFSIZE];
	int length = strftime(buf, TIME_BUFSIZE, "%Y-%m-%d %H:%M:%S %z", now);

	if (length > 0) {
		fwrite(buf, length, 1, stream);
	} else {
		fprintf(stderr, "%ld", t);
	}
}

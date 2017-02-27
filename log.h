#include <sys/types.h>
#include <unistd.h>

#define LOG(...) do { print_date(stderr); fprintf(stderr, " %i LOG -- ",  getpid()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)

void print_date(FILE *);

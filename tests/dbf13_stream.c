#include <sdb/c_api/history.h>
#include <stdio.h>

struct sample_state { int seen, bad; };

static int real_sample(void *context, double t, double v) {
    struct sample_state *s = context;
    if (t != 4090.0 + s->seen || v != t) s->bad = 1;
    return ++s->seen < 3;
}

static int exact_sample(void *context, double t, uint64_t v, uint8_t valid) {
    struct sample_state *s = context;
    if (t != 19.0 || !valid || v != UINT64_MAX) s->bad = 1;
    ++s->seen;
    return 1;
}

static int masked_sample(void *context, double t, uint64_t v, uint8_t valid) {
    struct sample_state *s = context;
    if (t != 0 || v != 0 || valid) s->bad = 1;
    ++s->seen;
    return 1;
}

int main(int argc, char **argv) {
    char error[256] = {0};
    struct sample_state a = {0, 0}, b = {0, 0}, masked = {0, 0};
    if (argc != 4) return 2;
    if (sdb_visit_history_f64_v1(argv[1], "U", 1, 0, 4090, 4095, real_sample, &a, error, sizeof error) ||
        a.seen != 3 || a.bad ||
        sdb_visit_history_u64_v1(argv[2], "EXACT", 1, 0, 19, 19, exact_sample, &b, error, sizeof error) ||
        b.seen != 1 || b.bad ||
        sdb_visit_history_u64_v1(argv[3], "U64", 1, 0, 0, 0, masked_sample, &masked, error, sizeof error) ||
        masked.seen != 1 || masked.bad) {
        fprintf(stderr, "unexpected streamed history: %s\n", error);
        return 1;
    }
    if (sdb_visit_history_f64_v1(NULL, "U", 1, 0, 0, 0, real_sample, &a, error, sizeof error) != -1 ||
        !error[0] || a.seen != 3) return 1;
    return 0;
}

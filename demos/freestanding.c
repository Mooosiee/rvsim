/* Volatile input/output exercise ELF data/BSS, stack, and a real C compiler.
   No library calls or undefined signed overflow. main returns 30 in a0. */
volatile int input[] = {-2, 5, 12, 15};
volatile int result;

static int sum(const volatile int* p, unsigned length) {
    int total = 0;
    for (unsigned i = 0; i < length; ++i) {
        total += p[i];
    }
    return total;
}

int main(void) {
    result = sum(input, 4);
    return result;
}

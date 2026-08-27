#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

int main() {
    srand(42);

    const int slot = 1000;
    const int n = 10000000;
    int **arr = calloc(slot, sizeof(int *));

    for (size_t i = 0; i < n; i++) {
        size_t idx = rand() % slot;

        free(arr[idx]);

        size_t size = 8 + (rand() % 0x1000);
        arr[idx] = malloc(size);

        memset(arr[idx], size, size);
    }
}

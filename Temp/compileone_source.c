#include <stdio.h>

int main() {
    int n = 10;
    int sum = 0;

    for (int i = 1; i <= n; i++) {
        sum += i;
    }

    printf("Sum of 1..10 = %d\n", sum);

    if (sum > 50) {
        printf("That is a big sum!\n");
    } else {
        printf("That is a small sum.\n");
    }

    return 0;
}

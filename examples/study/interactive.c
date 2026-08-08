#include <stdio.h>

int main() {
    char name[100];
    int age;

    printf("Hello! This is an interactive C program.\n");

    printf("What is your name? ");
    scanf("%99s", name);

    printf("What is your age? ");
    scanf("%d", &age);

    printf("\nThank you, %s.\n", name);

    if (age < 18) {
        printf("You are %d years old, which is quite young!\n", age);
    } else {
        printf("You are %d years old, a fine age indeed.\n", age);
    }

    printf("\nProgram finished. Bye!\n");

    return 0;
}

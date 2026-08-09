#include <stdio.h>

int main() {
    // 1. Basic Variables and User Input
    int max_limit = 0;
    
    printf("=== C Basics and Loops Demo ===\n");
    printf("Enter a positive integer limit: ");
    
    // Read user input safely
    if (scanf("%d", &max_limit) != 1 || max_limit <= 0) {
        printf("Error: Please enter a valid positive integer.\n");
        return 1; // Exit program with error code
    }

    // 2. Conditional Logic (if-else)
    if (max_limit > 100) {
        printf("Note: That is a large limit. Processing...\n\n");
    } else {
        printf("Note: Processing small limit.\n\n");
    }

    // 3. For Loop (Perfect for fixed iterations)
    printf("--- 1. For Loop Demo ---\n");
    for (int i = 1; i <= max_limit; i++) {
        printf("Count: %d\n", i);
        if (i == 5) {
            printf("  (Reached 5, stopping loop early via break)\n");
            break; 
        }
    }

    // 4. While Loop (Perfect when condition depends on variables)
    printf("\n--- 2. While Loop Demo ---\n");
    int count = max_limit;
    while (count > 0) {
        // Skip odd numbers using continue
        if (count % 2 != 0) {
            count--;
            continue; 
        }
        printf("Even Countdown: %d\n", count);
        count--;
    }

    // 5. Do-While Loop (Guaranteed to run at least once)
    printf("\n--- 3. Do-While Loop Demo ---\n");
    int choice = 0;
    do {
        printf("Press 1 to repeat this block, or 0 to exit: ");
        if (scanf("%d", &choice) != 1) {
            break; // Exit loop if non-integer is entered
        }
    } while ( choice == 1 );

    printf("\nProgram finished successfully.\n");
    return 0;
}

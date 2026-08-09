#include <stdio.h>

// 1. Function Prototypes (Declarations)
// This tells the compiler the functions exist before main() uses them.
int calculate_power(int base, int exponent);
void print_banner(void);

int main() {
    int user_base = 0;
    int user_exp = 0;
    int result = 0;

    // Call a void function that takes no arguments
    print_banner();

    // Get user input for the calculation
    printf("Enter base number: ");
    if (scanf("%d", &user_base) != 1) return 1;

    printf("Enter exponent (power): ");
    if (scanf("%d", &user_exp) != 1) return 1;

    // 2. Function Call
    // We pass 'user_base' and 'user_exp' as arguments, and catch the return value.
    result = calculate_power(user_base, user_exp);

    // Print the final result
    printf("\nResult: %d raised to the power of %d is %d\n", user_base, user_exp, result);

    return 0;
}

// 3. Function Definition: Calculate Power
// Takes two integers as inputs, processes them, and returns an integer result.
int calculate_power(int base, int exponent) {
    int total = 1;

    // Using a basic loop to multiply the base
    for (int i = 0; i < exponent; i++) {
        total = total * base;
    }

    return total; // Sends the final answer back to main()
}

// 4. Function Definition: Print Banner
// 'void' means it takes no parameters and returns no value.
void print_banner(void) {
    printf("===================================\n");
    printf("      C Power Calculator Tool     \n");
    printf("===================================\n\n");
}

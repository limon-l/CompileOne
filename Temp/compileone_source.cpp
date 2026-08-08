#include <iostream>

int main() {
    int n = 10;
    int sum = 0;

    for (int i = 1; i <= n; i++) {
        sum += i;
    }

    std::cout << "Sum of 1..10 = " << sum << std::endl;

    if (sum > 50) {
        std::cout << "That is a big sum!" << std::endl;
    } else {
        std::cout << "That is a small sum." << std::endl;
    }

    return 0;
}

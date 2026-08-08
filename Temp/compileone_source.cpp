#include <iostream>

// Function to check if a number is prime
bool isPrime(int n) {
    // 0 and 1 are not prime numbers
    if (n <= 1) {
        return false;
    }
    
    // 2 is the only even prime number
    if (n == 2) {
        return true;
    }
    
    // Exclude all other even numbers
    if (n % 2 == 0) {
        return false;
    }
    
    // Check odd factors up to the square root of n
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) {
            return false; // Found a factor, so it is not prime
        }
    }
    
    return true; // No factors found, it is prime
}

int main() {
    int number;
    
    std::cout << "Enter a positive integer: ";
    std::cin >> number;
    
    if (isPrime(number)) {
        std::cout << number << " is a prime number." << std::endl;
    } else {
        std::cout << number << " is NOT a prime number." << std::endl;
    }
    
    return 0;
}

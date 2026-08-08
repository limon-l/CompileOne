#include <iostream>
#include <string>
#include <vector>

int main() {
    std::cout << "Hello from C++!" << std::endl;
    
    std::string name;
    std::cout << "What's your name? ";
    std::cin >> name;
    
    std::cout << "Nice to meet you, " << name << "!" << std::endl;
    
    return 0;
}

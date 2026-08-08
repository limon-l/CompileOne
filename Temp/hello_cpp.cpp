#include <iostream>

class Calculator
{
public:
    int add(int a, int b)
    {
        return a + b;
    }
};

int main()
{
    Calculator calc;
    int sum = calc.add(15, 25);
    std::cout << "C++ Calculator Result: " << sum << std::endl;
    return 0;
}
// fib.mc — recursive fibonacci (teaches functions, scopes, control flow)
int fib(int n) {
    if (n <= 1) {
        return 1;
    }
    int a = fib(n - 1);
    int b = fib(n - 2);
    return a + b;
}

print fib(10);

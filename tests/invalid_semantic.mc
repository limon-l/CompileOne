int x;
bool flag;

x = 10;
flag = x + 5; // ERROR 1: Assigning int expression to boolean variable

y = 20; // ERROR 2: Undeclared variable 'y'

int x; // ERROR 3: Redeclaration of variable 'x' in same scope

if (x) { // ERROR 4: 'if' condition must be boolean, found 'int'
    print x;
}
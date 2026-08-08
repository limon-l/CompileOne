// semtest.mc — exercises semantic diagnostics
int a = 10;
const int b = 5;
float c = 3.14;
bool ok = true;

a = c;          // SEM007 narrowing (warning)
b = 2;          // SEM003 assign to const (error)
d = a;          // SEM001 undeclared (error)
int a;          // SEM002 redeclaration (error)
c = ok;         // SEM004 type mismatch (error)
int unused_var; // SEM009 unused (warning)

while (a > 0) {
    if (c + "text") { } // SEM005 + SEM006
    a = a - 1;
}

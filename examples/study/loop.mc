// loop.mc — a runnable mini-c program (while, if, arithmetic, print)
int total = 0;
int i = 1;
while (i <= 10) {
    total = total + i;
    if (i == 5) {
        print "five reached: ";
        print i;
    }
    i = i + 1;
}
print "total = ";
print total;

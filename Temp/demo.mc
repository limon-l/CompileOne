// mini-c demo — loops, conditionals and print
int n = 10;
int sum = 0;
int i = 1;

while (i <= n) {
    sum = sum + i;
    i = i + 1;
}
print "Sum of 1..10 = ";
print sum;

if (sum > 50) {
    print "That is a big sum!";
} else {
    print "That is a small sum.";
}

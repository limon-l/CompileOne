/* comments.mc — every token kind has a colour and description */
const int MAX = 100;   // const-qualified declaration
bool flag = true;

while (MAX > 0) {
    if (flag == true && MAX >= 10) {
        flag = false;
    } else {
        flag = true;
    }
    MAX = MAX - 1;
}

print "done: ";
print MAX;

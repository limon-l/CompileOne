// invalid.mc — deliberately broken to teach lexical diagnostics
int x;
x = 10;
x = x @ 2;   // '@' is not valid mini-c
print "unterminated string: 

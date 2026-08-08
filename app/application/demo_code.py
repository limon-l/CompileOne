"""Built-in demo programs shown when the user selects a language.

Each entry is paired with a file extension so the editor, the study
pipeline and the native runner all know how to treat the source.
"""

from __future__ import annotations

LANGUAGES: dict[str, str] = {
    "Mini-C": "mini-c",
    "C": "c",
    "C++": "c++",
    "Java": "java",
}

EXTENSIONS: dict[str, str] = {
    "mini-c": ".mc",
    "c": ".c",
    "c++": ".cpp",
    "java": ".java",
}

DEMO_CODE: dict[str, str] = {
    "mini-c": """// mini-c demo — loops, conditionals and print
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
""",
    "c": """#include <stdio.h>

int main() {
    int n = 10;
    int sum = 0;

    for (int i = 1; i <= n; i++) {
        sum += i;
    }

    printf("Sum of 1..10 = %d\\n", sum);

    if (sum > 50) {
        printf("That is a big sum!\\n");
    } else {
        printf("That is a small sum.\\n");
    }

    return 0;
}
""",
    "c++": """#include <iostream>

int main() {
    int n = 10;
    int sum = 0;

    for (int i = 1; i <= n; i++) {
        sum += i;
    }

    std::cout << "Sum of 1..10 = " << sum << std::endl;

    if (sum > 50) {
        std::cout << "That is a big sum!" << std::endl;
    } else {
        std::cout << "That is a small sum." << std::endl;
    }

    return 0;
}
""",
    "java": """public class Demo {
    public static void main(String[] args) {
        int n = 10;
        int sum = 0;

        for (int i = 1; i <= n; i++) {
            sum += i;
        }

        System.out.println("Sum of 1..10 = " + sum);

        if (sum > 50) {
            System.out.println("That is a big sum!");
        } else {
            System.out.println("That is a small sum.");
        }
    }
}
""",
}

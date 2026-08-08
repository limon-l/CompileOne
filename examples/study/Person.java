package demo;

import java.util.*;

public class Person {
    private String name;
    private int age;

    public Person(String n, int a) {
        this.name = n;
        this.age = a;
    }

    public String getName() {
        return this.name;
    }

    public int getAge() {
        return this.age;
    }

    public void birthday() {
        this.age = this.age + 1;
    }

    public static int fib(int n) {
        if (n < 2) {
            return n;
        }
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) {
        int x = 5;
        System.out.println("Hello from Java! x=" + x);
        Person p = new Person("Alice", 30);
        System.out.println("Name: " + p.getName() + ", age: " + p.getAge());
        p.birthday();
        System.out.println("After birthday: " + p.getAge());
        System.out.println("fib(10) = " + fib(10));
    }
}

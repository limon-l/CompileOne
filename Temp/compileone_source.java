public class Demo {
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

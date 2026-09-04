// Test 05: Namespaces, Scope Resolution (::), and Using Directives

extern int printf(const char *fmt, ...);

namespace Math {
    int add(int a, int b) {
        return a + b;
    }

    int mul(int a, int b) {
        return a * b;
    }
}

namespace Physics {
    int compute_force(int mass, int accel) {
        return Math::mul(mass, accel);
    }
}

namespace Geometry {
    class Square {
    public:
        int side;

        Square(int s) {
            this->side = s;
        }

        int area() {
            return this->side * this->side;
        }

        int perimeter() {
            return 4 * this->side;
        }
    };
}

int main() {
    // 1. Qualified function calls
    int sum = Math::add(15, 27);
    if (sum != 42) {
        printf("FAIL: Math::add expected 42, got %d\n", sum);
        return 1;
    }

    int product = Math::mul(6, 7);
    if (product != 42) {
        printf("FAIL: Math::mul expected 42, got %d\n", product);
        return 2;
    }

    // 2. Cross-namespace calls
    int force = Physics::compute_force(10, 5);
    if (force != 50) {
        printf("FAIL: Physics::compute_force expected 50, got %d\n", force);
        return 3;
    }

    // 3. Class inside namespace
    Geometry::Square sq(7);
    if (sq.area() != 49) {
        printf("FAIL: Geometry::Square area expected 49, got %d\n", sq.area());
        return 4;
    }

    if (sq.perimeter() != 28) {
        printf("FAIL: Geometry::Square perimeter expected 28, got %d\n", sq.perimeter());
        return 5;
    }

    // 4. using namespace directive
    using namespace Math;
    int sum2 = add(100, 23);
    if (sum2 != 123) {
        printf("FAIL: unqualified add expected 123, got %d\n", sum2);
        return 6;
    }

    int prod2 = mul(11, 11);
    if (prod2 != 121) {
        printf("FAIL: unqualified mul expected 121, got %d\n", prod2);
        return 7;
    }

    printf("PASS: 05_namespace completed successfully\n");
    return 0;
}

// Test 03: Classes, Member Functions, Access Specifiers, and 'this'

extern int printf(const char *fmt, ...);

class Counter {
private:
    int value;
    int step;

public:
    void init(int start_val, int step_val) {
        this->value = start_val;
        this->step = step_val;
    }

    void tick() {
        this->value = this->value + this->step;
    }

    int get_value() {
        return this->value;
    }

    void reset() {
        this->value = 0;
    }
};

class Rectangle {
public:
    int width;
    int height;

    void set_dimensions(int w, int h) {
        this->width = w;
        this->height = h;
    }

    int area() {
        return this->width * this->height;
    }

    int perimeter() {
        return 2 * (this->width + this->height);
    }
};

int main() {
    // 1. Counter test
    Counter c1;
    c1.init(10, 5);
    c1.tick();
    c1.tick(); // 10 + 5 + 5 = 20

    if (c1.get_value() != 20) {
        printf("FAIL: counter expected 20, got %d\n", c1.get_value());
        return 1;
    }

    c1.reset();
    if (c1.get_value() != 0) {
        printf("FAIL: counter reset expected 0, got %d\n", c1.get_value());
        return 2;
    }

    // 2. Rectangle test
    Rectangle rect;
    rect.set_dimensions(7, 6);

    if (rect.area() != 42) {
        printf("FAIL: rectangle area expected 42, got %d\n", rect.area());
        return 3;
    }

    if (rect.perimeter() != 26) {
        printf("FAIL: rectangle perimeter expected 26, got %d\n", rect.perimeter());
        return 4;
    }

    printf("PASS: 03_classes completed successfully\n");
    return 0;
}

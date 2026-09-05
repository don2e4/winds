extern int printf(const char *fmt, ...);
extern int puts(const char *str);

class Point {
public:
    int x;
    int y;

    Point(int _x, int _y) {
        x = _x;
        y = _y;
    }

    Point operator+(Point other) {
        Point res(x + other.x, y + other.y);
        return res;
    }

    bool operator==(Point other) {
        return x == other.x && y == other.y;
    }
};

class IntArray {
    int data[4];
public:
    IntArray() {
        data[0] = 10;
        data[1] = 20;
        data[2] = 30;
        data[3] = 40;
    }

    int operator[](int idx) {
        return data[idx];
    }
};

class MiniStream {
public:
    MiniStream() {}
    MiniStream& operator<<(const char *s) {
        printf("%s", s);
        return *this;
    }
    MiniStream& operator<<(int n) {
        printf("%d", n);
        return *this;
    }
};

int main() {
    Point p1(10, 20);
    Point p2(30, 40);
    Point p3 = p1 + p2;

    if (p3.x != 40 || p3.y != 60) {
        puts("Point addition failed");
        return 1;
    }

    Point p4(40, 60);
    if (p3 == p4) {
        puts("Point equality passed");
    } else {
        puts("Point equality failed");
        return 2;
    }

    IntArray arr;
    if (arr[1] != 20 || arr[3] != 40) {
        puts("Subscript operator failed");
        return 3;
    }

    MiniStream ms;
    ms << "Values: " << arr[0] << ", " << arr[2] << "\n";

    puts("Operator overload tests passed!");
    return 0;
}

// Test 04: Constructors, Destructors, new, and delete

extern int printf(const char *fmt, ...);

class Vector2D {
public:
    int x;
    int y;

    Vector2D(int a, int b) {
        this->x = a;
        this->y = b;
    }

    int dot(Vector2D &other) {
        return this->x * other.x + this->y * other.y;
    }
};

class Resource {
public:
    int id;

    Resource(int id_num) {
        this->id = id_num;
    }

    ~Resource() {
        this->id = -1;
    }

    int get_id() {
        return this->id;
    }
};

int main() {
    // 1. Stack constructor
    Vector2D v1(3, 4);
    Vector2D v2(2, 5);

    int dot_prod = v1.dot(v2); // 3*2 + 4*5 = 6 + 20 = 26
    if (dot_prod != 26) {
        printf("FAIL: dot product expected 26, got %d\n", dot_prod);
        return 1;
    }

    // 2. Heap allocation with new and delete
    Resource *res = new Resource(42);
    if (res == nullptr) {
        printf("FAIL: new Resource returned null\n");
        return 2;
    }

    if (res->get_id() != 42) {
        printf("FAIL: res id expected 42, got %d\n", res->get_id());
        return 3;
    }

    delete res;

    printf("PASS: 04_ctor_dtor completed successfully\n");
    return 0;
}

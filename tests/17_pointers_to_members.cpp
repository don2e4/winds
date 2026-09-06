extern int puts(const char *s);

struct point {
    int x;
    int y;
    int z;
};

class calculator {
public:
    int factor;

    calculator(int f) {
        factor = f;
    }

    int add(int a, int b) {
        return (a + b) * factor;
    }

    int multiply(int a, int b) {
        return (a * b) * factor;
    }

    int get_factor() {
        return factor;
    }
};

using point_member = int point::*;
typedef int point::*point_member_typedef;
using calc_method = int (calculator::*)(int, int);
typedef int (calculator::*calc_method_typedef)(int, int);

int get_point_field(point *pt, int point::*ptr) {
    return pt->*ptr;
}

int dispatch_calc(calculator *c, int (calculator::*m)(int, int), int x, int y) {
    return (c->*m)(x, y);
}

int main() {
    /* 1. Pointer to data member: read and write via .* */
    point pt;
    pt.x = 10;
    pt.y = 20;
    pt.z = 30;

    int point::*px = &point::x;
    int point::*py = &point::y;
    int point::*pz = &point::z;

    if (pt.*px != 10 || pt.*py != 20 || pt.*pz != 30) {
        puts("fail: pt.*p reading mismatch");
        return 1;
    }

    /* Modify via .* */
    pt.*px = 100;
    pt.*py = 200;
    if (pt.x != 100 || pt.y != 200) {
        puts("fail: pt.*p writing mismatch");
        return 2;
    }

    /* 2. Pointer to data member: read and write via ->* */
    point *ppt = &pt;
    if (ppt->*px != 100 || ppt->*py != 200) {
        puts("fail: ppt->*p reading mismatch");
        return 3;
    }

    ppt->*pz = 300;
    if (pt.z != 300) {
        puts("fail: ppt->*pz writing mismatch");
        return 4;
    }

    /* 3. Aliases: using and typedef */
    point_member m1 = &point::x;
    if (pt.*m1 != 100) {
        puts("fail: using alias mismatch");
        return 5;
    }

    /* 4. Passing data member pointer to function */
    if (get_point_field(&pt, px) != 100 ||
        get_point_field(&pt, py) != 200 ||
        get_point_field(&pt, pz) != 300) {
        puts("fail: get_point_field mismatch");
        return 6;
    }

    /* 5. Pointer to member function via .* and ->* */
    calculator calc(2);
    int (calculator::*m_add)(int, int) = &calculator::add;
    int (calculator::*m_mul)(int, int) = &calculator::multiply;

    if ((calc.*m_add)(3, 4) != 14) {
        puts("fail: (calc.*m_add)(3, 4) != 14");
        return 7;
    }

    if ((calc.*m_mul)(3, 4) != 24) {
        puts("fail: (calc.*m_mul)(3, 4) != 24");
        return 8;
    }

    calculator *pcalc = &calc;
    if ((pcalc->*m_add)(10, 5) != 30) {
        puts("fail: (pcalc->*m_add)(10, 5) != 30");
        return 9;
    }

    if ((pcalc->*m_mul)(5, 6) != 60) {
        puts("fail: (pcalc->*m_mul)(5, 6) != 60");
        return 10;
    }

    /* 6. Reassigning member function pointer */
    int (calculator::*op)(int, int) = m_add;
    if ((calc.*op)(1, 2) != 6) {
        puts("fail: (calc.*op)(1, 2) != 6");
        return 11;
    }
    op = m_mul;
    if ((calc.*op)(1, 2) != 4) {
        puts("fail: (calc.*op)(1, 2) != 4");
        return 12;
    }

    /* 7. Member function pointer aliases */
    calc_method u_op = &calculator::add;
    calc_method_typedef t_op = &calculator::multiply;
    if ((calc.*u_op)(10, 20) != 60) {
        puts("fail: (calc.*u_op)(10, 20) != 60");
        return 13;
    }
    if ((calc.*t_op)(10, 20) != 400) {
        puts("fail: (calc.*t_op)(10, 20) != 400");
        return 14;
    }

    /* 8. Passing member function pointer as parameter */
    if (dispatch_calc(&calc, &calculator::add, 7, 8) != 30) {
        puts("fail: dispatch_calc add != 30");
        return 15;
    }
    if (dispatch_calc(&calc, &calculator::multiply, 7, 8) != 112) {
        puts("fail: dispatch_calc multiply != 112");
        return 16;
    }

    /* 9. Parameterless member function pointer */
    int (calculator::*get_f)() = &calculator::get_factor;
    if ((calc.*get_f)() != 2) {
        puts("fail: (calc.*get_f)() != 2");
        return 17;
    }
    if ((pcalc->*get_f)() != 2) {
        puts("fail: (pcalc->*get_f)() != 2");
        return 18;
    }

    puts("all pointers to members tests passed successfully!");
    return 0;
}

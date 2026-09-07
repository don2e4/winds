static int table[2][3] = {{-1, 1 << 4}, {10 + 2, 7, 9}};
int sum(int row[]) { return row[0] + row[1] + row[2]; }
int main(void) {
    int cube[2][2][3] = {{{1,2,3},{4}},{{5,6},{7,8,9}}};
    cube[0][1][2] = 11;
    int *row = cube[1][1];
    int (*rows)[3] = cube[1];
    static int constants[] = {-7, sizeof(int), 10 + 2};
    int widths[sizeof(long)] = {0};
    return !(table[0][0] == -1 && table[0][1] == 16 && table[0][2] == 0 &&
        cube[0][1][1] == 0 && cube[0][1][2] == 11 && sum(row) == 24 &&
        constants[0] == -7 && constants[1] == 4 && constants[2] == 12 &&
        sizeof *row == 4 && sizeof cube == 48 && sizeof widths == 32 && rows[1][2] == 9);
}

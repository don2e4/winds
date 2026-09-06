#ifdef __linux__
#error -U did not remove __linux__
#endif

extern int c_value();
int project_value();

int main() {
    if (project_value() + c_value() == 42) return 0;
    return 1;
}

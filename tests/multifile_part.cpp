#ifndef BUILD_VALUE
#error BUILD_VALUE must be provided with -D
#endif

int project_value() {
    return BUILD_VALUE;
}

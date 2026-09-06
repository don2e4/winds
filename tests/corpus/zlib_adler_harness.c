extern unsigned long adler32(unsigned long adler, const unsigned char *buffer,
                             unsigned int length);

int main(void) {
    const unsigned char input[] = "hello";
    return adler32(1, input, 5) == 0x062c0215UL ? 0 : 1;
}

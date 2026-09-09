#include "zlib.h"
#include <string.h>

int main(void) {
    const unsigned char input[] = "winds corpus: full zlib round trip";
    unsigned char compressed[128];
    unsigned char output[128];
    uLongf compressed_size = sizeof(compressed);
    uLongf output_size = sizeof(output);
    if (compress(compressed, &compressed_size, input, sizeof(input)) != Z_OK) return 1;
    if (uncompress(output, &output_size, compressed, compressed_size) != Z_OK) return 2;
    return output_size == sizeof(input) && memcmp(input, output, sizeof(input)) == 0 ? 0 : 3;
}

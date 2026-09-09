#include "cJSON.h"
#include <string.h>

int main(void) {
    cJSON *root = cJSON_Parse("{\"name\":\"winds\",\"values\":[1,2.5]}");
    if (!root) return 1;
    if (!cJSON_AddBoolToObject(root, "native", 1)) return 4;
    cJSON *values = cJSON_GetObjectItemCaseSensitive(root, "values");
    if (!cJSON_IsArray(values) || cJSON_GetArraySize(values) != 2) return 2;
    char printed[256];
    int rendered = cJSON_PrintPreallocated(root, printed, sizeof(printed), 0);
    cJSON *again = rendered ? cJSON_Parse(printed) : 0;
    int ok = again && strstr(printed, "\"native\":true") != 0;
    cJSON_Delete(again);
    cJSON_Delete(root);
    return ok ? 0 : 3;
}

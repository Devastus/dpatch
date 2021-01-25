#define INI_IMPL
#include "ini.h"

void
test_parsed(void* data, char* section, char* key, char* value) {
    printf("section: %s, key: %s, value: %s\n", section, key, value);
}

int
ini_test_body() {
    FILE* fp = fopen("tests/test.ini", "r");
    if (!fp) perror("Ok then");

    int parsed = ini_parse(fp, test_parsed, NULL);
    printf("parse result code: %d\n", parsed);
    fclose(fp);

    return 0;
}

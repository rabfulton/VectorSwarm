#include "level_editor.h"
#include "leveldef.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_file_bytes(const char* path, char** out_data, size_t* out_size) {
    FILE* f = NULL;
    long len = 0;
    size_t nread = 0;
    char* data = NULL;
    if (!path || !out_data || !out_size) {
        return 0;
    }
    *out_data = NULL;
    *out_size = 0;
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    data = (char*)malloc((size_t)len + 1u);
    if (!data) {
        fclose(f);
        return 0;
    }
    nread = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (nread != (size_t)len) {
        free(data);
        return 0;
    }
    data[nread] = '\0';
    *out_data = data;
    *out_size = nread;
    return 1;
}

static int write_file_bytes(const char* path, const char* data, size_t size) {
    FILE* f = NULL;
    size_t n = 0;
    if (!path || !data) {
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    n = fwrite(data, 1, size, f);
    fclose(f);
    return n == size;
}

int main(void) {
    leveldef_db db;
    level_editor_state editor;
    char saved_path[LEVEL_EDITOR_PATH_CAP];
    char* before = NULL;
    char* after = NULL;
    size_t before_n = 0;
    size_t after_n = 0;

    if (chdir(VTYPE_SOURCE_DIR) != 0) {
        fprintf(stderr, "roundtrip: chdir to source root failed\n");
        return 1;
    }
    if (!leveldef_load_project_layout(&db, "data/levels", stderr)) {
        fprintf(stderr, "roundtrip: failed to load leveldef db\n");
        return 1;
    }

    level_editor_init(&editor);
    if (!level_editor_load_by_name(&editor, &db, "level_defender")) {
        fprintf(stderr, "roundtrip: level load failed\n");
        return 1;
    }
    if (editor.source_path[0] == '\0') {
        fprintf(stderr, "roundtrip: source path unresolved\n");
        return 1;
    }

    if (!read_file_bytes(editor.source_path, &before, &before_n)) {
        fprintf(stderr, "roundtrip: read before failed (%s)\n", editor.source_path);
        return 1;
    }
    if (!level_editor_save_current(&editor, &db, saved_path, sizeof(saved_path))) {
        fprintf(stderr, "roundtrip: save failed (%s)\n", editor.status_text);
        free(before);
        return 1;
    }
    if (strcmp(saved_path, editor.source_path) != 0) {
        fprintf(stderr, "roundtrip: saved path mismatch\n");
        free(before);
        return 1;
    }
    if (!read_file_bytes(editor.source_path, &after, &after_n)) {
        fprintf(stderr, "roundtrip: read after failed (%s)\n", editor.source_path);
        free(before);
        return 1;
    }
    if (before_n != after_n || memcmp(before, after, before_n) != 0) {
        fprintf(stderr, "roundtrip: file changed after load/save (%s)\n", editor.source_path);
        free(before);
        free(after);
        return 1;
    }

    /* Modified save path: nudge first marker X and confirm file changes. */
    level_editor_select_marker(&editor, 1);
    editor.selected_property = 0;
    level_editor_adjust_selected_property(&editor, 0.01f);
    if (!level_editor_save_current(&editor, &db, saved_path, sizeof(saved_path))) {
        fprintf(stderr, "roundtrip: modified save failed (%s)\n", editor.status_text);
        free(before);
        free(after);
        return 1;
    }
    free(after);
    after = NULL;
    after_n = 0;
    if (!read_file_bytes(editor.source_path, &after, &after_n)) {
        fprintf(stderr, "roundtrip: read modified failed (%s)\n", editor.source_path);
        free(before);
        return 1;
    }
    if (before_n == after_n && memcmp(before, after, before_n) == 0) {
        fprintf(stderr, "roundtrip: modified save produced no file changes\n");
        free(before);
        free(after);
        return 1;
    }

    /* Restore original file to keep repo unchanged after test. */
    if (!write_file_bytes(editor.source_path, before, before_n)) {
        fprintf(stderr, "roundtrip: restore original failed (%s)\n", editor.source_path);
        free(before);
        free(after);
        return 1;
    }

    free(before);
    free(after);
    return 0;
}

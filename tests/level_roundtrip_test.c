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

static int verify_wave_type_remap_semantics(void) {
    level_editor_state editor;
    level_editor_marker* m = NULL;

    level_editor_init(&editor);
    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;
    editor.marker_count = 1;
    editor.selected_marker = 0;
    editor.selected_property = 0;

    m = &editor.markers[0];
    memset(m, 0, sizeof(*m));
    m->track = LEVEL_EDITOR_TRACK_SPATIAL;
    m->x01 = 0.50f;
    m->y01 = 0.50f;

    /* Regression: manta B is size, not speed; cycling to kamikaze must not keep B=4. */
    m->kind = LEVEL_EDITOR_MARKER_MANTA_WING;
    m->a = 2.0f;
    m->b = 4.0f;
    m->c = 2.0f;
    m->d = 0.0f;
    level_editor_adjust_selected_property(&editor, 1.0f);
    if (m->kind != LEVEL_EDITOR_MARKER_WAVE_KAMIKAZE) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle manta->kamikaze\n");
        return 0;
    }
    if (m->b < 100.0f || m->c < 1.0f) {
        fprintf(stderr, "roundtrip: wave remap kept incompatible manta params for kamikaze (b=%.3f c=%.3f)\n", m->b, m->c);
        return 0;
    }

    /* Regression: fish D is turn rate; cycling to jelly/eel must not keep huge D as size scale. */
    m->kind = LEVEL_EDITOR_MARKER_BOID_BIRD;
    m->a = 12.0f;
    m->b = 300.0f;
    m->c = 7.8f;
    m->d = 440.0f;
    level_editor_adjust_selected_property(&editor, 1.0f); /* bird -> jelly */
    if (m->kind != LEVEL_EDITOR_MARKER_JELLY_SWARM) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle bird->jelly\n");
        return 0;
    }
    if (m->d > 4.0f) {
        fprintf(stderr, "roundtrip: wave remap kept incompatible turn-rate as jelly size (d=%.3f)\n", m->d);
        return 0;
    }
    level_editor_adjust_selected_property(&editor, 1.0f); /* jelly -> eel */
    if (m->kind != LEVEL_EDITOR_MARKER_EEL_SWARM) {
        fprintf(stderr, "roundtrip: wave remap failed to cycle jelly->eel\n");
        return 0;
    }
    if (m->d <= 0.0f || m->d > 4.0f) {
        fprintf(stderr, "roundtrip: wave remap produced invalid eel size scale (d=%.3f)\n", m->d);
        return 0;
    }

    return 1;
}

static int verify_exit_portal_tool_placement(void) {
    level_editor_state editor;
    level_editor_layout layout;
    const float w = 1280.0f;
    const float h = 720.0f;

    level_editor_init(&editor);
    level_editor_compute_layout(w, h, &layout);

    editor.level_background_mask_style = LEVELDEF_BG_MASK_WINDOWS;
    editor.level_wave_mode = LEVELDEF_WAVES_CURATED;

    {
        float bx = layout.exit_button.x + layout.exit_button.w * 0.5f;
        float by = layout.exit_button.y + layout.exit_button.h * 0.5f;
        float vx = layout.viewport.x + layout.viewport.w * 0.5f;
        float vy = layout.viewport.y + layout.viewport.h * 0.5f;
        if (!level_editor_handle_mouse(&editor, bx, by, w, h, 1, 1)) {
            fprintf(stderr, "roundtrip: exit button click was not handled\n");
            return 0;
        }
        if (!editor.entity_drag_active || editor.entity_drag_kind != LEVEL_EDITOR_MARKER_EXIT) {
            fprintf(stderr, "roundtrip: exit button did not arm exit drag tool\n");
            return 0;
        }
        if (!level_editor_handle_mouse_release(&editor, vx, vy, w, h)) {
            fprintf(stderr, "roundtrip: exit drag release did not place marker\n");
            return 0;
        }
    }

    if (editor.marker_count != 1 || editor.markers[0].kind != LEVEL_EDITOR_MARKER_EXIT) {
        fprintf(stderr, "roundtrip: exit tool placement failed (count=%d kind=%d)\n",
                editor.marker_count,
                (editor.marker_count > 0) ? editor.markers[0].kind : -1);
        return 0;
    }
    if (editor.entity_tool_selected != LEVEL_EDITOR_TOOL_NONE) {
        fprintf(stderr, "roundtrip: exit tool was not cleared after placement\n");
        return 0;
    }
    return 1;
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
    if (!verify_wave_type_remap_semantics()) {
        return 1;
    }
    if (!verify_exit_portal_tool_placement()) {
        return 1;
    }

    level_editor_init(&editor);
    if (!level_editor_load_by_name(&editor, &db, "level_blank")) {
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

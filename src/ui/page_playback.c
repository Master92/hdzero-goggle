#include "page_playback.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <log/log.h>

#include "common.hh"
#include "core/app_state.h"
#include "core/osd.h"
#include "lang/language.h"
#include "record/record_definitions.h"
#include "ui/page_common.h"
#include "ui/ui_player.h"
#include "ui/ui_style.h"
#include "util/filesystem.h"
#include "util/math.h"
#include "util/system.h"

#define MEDIA_FILES_DIR REC_diskPATH REC_packPATH // "/mnt/extsd/movies" --> "/mnt/extsd" "/movies/"
// #define MEDIA_FILES_DIR "/mnt/extsd/movies/"--Useful for testing playback page

bool status_displayed = false;
lv_obj_t *status;
LV_IMG_DECLARE(img_star);
LV_IMG_DECLARE(img_arrow1);

static lv_coord_t col_dsc[] = {320, 320, 320, LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {150, 30, 150, 30, 150, 30, 30, LV_GRID_TEMPLATE_LAST};

static media_file_node_t media_db;
static media_file_node_t *currentFolder = &media_db;
static int cur_sel = 0;
static pb_ui_item_t pb_ui[ITEMS_LAYOUT_CNT];
static const char* root_label = "leave folder";
static char current_filter_dir[256] = "";

/**
 * Displays the status message box.
 */
static void page_playback_open_status_box(const char *title, const char *text) {
    status_displayed = true;
    lv_label_set_text(lv_msgbox_get_title(status), title);
    lv_label_set_text(lv_msgbox_get_text(status), text);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_HIDDEN);
}
/**
 * Cancel operation.
 */
static void page_playback_cancel() {
}

static bool page_playback_close_status_box() {
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
    status_displayed = false;
    return status_displayed;
}

static void page_playback_on_click(uint8_t key, int sel) {
    pb_key(key);
}

static lv_obj_t *page_playback_create(lv_obj_t *parent, panel_arr_t *arr) {
    char buf[128];
    lv_obj_t *page = lv_menu_page_create(parent, NULL);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page, 1142, 900);
    lv_obj_add_style(page, &style_subpage, LV_PART_MAIN);
    lv_obj_set_style_pad_top(page, 46, 0);

    lv_obj_t *section = lv_menu_section_create(page);
    lv_obj_add_style(section, &style_submenu, LV_PART_MAIN);
    lv_obj_set_size(section, 1142, 894);

    sprintf(buf, "%s:", _lang("Playback"));
    create_text(NULL, section, false, buf, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t *cont = lv_obj_create(section);
    lv_obj_set_size(cont, 1164, 760);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont, &style_context, LV_PART_MAIN);

    for (uint32_t pos = 0; pos < ITEMS_LAYOUT_CNT; pos++) {
        pb_ui[pos]._img = lv_img_create(cont);
        lv_obj_set_size(pb_ui[pos]._img, ITEM_PREVIEW_W, ITEM_PREVIEW_H);
        lv_obj_add_flag(pb_ui[pos]._img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_style(pb_ui[pos]._img, &style_pb_dark, LV_PART_MAIN);

        pb_ui[pos]._arrow = lv_img_create(cont);
        lv_img_set_src(pb_ui[pos]._arrow, &img_arrow1);
        lv_obj_add_flag(pb_ui[pos]._arrow, LV_OBJ_FLAG_HIDDEN);

        pb_ui[pos]._star = lv_img_create(cont);
        lv_img_set_src(pb_ui[pos]._star, &img_star);
        lv_obj_add_flag(pb_ui[pos]._star, LV_OBJ_FLAG_HIDDEN);

        pb_ui[pos]._label = lv_label_create(cont);
        lv_obj_set_style_text_font(pb_ui[pos]._label, &lv_font_montserrat_26, 0);
        lv_label_set_long_mode(pb_ui[pos]._label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(pb_ui[pos]._label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(pb_ui[pos]._label, LV_OBJ_FLAG_HIDDEN);

        pb_ui[pos].x = pos % ITEMS_LAYOUT_COLS;
        pb_ui[pos].y = (uint32_t)((double)(pos) / (double)(ITEMS_LAYOUT_COLS));

        pb_ui[pos].x = PB_X_START + pb_ui[pos].x * (ITEM_PREVIEW_W + ITEM_GAP_W);
        pb_ui[pos].y = PB_Y_START + pb_ui[pos].y * (ITEM_PREVIEW_H + ITEM_GAP_H);
        pb_ui[pos].state = ITEM_STATE_INVISIBLE;

        lv_obj_set_pos(pb_ui[pos]._img, pb_ui[pos].x + (ITEM_GAP_W >> 1), pb_ui[pos].y);

        lv_obj_set_pos(pb_ui[pos]._arrow, pb_ui[pos].x + (ITEM_PREVIEW_W >> 2) - 10,
                       pb_ui[pos].y + ITEM_PREVIEW_H + 10);

        lv_obj_set_pos(pb_ui[pos]._star, pb_ui[pos].x + 5, pb_ui[pos].y);

        lv_obj_set_pos(pb_ui[pos]._label, pb_ui[pos].x + (ITEM_PREVIEW_W >> 2) + ITEM_GAP_W,
                       pb_ui[pos].y + ITEM_PREVIEW_H + 10);
    }

    lv_obj_t *label = lv_label_create(cont);
    sprintf(buf, "*%s\n**%s", _lang("Long press the Enter button to exit"), _lang("Long press the Func button to delete"));
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(label, lv_color_make(255, 255, 255), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(label, 10, 700);
    status = create_msgbox_item("Status", "None");
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);

    media_db.parent = NULL;

    return page;
}

static void show_pb_item(uint8_t pos, media_file_node_t *node) {
    if (pb_ui[pos].state == ITEM_STATE_INVISIBLE) {
        lv_obj_add_flag(pb_ui[pos]._img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pb_ui[pos]._label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pb_ui[pos]._arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pb_ui[pos]._star, LV_OBJ_FLAG_HIDDEN);
        return;
    }

	if (node == NULL) {
        return;
    }

    char fname[256];
	const char * const label = node->label;
    lv_label_set_text(pb_ui[pos]._label, label);
    lv_obj_clear_flag(pb_ui[pos]._label, LV_OBJ_FLAG_HIDDEN);

    const lv_coord_t labelPosX = pb_ui[pos].x + (ITEM_PREVIEW_W - lv_txt_get_width(label, strlen(label) - 2, &lv_font_montserrat_26, 0, 0)) / 2;
    const lv_coord_t labelPosY = pb_ui[pos].y + ITEM_PREVIEW_H + 10;
    lv_obj_set_pos(pb_ui[pos]._label, labelPosX, labelPosY);
    lv_obj_set_pos(pb_ui[pos]._arrow, labelPosX - lv_obj_get_width(pb_ui[pos]._arrow) - 5, labelPosY);

    sprintf(fname, "%s/%s." REC_packJPG, TMP_DIR, label);
    if (strcmp(label, root_label) == 0) {
        osd_resource_path(fname, "%s", OSD_RESOURCE_720, DEF_ARROW_UP);
    } else if (node->children != NULL) {
        osd_resource_path(fname, "%s", OSD_RESOURCE_720, DEF_FOLDER);
    } else if (fs_file_exists(fname)) {
        sprintf(fname, "A:%s/%s." REC_packJPG, TMP_DIR, label);
    } else {
        osd_resource_path(fname, "%s", OSD_RESOURCE_720, DEF_VIDEOICON);
    }
    lv_img_set_src(pb_ui[pos]._img, fname);

    if (pb_ui[pos].state == ITEM_STATE_HIGHLIGHT) {
        lv_obj_remove_style(pb_ui[pos]._img, &style_pb_dark, LV_PART_MAIN);
        lv_obj_add_style(pb_ui[pos]._img, &style_pb, LV_PART_MAIN);
        lv_obj_clear_flag(pb_ui[pos]._arrow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_style(pb_ui[pos]._img, &style_pb, LV_PART_MAIN);
        lv_obj_add_style(pb_ui[pos]._img, &style_pb_dark, LV_PART_MAIN);
        lv_obj_add_flag(pb_ui[pos]._arrow, LV_OBJ_FLAG_HIDDEN);
    }

    if (node->star) {
        lv_obj_clear_flag(pb_ui[pos]._star, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(pb_ui[pos]._star, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(pb_ui[pos]._img, LV_OBJ_FLAG_HIDDEN);
}

int get_videofile_cnt() {
    int size = media_db.size;
    for (int i = 0; i < media_db.size; i++) {
        if (media_db.children[i].children != NULL) {
            size += media_db.children[i].size;
        }
    }

    return size;
}

static void clear_media_db() {
    for (int i = 0; i < media_db.size; i++) {
        free(media_db.children[i].children);
    }
    free(media_db.children);
    media_db.children = NULL;

    media_db.size = 0;
}

void clear_videofile_cnt() {
    clear_media_db();
    cur_sel = 0;
}

static media_file_node_t *get_list(int seq) {
    int seq_reserve = currentFolder->size - 1 - seq;
    return &currentFolder->children[seq_reserve];
}

static bool get_seleteced(int seq, char *fname) {
    media_file_node_t *pnode = get_list(seq);
    if (!pnode)
        return false;
    sprintf(fname, "%s%s", pnode->parent->filename, pnode->filename);
    return true;
}

int hot_alphasort(const struct dirent **a, const struct dirent **b) {
    const bool a_hot = strncmp((*a)->d_name, REC_hotPREFIX, 4) == 0;
    const bool b_hot = strncmp((*b)->d_name, REC_hotPREFIX, 4) == 0;
    const bool a_dir = (*a)->d_type == DT_DIR;
    const bool b_dir = (*b)->d_type == DT_DIR;
    if (a_dir && !b_dir) {
        return 1;
    }
    if (!a_dir && b_dir) {
        return -1;
    }
    if (a_hot && !b_hot) {
        return -1;
    }
    if (!a_hot && b_hot) {
        return 1;
    }
    return strcoll((*a)->d_name, (*b)->d_name);
}

static bool dvr_has_stars(const char *filename) {
    char star_file[256] = "";
    int count = snprintf(star_file, sizeof(star_file), "%s" REC_starSUFFIX, filename);

    return fs_file_exists(star_file);
}

static int filter(const struct dirent *entry) {
    if (entry->d_name[0] == '.') {
        // Skip current directory, parent directory and hidden files
        return 0;
    }

    if (entry->d_type == DT_DIR) {
        return 1;
    }

    const char *dot = strrchr(entry->d_name, '.');
    if (dot == NULL) {
        // Skip files without extension
        return 0;
    }

    if (strcasecmp(dot, "." REC_packTS) != 0 && strcasecmp(dot, "." REC_packMP4) != 0) {
        // Skip files that are not .ts or .mp4 files
        return 0;
    }

    char fname[512];
    sprintf(fname, "%s%s", current_filter_dir, entry->d_name);
    if ((fs_filesize(fname) >> 20) < 5) {
        // Skip files that are smaller than 5MiB
        return 0;
    }

    return 1;
}

static int scan_directory(const char* dir, media_file_node_t *node) {
    char fname[512];
    const bool isRootScan = node == &media_db;

    struct dirent **namelist;
    strcpy(current_filter_dir, dir);
    int count = scandir(dir, &namelist, filter, hot_alphasort);
    if (count == -1) {
        return 0;
    }

    strcpy(node->filename, dir);
    strcpy(node->label, dir + strlen(MEDIA_FILES_DIR));
    const size_t dirnameLength = strlen(node->label);
    if (dirnameLength > 0) {
        node->label[dirnameLength - 1] = 0; // Strip trailing '/' from folder names
    } else {
        strcpy(node->label, root_label);
    }
    node->size = count;
    node->children = malloc((count + (!isRootScan ?: 0)) * sizeof(media_file_node_t));
    node->star = false;

    for (size_t i = 0; i < count; i++) {
        struct dirent *in_file = namelist[i];
        if (in_file->d_type == DT_DIR) {
            sprintf(fname, "%s%s/", MEDIA_FILES_DIR, in_file->d_name);
            scan_directory(fname, &node->children[i]);
            continue;
        }

        sprintf(fname, "%s%s", dir, in_file->d_name);

        const char *dot = strrchr(in_file->d_name, '.');

        if (i >= MAX_VIDEO_FILES) {
            LOGI("max video file cnt reached %d,skipped", MAX_VIDEO_FILES);
            continue;
        }

        media_file_node_t *pnode = &node->children[i];
        ZeroMemory(pnode->filename, sizeof(pnode->filename));
        ZeroMemory(pnode->label, sizeof(pnode->label));
        ZeroMemory(pnode->ext, sizeof(pnode->ext));
        pnode->children = NULL;

        pnode->parent = node;
        strcpy(pnode->filename, in_file->d_name);
        strncpy(pnode->label, in_file->d_name, dot - in_file->d_name);
        strcpy(pnode->ext, dot + 1);
        pnode->star = dvr_has_stars(fname);

        pnode->size = (int)(fs_filesize(fname) >> 20); // in MB;
        LOGI("%d: %s-%dMB", node->size, pnode->filename, pnode->size);
    }

    for (size_t i = 0; i < count; i++) {
        free(namelist[i]);
    }
    free(namelist);

    if (!isRootScan) {
        strcpy(node->children[node->size++].label, media_db.label);
    }

    return node->size;
}

static int walk_sdcard() {
    char cmd[512];

    clear_videofile_cnt();
    scan_directory(MEDIA_FILES_DIR, &media_db);

    // copy all thumbnail files to /tmp
    sprintf(cmd, "cp %s*." REC_packJPG " %s", MEDIA_FILES_DIR, TMP_DIR);
    system_exec(cmd);

    return media_db.size;
}

static int find_next_available_hot_index() {
    DIR *fd = opendir(MEDIA_FILES_DIR);
    if (!fd) {
        return 1;
    }

    int result = 0;
    struct dirent *in_file;
    while ((in_file = readdir(fd))) {
        if (in_file->d_name[0] == '.' || strncmp(in_file->d_name, REC_hotPREFIX, 4) != 0) {
            continue;
        }

        const char *dot = strrchr(in_file->d_name, '.');
        if (dot == NULL) {
            // '.' not found
            continue;
        }

        if (strcasecmp(dot, "." REC_packTS) != 0 && strcasecmp(dot, "." REC_packMP4) != 0) {
            continue;
        }

        int index = 0;
        if (sscanf(in_file->d_name, REC_packHotPREFIX "%d", &index) != 1) {
            continue;
        }

        if (index > result) {
            result = index;
        }
    }
    closedir(fd);

    return result + 1;
}

static void update_page() {
    uint32_t const page_num = (uint32_t)floor((double)cur_sel / ITEMS_LAYOUT_CNT);
    uint32_t const end_pos = currentFolder->size - page_num * ITEMS_LAYOUT_CNT;
    uint32_t const cur_pos = cur_sel - page_num * ITEMS_LAYOUT_CNT;

    for (uint8_t i = 0; i < ITEMS_LAYOUT_CNT; i++) {
        uint32_t seq = i + page_num * ITEMS_LAYOUT_CNT;
        if (seq < currentFolder->size) {
            media_file_node_t *pnode = get_list(seq);
            if (!pnode) {
                perror("update_page failed.");
                return;
            }

            if (i < cur_pos)
                pb_ui[i].state = ITEM_STATE_NORMAL;
            else if (i == cur_pos)
                pb_ui[i].state = ITEM_STATE_HIGHLIGHT;
            else if (i < end_pos)
                pb_ui[i].state = ITEM_STATE_NORMAL;
            else
                pb_ui[i].state = ITEM_STATE_INVISIBLE;

            show_pb_item(i, pnode);
        } else {
            pb_ui[i].state = ITEM_STATE_INVISIBLE;
            show_pb_item(i, NULL);
        }
    }
}

static void update_item(uint8_t cur_pos, uint8_t lst_pos) {
    if (cur_pos == lst_pos) {
        return;
    }

    lv_obj_clear_flag(pb_ui[cur_pos]._arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style(pb_ui[cur_pos]._img, &style_pb_dark, LV_PART_MAIN);
    lv_obj_add_style(pb_ui[cur_pos]._img, &style_pb, LV_PART_MAIN);

    lv_obj_add_flag(pb_ui[lst_pos]._arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style(pb_ui[lst_pos]._img, &style_pb, LV_PART_MAIN);
    lv_obj_add_style(pb_ui[lst_pos]._img, &style_pb_dark, LV_PART_MAIN);
}

static void mark_video_file(int const seq) {
    media_file_node_t const *const pnode = get_list(seq);
    if (!pnode) {
        return;
    }
    if (pnode->children != NULL) {
        // trying to mark a folder
        return;
    }
    if (strncmp(pnode->filename, REC_hotPREFIX, 4) == 0) {
        // file already marked hot
        return;
    }

    const int index = find_next_available_hot_index();

    char cmd[256];
    char newLabel[68];
    sprintf(newLabel, "%s%s", REC_hotPREFIX, pnode->label);

    sprintf(cmd, "mv %s%s %s%s.%s", MEDIA_FILES_DIR, pnode->filename, MEDIA_FILES_DIR, newLabel, pnode->ext);
    system_exec(cmd);
    sprintf(cmd, "mv %s%s." REC_packJPG " %s%s." REC_packJPG, MEDIA_FILES_DIR, pnode->label, MEDIA_FILES_DIR, newLabel);
    system_exec(cmd);
    sprintf(cmd, "mv %s%s" REC_starSUFFIX " %s%s.%s" REC_starSUFFIX, MEDIA_FILES_DIR, pnode->filename, MEDIA_FILES_DIR, newLabel, pnode->ext);
    system_exec(cmd);

    walk_sdcard();
    cur_sel = constrain(seq, 0, (currentFolder->size - 1));
    update_page();
}

static void delete_video_file(int seq) {
    media_file_node_t const *const pnode = get_list(seq);
    if (!pnode) {
        LOGE("delete_video_file failed. (PNODE ERROR)");
        return;
    }

    char cmd[128];
    if (pnode->children != NULL) {
        if (strcmp(pnode->label, root_label) == 0) {
            LOGE("delete_video_file failed. Trying to delete the root folder.");
            return;
        } else if (pnode->size > 1) {
            // Parent directory is always a member
            LOGE("delete_video_file failed. Trying to delete a non-empty folder");
            return;
        } else {
            sprintf(cmd, "rm -rf %s", pnode->filename);
        }
    } else {
        sprintf(cmd, "rm %s%s.*", MEDIA_FILES_DIR, pnode->label);
    }

    if (system_exec(cmd) != -1) {
        walk_sdcard();
        cur_sel = constrain(seq, 0, (currentFolder->size - 1));
        update_page();
        LOGD("delete_video_file successful.");
    } else {
        LOGE("delete_video_file failed.");
    }
}

static void page_playback_exit() {
    page_playback_close_status_box();
    clear_videofile_cnt();
    currentFolder = &media_db;
    update_page();
}

static void page_playback_enter() {
    const int ret = walk_sdcard();
    update_page();

    if (ret == 0) {
        // no files found, back out
        submenu_exit();
    }
}

void pb_key(uint8_t const key) {
    static bool done = true;
    static uint8_t state = 0; // 0= select video files, 1=playback
    static bool status_deleting = false;
    char fname[128];
    uint32_t cur_page_num, lst_page_num;
    uint8_t cur_pos, lst_pos;

    if (state == 1) {
        if (mplayer_on_key(key)) {
            state = 0;
            app_state_push(APP_STATE_SUBMENU);
        }
        return;
    }

    if (!key || !currentFolder->size || (!done && status_displayed && !status_deleting)) {
        return;
    }
    char text[128];
    done = false;
    switch (key) {
    case DIAL_KEY_UP: // up
        if (status_displayed) {
            page_playback_close_status_box();
            break;
        }
        lst_page_num = (uint32_t)floor((double)cur_sel / ITEMS_LAYOUT_CNT);
        lst_pos = cur_sel - lst_page_num * ITEMS_LAYOUT_CNT;

        if (cur_sel == (currentFolder->size - 1)) {
            cur_sel = 0;
        } else {
            cur_sel++;
        }

        cur_page_num = (uint32_t)floor((double)cur_sel / ITEMS_LAYOUT_CNT);
        cur_pos = cur_sel - cur_page_num * ITEMS_LAYOUT_CNT;

        if (lst_page_num == cur_page_num) {
            update_item(cur_pos, lst_pos);
        } else {
            update_page();
        }
        break;

    case DIAL_KEY_DOWN: // down
        if (status_displayed) {
            page_playback_close_status_box();
            break;
        }
        lst_page_num = (uint32_t)floor((double)cur_sel / ITEMS_LAYOUT_CNT);
        lst_pos = cur_sel - lst_page_num * ITEMS_LAYOUT_CNT;

        if (cur_sel) {
            cur_sel--;
        } else {
            cur_sel = (currentFolder->size - 1);
        }

        cur_page_num = (uint32_t)floor((double)cur_sel / ITEMS_LAYOUT_CNT);
        cur_pos = cur_sel - cur_page_num * ITEMS_LAYOUT_CNT;

        if (lst_page_num == cur_page_num) {
            update_item(cur_pos, lst_pos);
        } else {
            update_page();
        }
        break;

    case DIAL_KEY_CLICK: { // Enter
        if (status_displayed) {
            delete_video_file(cur_sel);
            status_deleting = page_playback_close_status_box();
        } else {
            media_file_node_t * item = get_list(cur_sel);
            if (strcmp(item->label, root_label) == 0) {
                currentFolder = &media_db;
                cur_sel = 0;
                update_page();
                break;
            }
            if (item->children != NULL) {
                LOGI("Trying to enter folder %s%s", MEDIA_FILES_DIR, item->label);
                currentFolder = item;
                cur_sel = 0;
                update_page();
                break;
            }
            if (get_seleteced(cur_sel, fname)) {
                mplayer_file(fname);
                state = 1;
                app_state_push(APP_STATE_PLAYBACK);
            }
        }
        break;
    }

    case DIAL_KEY_PRESS: // long press
        if (status_deleting) {
            status_deleting = page_playback_close_status_box();
        } else if (currentFolder != &media_db) {
            currentFolder = &media_db;
            cur_sel = 0;
            update_page();
        } else {
            page_playback_exit();
        }
        break;

    case RIGHT_KEY_CLICK:
        if (status_displayed) {
            status_deleting = page_playback_close_status_box();
        } else {
            mark_video_file(cur_sel);
        }
        break;

    case RIGHT_KEY_PRESS:
        if (!status_displayed) {
            snprintf(text, sizeof(text), "%s", "Click center of dial to continue.\nClick function(right button) or scroll to exit.");
            page_playback_open_status_box("Are you sure you want to DELETE the file", text);
            status_deleting = true;
        } else {
            page_playback_close_status_box();
        }
        break;
        done = true;
    }
}
static void page_playback_on_roller(uint8_t key) {
    pb_key(key);
}

static void page_playback_on_right_button(bool is_short) {
    pb_key(is_short ? RIGHT_KEY_CLICK : RIGHT_KEY_PRESS);
}

page_pack_t pp_playback = {
    .name = "Playback",
    .create = page_playback_create,
    .enter = page_playback_enter,
    .exit = page_playback_exit,
    .on_created = NULL,
    .on_update = NULL,
    .on_roller = page_playback_on_roller,
    .on_click = page_playback_on_click,
    .on_right_button = page_playback_on_right_button,
};

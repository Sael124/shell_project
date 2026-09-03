#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PATH 1024

/* -------------------------------------------------------
 * print_tree – recursively print directory contents
 *   path  : directory to open
 *   level : depth (used for indentation)
 * ------------------------------------------------------- */
void print_tree(const char *path, int level) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror(path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* skip hidden entries (. and ..) */
        if (entry->d_name[0] == '.') continue;

        /* print indentation */
        for (int i = 0; i < level; i++) printf("  ");

        if (entry->d_type == DT_DIR) {
            /* it is a sub-directory */
            printf("%s/\n", entry->d_name);

            /* build full sub-path and recurse */
            char subPath[MAX_PATH];
            snprintf(subPath, sizeof(subPath), "%s/%s", path, entry->d_name);
            print_tree(subPath, level + 1);
        } else {
            /* regular file (or symlink, etc.) */
            printf("%s\n", entry->d_name);
        }
    }

    closedir(dir);
}

/* -------------------------------------------------------
 * main – entry point when executed via execvp from shell
 *   argv[1] : path to print (defaults to "." if not given)
 * ------------------------------------------------------- */
int main(int argc, char **argv) {
    const char *startPath = (argc > 1) ? argv[1] : ".";
    printf("%s\n", startPath);
    print_tree(startPath, 1);
    return 0;
}

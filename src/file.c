#include "kernel-header/config.h"
#include "std-header/std_stringio.h"
#include "shell-header/shell_common.h"
#include "basic-header/std_opr.h"
#include "std-header/boolean.h"

void file(char *dirtable, char current_dir_index, char *target);

int main() {
    char directory_table[FILES_SECTOR_SIZE*SECTOR_SIZE];
    char shell_cache[SECTOR_SIZE];
    char arg_vector[ARGC_MAX][ARG_LENGTH];
    char argc = 0;
    int current_dir_index;

    clear(shell_cache, SECTOR_SIZE);
    getDirectoryTable(directory_table);
    getShellCache(shell_cache);

    clear(arg_vector, ARGC_MAX*ARG_LENGTH);
    memcpy(arg_vector[0], shell_cache+ARGV_OFFSET, ARG_LENGTH);
    memcpy(arg_vector[1], shell_cache+ARGV_2_OFFSET, ARG_LENGTH);
    memcpy(arg_vector[2], shell_cache+ARGV_3_OFFSET, ARG_LENGTH);
    argc = shell_cache[ARGC_OFFSET];
    current_dir_index = shell_cache[CURRENT_DIR_CACHE_OFFSET];

    // Argument count
    if (argc == 2)
        if (!strcmp("--help", arg_vector[0])) {
            print("Utility to determine file type\n", BIOS_WHITE);
            print("Possible Usage:\n", BIOS_LIGHT_BLUE);
            print("file [file_name]\n", BIOS_LIGHT_CYAN);
        }
        else {
            file(directory_table, current_dir_index, arg_vector[0]);
        }
    else
        print("Usage : file <source>\n", BIOS_WHITE);

    setShellCache(shell_cache);
    shellReturn();
}

bool isTextCharOnly(char *buffer) {
    int i = 0;
    int null_count = 0;
    bool isCharOnly = true;
    while (i < FILE_SIZE_MAXIMUM && null_count < 5 && isCharOnly) {
        if (buffer[i] == '\0')
            null_count++;
        else
            null_count = 0;

        if (!(CHAR_SPACE <= buffer[i] && buffer[i] <= CHAR_TILDE
        || buffer[i] == CHAR_NULL || buffer[i] == CHAR_CARRIAGE_RETURN
        || buffer[i] == CHAR_LINEFEED) )
            isCharOnly = false;

        i++;
    }
    return isCharOnly;
}

void file(char *dirtable, char current_dir_index, char *target) {
    // char as string / char
    char filename_buffer[16];
    char source_directory_name[16];
    char string_byte_count[32];
    char string_disk_byte_count[32];
    // char as 1 byte integer
    char file_read[FILE_SIZE_MAXIMUM];
    char sectors_buf[SECTOR_SIZE];
    char target_entry_byte = 0;
    char target_parent_byte = 0;
    char source_dir_idx;
    char link_status = -1;
    int returncode_src = 0;
    bool is_write_success = false, valid_filename = true;
    bool f_target_found = false, empty_entry_found = false;
    bool is_found_parent = false;
    int i = 0, j = 0;
    int f_entry_idx = 0;
    int f_entry_sector_idx = 0;
    int last_slash_index;
    int sectors_byte_count;

    // Relative pathing
    clear(source_directory_name, 16);
    // Split target / source
    last_slash_index = getLastMatchedCharIdx(CHAR_SLASH, target);
    // FIXME : Extra, unsafe getlast
    if (last_slash_index != -1) {
        // Split argument to path and filename
        // Get path
        strcpybounded(source_directory_name, target, last_slash_index);
        source_dir_idx = directoryEvaluator(dirtable, source_directory_name, &returncode_src, current_dir_index);

        // Get filename
        strcpybounded(source_directory_name, target+last_slash_index+1, ARG_LENGTH-last_slash_index-1);
    }
    else {
        // Cut slash
        strcpybounded(source_directory_name, target, ARG_LENGTH);

        source_dir_idx = current_dir_index;
        returncode_src = 0;
    }



    // Copying file if path evaluation success
    if (returncode_src == -1) {
        print("file: ", BIOS_GRAY);
        print(target, BIOS_GRAY);
        print(": target not found\n", BIOS_GRAY);
    }
    else {
        clear(file_read, FILE_SIZE_MAXIMUM);
        read(file_read, source_directory_name, &returncode_src, source_dir_idx);
        if (returncode_src == 0) {

            // Find entry in files
            i = 0;
            while (i < FILES_ENTRY_COUNT && !is_found_parent) {
                clear(filename_buffer, 16);
                strcpybounded(filename_buffer, dirtable+FILES_ENTRY_SIZE*i+PATHNAME_BYTE_OFFSET, 14);
                if (dirtable[i*FILES_ENTRY_SIZE + PARENT_BYTE_OFFSET] == source_dir_idx &&
                    !strcmp(source_directory_name, filename_buffer)) {
                    is_found_parent = true;
                    target_entry_byte = dirtable[i*FILES_ENTRY_SIZE+ENTRY_BYTE_OFFSET];
                    link_status = dirtable[i*FILES_ENTRY_SIZE+LINK_BYTE_OFFSET];
                }
                i++;
            }

            print("file: ", BIOS_WHITE);
            print(source_directory_name, BIOS_WHITE);
            print("\n", BIOS_WHITE);

            if (link_status == HARDLINK_ENTRY) {
                print("type : ", BIOS_WHITE);
                print("hardlink\n", BIOS_LIGHT_RED);
            }
            else if (link_status == SOFTLINK_ENTRY) {
                print("type : ", BIOS_WHITE);
                print("softlink\n", BIOS_LIGHT_CYAN);
                // Get linked file / folder with recursion depth limit 16
                i = 0;
                // TODO : Extra, currently only single softlink depth supported
                clear(filename_buffer, 16);
                strcpybounded(filename_buffer, dirtable+target_entry_byte*FILES_ENTRY_SIZE+PATHNAME_BYTE_OFFSET, 14);
                target_parent_byte = dirtable[target_entry_byte*FILES_ENTRY_SIZE + PARENT_BYTE_OFFSET];
                target_entry_byte = dirtable[target_entry_byte*FILES_ENTRY_SIZE + ENTRY_BYTE_OFFSET];
                clear(file_read, FILE_SIZE_MAXIMUM);
                read(file_read, filename_buffer, &returncode_src, target_parent_byte);
                // Needed to compare due _mash_cache always filling empty space
                // Implying softlink to _mash_cache is not available
                if (!strcmp(filename_buffer, "_mash_cache"))
                    target_entry_byte = EMPTY_FILES_ENTRY;
            }

            if (returncode_src == 0 && target_entry_byte != EMPTY_FILES_ENTRY) {
                if (target_entry_byte != FOLDER_ENTRY) {
                    if (isBinaryFileMagicNumber(file_read)) {
                        print("type : ", BIOS_WHITE);
                        print("binary executable\n", BIOS_LIGHT_GREEN);
                    }
                    else if (isTextCharOnly(file_read))
                        print("type : text\n", BIOS_WHITE);
                    else
                        print("type : file\n", BIOS_WHITE);

                    clear(string_byte_count, 32);
                    clear(string_disk_byte_count, 32);
                    inttostr(string_byte_count, strlenbin(file_read) - 5);
                    print("file size : ", BIOS_WHITE);
                    print(string_byte_count, BIOS_WHITE);

                    directSectorRead(sectors_buf, SECTORS_SECTOR);
                    i = 0;
                    sectors_byte_count = 0;
                    while (i < SECTORS_ENTRY_SIZE) {
                        if (sectors_buf[target_entry_byte*SECTORS_ENTRY_SIZE+i] != FILLED_EMPTY_SECTORS_BYTE)
                        sectors_byte_count++;
                        i++;
                    }

                    print(" bytes\ndisk size : ", BIOS_WHITE);
                    inttostr(string_disk_byte_count, sectors_byte_count*SECTOR_SIZE);
                    print(string_disk_byte_count, BIOS_WHITE);
                    print(" bytes\n", BIOS_WHITE);
                }
                else {
                    print("type : folder\n", BIOS_WHITE);
                }
            }
            else {
                print("file: ", BIOS_WHITE);
                print(source_directory_name, BIOS_WHITE);
                print(" softlink broken\n", BIOS_WHITE);
            }

        }
        else {
            print("file: ", BIOS_WHITE);
            print(source_directory_name, BIOS_WHITE);
            print(" not found\n", BIOS_WHITE);
        }


    }
}

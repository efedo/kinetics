#pragma once
// Port of DIRECT.HPP/.CPP: the file and folder browser (find_a_file,
// create_a_file, find_a_directory) and the small file helpers around it.
//
// v3.51 used DOS findfirst()/findnext(); this browses the real file system
// with std::filesystem on the desktop, and the page's in-memory file system
// in the browser, where find_a_file also lists an entry for uploading a file
// from the user's computer. Paths are UTF-8 strings in the browser's form:
// native separators and no trailing one ("C:" for a drive root on Windows,
// "" for the POSIX root).

#include <string>
#include <vector>

#include "classic/screen.hpp"
#include "classic/task.hpp"

namespace classic {

inline constexpr int OLDER = 1, SAME = 2, YOUNGER = 3;

class directory {
    std::string drive_and_path;  // v3.51: char[80]
    std::string extension;       // v3.51: char[10]
    int highlight_colour = WHITE;
    // The names behind the listed entries. v3.51 read them back from the
    // text ("[name]"), which long file names no longer fit.
    std::vector<std::string> entry_name;

    void change_to_subdirectory(int entry);
    int find_subdirectories(char fn[][80], int* colour, int Start, int Stop);
    int find_subfiles(char fn[][80], int Start, int Stop);
    Task<int> get_file_name(std::string& name, int flag);
    void go_back_one_directory();
    int list_drives(char fn[][80], int* colour);
    Task<void> print_error(const char* s, const char* sm);
    void print_message(int row, const char* s);
    Task<int> test_drive_ready(char drive);
    void set_entry(char fn[][80], int i, const std::string& text, const std::string& name);
    Task<int> upload_a_file(std::string& dap, std::string& file_name);

public:
    int search_for_a_current_directory_file(const char* ext, char fn[][80], int Stop);
    int search_for_a_file(const std::string& dap, const char* ext, char fn[][80], int Stop);
    Task<int> create_a_file(std::string& dap, const char* ext, std::string& file_name, int left, int top, int right,
                            int bottom, int sc, int hc, int ec, int restore, int KEEP_FN);
    Task<int> find_a_file(std::string& dap, const char* ext, std::string& file_name, int left, int top, int right,
                          int bottom, int sc, int hc, int ec, int restore);
    int find_num_files(const std::string& dap, const char* ext, std::string& last_file_found);
    void extract_file_name(const std::string& ffname, std::string& fname, std::string& ext);
    void get_current_directory(std::string& dap);
    int compare_files(const std::string& ffn1, const std::string& ffn2);
    void update_files(const std::string& daps, const std::string& fnames, const std::string& destination);
    int find_single_file(const std::string& dap, const std::string& fname, const std::string& ext);
    Task<int> find_a_directory(std::string& dap, int left, int top, int right, int bottom, int sc, int hc, int ec,
                               int restore);
};

}  // namespace classic

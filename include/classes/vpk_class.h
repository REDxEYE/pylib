#ifndef PYLIB_VPK_CLASS_H
#define PYLIB_VPK_CLASS_H

#include <Python.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

struct VPKEntry {
    std::string name;
    uint32_t offset;
    uint32_t size;
    int32_t archive_id; // -1: data embedded in the main file
    std::vector<uint8_t> preload{};
};

struct VPKFile {
    PyObject_HEAD
    std::filesystem::path m_path;
    PyObject *m_path_str;   // decoded path as str, for repr
    std::ifstream *m_stream;
    std::vector<VPKEntry> *m_entries;
    std::unordered_map<std::string, size_t> *m_index; // lowercased name -> entry index
    uint64_t m_embedded_data_start;
    // Native path prefix "<base>_" for "<base>_NNN.vpk" chunks; empty when the
    // file is not a "_dir.vpk" (external archives then cannot be resolved).
    std::filesystem::path::string_type m_chunked_base;
};

extern PyType_Spec VPKFile_class_spec;

// Registers VPKFile in `module` and creates the glob iterator type.
int VPK_InitTypes(PyObject *module);

// Reads an entry's bytes. `chunk_cache`/`chunk_cache_id` (optional) hold an
// open chunk stream reused across calls for the same archive index.
PyObject *get_entry_data(VPKFile *self, const VPKEntry &entry,
                         std::ifstream **chunk_cache = nullptr, int32_t *chunk_cache_id = nullptr);

#endif //PYLIB_VPK_CLASS_H

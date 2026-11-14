#include "classes/vpk_class.h"
#include "classes/vpk_glob_iterator.h"
#include "utils/utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static PyObject *VPKFile_new(PyTypeObject *type, PyObject *, PyObject *) {
    auto *self = (VPKFile *) PyType_GenericAlloc(type, 0);
    if (!self)
        return nullptr;
    // The object memory is raw (zero-filled); C++ members must be constructed
    // in place before use and destroyed explicitly in dealloc.
    try {
        new(&self->m_path) std::filesystem::path();
        new(&self->m_chunked_base) std::filesystem::path::string_type();
        // Containers always exist, so methods are safe even if __init__ never ran.
        self->m_entries = new std::vector<VPKEntry>;
        self->m_index = new std::unordered_map<std::string, size_t>;
    } catch (const std::bad_alloc &) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    return (PyObject *) self;
}

static void VPKFile_dealloc(VPKFile *self) {
    using path_t = std::filesystem::path;
    using native_t = std::filesystem::path::string_type;
    delete self->m_stream;
    delete self->m_entries;
    delete self->m_index;
    Py_XDECREF(self->m_path_str);
    self->m_path.~path_t();
    self->m_chunked_base.~native_t();
    PyTypeObject *tp = Py_TYPE((PyObject *) self);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))((PyObject *) self);
    Py_DECREF(tp);
}

// Pack struct to 1 byte
#pragma pack(push, 1)
struct VampirePakFooter {
    unsigned int file_count;
    unsigned int directory_offset; // Absolute from the start of the VPK
    unsigned char version; // Always 0
};


static std::string build_entry_name(const std::string &directory, const std::string &name, const std::string &extension);

#pragma pack(pop)

// Reads up to the next NUL (no length cap). Returns false if EOF/error is hit
// before the terminator, i.e. the directory is truncated.
static bool read_zero_terminated_string(std::ifstream &stream, std::string &out) {
    out.clear();
    std::getline(stream, out, '\0');
    // getline sets eofbit (without failbit) when the delimiter is missing.
    return stream && !stream.eof();
}

static std::string to_lower(const std::string &input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Builds a filesystem path from a str: wide chars on Windows (non-ASCII safe),
// filesystem-encoded bytes elsewhere (round-trips surrogateescape'd names).
static int path_from_str(PyObject *str, std::filesystem::path &out) {
#ifdef _WIN32
    Py_ssize_t len = 0;
    wchar_t *w = PyUnicode_AsWideCharString(str, &len);
    if (!w)
        return -1;
    try {
        out = std::filesystem::path(std::wstring(w, (size_t) len));
    } catch (...) {
        PyMem_Free(w);
        throw;
    }
    PyMem_Free(w);
#else
    PyObject *encoded = PyUnicode_EncodeFSDefault(str);
    if (!encoded)
        return -1;
    char *buf = nullptr;
    Py_ssize_t len = 0;
    if (PyBytes_AsStringAndSize(encoded, &buf, &len) < 0) {
        Py_DECREF(encoded);
        return -1;
    }
    try {
        out = std::filesystem::path(std::string(buf, (size_t) len));
    } catch (...) {
        Py_DECREF(encoded);
        throw;
    }
    Py_DECREF(encoded);
#endif
    return 0;
}

// "<base>_dir.vpk" (case-insensitive) -> native "<base>_"; otherwise empty.
static std::filesystem::path::string_type chunk_base_of(const std::filesystem::path &path) {
    using native_t = std::filesystem::path::string_type;
    const native_t &full = path.native();
    static const char suffix[] = "_dir.vpk";
    const size_t slen = sizeof(suffix) - 1;
    if (full.size() < slen)
        return {};
    for (size_t i = 0; i < slen; ++i) {
        auto c = full[full.size() - slen + i];
        if (c >= 'A' && c <= 'Z')
            c = (decltype(c)) (c - 'A' + 'a');
        if (c != (decltype(c)) suffix[i])
            return {};
    }
    return full.substr(0, full.size() - slen + 1); // keep the '_'
}

static int vpk_init_impl(VPKFile *self, PyObject *path_str) {
    // Reset state so re-running __init__ starts clean.
    delete self->m_stream;
    self->m_stream = nullptr;
    self->m_entries->clear();
    self->m_index->clear();
    self->m_chunked_base.clear();
    self->m_embedded_data_start = 0;

    if (path_from_str(path_str, self->m_path) < 0)
        return -1;
    Py_INCREF(path_str);
    {
        PyObject *old = self->m_path_str;
        self->m_path_str = path_str;
        Py_XDECREF(old);
    }
    self->m_stream = new std::ifstream(self->m_path, std::ios::binary | std::ios::in);
    if (!self->m_stream->is_open()) {
        PyErr_Format(PyExc_OSError, "Failed to open VPK file %R", path_str);
        return -1;
    }
    std::ifstream &stream = *self->m_stream;
    stream.seekg(0, std::ios::end);
    const auto end_pos = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (end_pos < 0) {
        PyErr_SetString(PyExc_OSError, "Failed to determine VPK file size");
        return -1;
    }
    const uint64_t file_size = (uint64_t) end_pos;

    uint32_t magic = 0;
    stream.read((char *) &magic, 4);
    if (!stream || magic != 0x55AA1234) {
        // Vampire: The Masquerade - Bloodlines layout: directory located by a
        // footer at the end. Every count/length below comes from the file and
        // is bounded by the bytes actually available before being trusted.
        stream.clear();
        if (file_size < sizeof(VampirePakFooter)) {
            PyErr_SetString(PyExc_ValueError, "Not a VPK file: too small");
            return -1;
        }
        stream.seekg(-(std::streamoff) sizeof(VampirePakFooter), std::ios::end);
        VampirePakFooter footer{};
        stream.read((char *) &footer, sizeof(VampirePakFooter));
        const uint64_t dir_end = file_size - sizeof(VampirePakFooter);
        if (!stream || footer.version != 0 || footer.directory_offset > dir_end) {
            PyErr_SetString(PyExc_ValueError, "Not a VPK file: invalid VtMB footer");
            return -1;
        }
        // Each entry is at least name_len + offset + size = 12 bytes.
        const uint64_t dir_bytes = dir_end - footer.directory_offset;
        if (footer.file_count > dir_bytes / 12) {
            PyErr_SetString(PyExc_ValueError, "Invalid VtMB VPK: file count exceeds directory size");
            return -1;
        }
        self->m_embedded_data_start = 0;
        stream.seekg(footer.directory_offset);
        uint64_t remaining = dir_bytes;
        self->m_entries->reserve(footer.file_count);
        for (uint32_t i = 0; i < footer.file_count; ++i) {
            uint32_t name_len = 0;
            stream.read((char *) &name_len, 4);
            if (!stream || remaining < 12 || name_len > remaining - 12) {
                PyErr_SetString(PyExc_ValueError, "Invalid VtMB VPK: truncated directory entry");
                return -1;
            }
            remaining -= 12 + (uint64_t) name_len;
            VPKEntry &entry = self->m_entries->emplace_back();
            entry.name.resize(name_len);
            stream.read(entry.name.data(), name_len);
            stream.read((char *) &entry.offset, 4);
            stream.read((char *) &entry.size, 4);
            if (!stream) {
                PyErr_SetString(PyExc_ValueError, "Invalid VtMB VPK: truncated directory entry");
                return -1;
            }
            // Registered *after* the read: inserting first recorded a string of NUL
            // bytes for every entry, so `check()` never matched in a VtMB VPK.
            entry.name = to_lower(entry.name);
            self->m_index->emplace(entry.name, self->m_entries->size() - 1);
            entry.archive_id = -1;
        }
    } else {
        self->m_chunked_base = chunk_base_of(self->m_path);

        uint16_t version[2];
        uint32_t tree_size;
        stream.read((char *) &version, 4);
        stream.read((char *) &tree_size, 4);
        if (!stream) {
            PyErr_SetString(PyExc_ValueError, "Invalid VPK: truncated header");
            return -1;
        }
        if (version[0] == 1) {
            stream.seekg(12);
        } else if (version[0] == 2 && version[1] == 0) {
            stream.seekg(12 + 16);
        } else if (version[0] == 2 && version[1] == 3) {
            stream.seekg(12 + 4);
        } else {
            PyErr_Format(PyExc_ValueError, "Unsupported VPK version %u.%u",
                         (unsigned) version[0], (unsigned) version[1]);
            return -1;
        }
        if (!stream) {
            PyErr_SetString(PyExc_ValueError, "Invalid VPK: truncated header");
            return -1;
        }
        const auto truncated = []() {
            PyErr_SetString(PyExc_ValueError, "Invalid VPK: truncated directory");
            return -1;
        };
        std::string type_name, directory_name, file_name;
        while (true) {
            if (!read_zero_terminated_string(stream, type_name))
                return truncated();
            if (type_name.empty())
                break;
            while (true) {
                if (!read_zero_terminated_string(stream, directory_name))
                    return truncated();
                if (directory_name.empty())
                    break;
                while (true) {
                    if (!read_zero_terminated_string(stream, file_name))
                        return truncated();
                    if (file_name.empty())
                        break;
                    auto entry_name = to_lower(build_entry_name(directory_name, file_name, type_name));

                    struct {
                        uint32_t crc;
                        uint16_t preload_size, archive_id;
                        uint32_t offset, size;
                    } entry{};
                    stream.read((char *) &entry, sizeof(entry));
                    if (!stream)
                        return truncated();
                    uint16_t terminator = 0;
                    stream.read((char *) &terminator, 2);
                    if (!stream)
                        return truncated();
                    if (terminator != 0xFFFF) {
                        PyErr_SetString(PyExc_ValueError, "Invalid VPK file: missing terminator");
                        return -1;
                    }
                    auto &vpk_entry = self->m_entries->emplace_back();
                    vpk_entry.archive_id = entry.archive_id == 0x7fff ? -1 : entry.archive_id;
                    if (entry.preload_size > 0) {
                        vpk_entry.preload.resize(entry.preload_size);
                        stream.read((char *) vpk_entry.preload.data(), entry.preload_size);
                        if (!stream)
                            return truncated();
                    }
                    self->m_index->emplace(entry_name, self->m_entries->size() - 1);
                    vpk_entry.name = std::move(entry_name);
                    vpk_entry.size = entry.size;
                    vpk_entry.offset = entry.offset;
                }
            }
        }
        const auto data_start = stream.tellg();
        if (data_start < 0) {
            PyErr_SetString(PyExc_OSError, "Failed to read VPK directory position");
            return -1;
        }
        self->m_embedded_data_start = (uint64_t) data_start;
    }

    return 0;
}

static int VPKFile_init(VPKFile *self, PyObject *args, PyObject *kwds) {
    static const char *kwlist[] = {"path", nullptr};
    PyObject *path = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&", const_cast<char **>(kwlist),
                                     PyUnicode_FSDecoder, &path))
        return -1;
    // Allocation failures must not escape as C++ exceptions (std::terminate).
    int rc;
    try {
        rc = vpk_init_impl(self, path);
    } catch (const std::bad_alloc &) {
        PyErr_NoMemory();
        rc = -1;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        rc = -1;
    }
    Py_DECREF(path);
    return rc;
}

static std::string build_entry_name(const std::string &directory, const std::string &name, const std::string &extension) {
    if (directory.at(0) == ' ') {
        return name + "." + extension;
    }
    if (extension.at(0) == ' ') {
        return directory + "/" + name;
    }
    if (name.at(0) == ' ') {
        auto tmp_name = name.substr(1);
        return directory + "/" + tmp_name + "." + extension;
    }
    return directory + "/" + name + "." + extension;
}

// Lowercased lookup key from a str argument (keeps embedded NULs).
static bool key_from_arg(PyObject *arg, const char *argname, std::string &out) {
    if (!PyUnicode_Check(arg)) {
        type_error(argname, "str", arg);
        return false;
    }
    Py_ssize_t len = 0;
    const char *s = PyUnicode_AsUTF8AndSize(arg, &len);
    if (!s)
        return false;
    out = to_lower(std::string(s, (size_t) len));
    return true;
}

static PyObject *VPKFile_find_file(VPKFile *self, PyObject *arg) {
    try {
        std::string key;
        if (!key_from_arg(arg, "name", key))
            return nullptr;
        const auto it = self->m_index->find(key);
        if (it == self->m_index->end())
            Py_RETURN_NONE;
        return get_entry_data(self, (*self->m_entries)[it->second]);
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
}

// "<base>_" + zero-padded archive index + ".vpk", in the native path encoding.
static std::filesystem::path format_vpk_chunk_path(const std::filesystem::path::string_type &base, int32_t archive_id) {
    char digits[32];
    std::snprintf(digits, sizeof(digits), "%03d.vpk", (int) archive_id);
    std::filesystem::path::string_type out = base;
    for (const char *p = digits; *p; ++p)
        out.push_back((std::filesystem::path::value_type) *p);
    return std::filesystem::path(std::move(out));
}

// Reads preload + body into a new bytes object; on a short read the stream
// state is reset so the shared stream stays usable for later lookups.
static PyObject *read_entry_bytes(std::ifstream &stream, uint64_t pos, const VPKEntry &entry) {
    stream.clear();
    stream.seekg((std::streamoff) pos);
    if (!stream) {
        stream.clear();
        PyErr_Format(PyExc_OSError, "Failed to seek to VPK entry '%s'", entry.name.c_str());
        return nullptr;
    }
    PyObject *data = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) entry.size + (Py_ssize_t) entry.preload.size());
    if (!data)
        return nullptr;
    char *buf = PyBytes_AsString(data);
    if (!entry.preload.empty())
        memcpy(buf, entry.preload.data(), entry.preload.size());
    stream.read(buf + entry.preload.size(), entry.size);
    if (stream.gcount() != (std::streamsize) entry.size) {
        stream.clear();
        Py_DECREF(data);
        PyErr_Format(PyExc_ValueError, "Truncated VPK data for entry '%s'", entry.name.c_str());
        return nullptr;
    }
    return data;
}

PyObject *get_entry_data(VPKFile *self, const VPKEntry &entry, std::ifstream **chunk_cache, int32_t *chunk_cache_id) {
    try {
        if (entry.archive_id == -1) {
            if (!self->m_stream) {
                PyErr_SetString(PyExc_ValueError, "VPKFile is not initialized");
                return nullptr;
            }
            return read_entry_bytes(*self->m_stream, self->m_embedded_data_start + (uint64_t) entry.offset, entry);
        }
        if (self->m_chunked_base.empty()) {
            PyErr_Format(PyExc_ValueError,
                         "VPK entry '%s' is stored in external archive %d, but the VPK path does not end "
                         "in '_dir.vpk' so the archive cannot be located",
                         entry.name.c_str(), (int) entry.archive_id);
            return nullptr;
        }
        if (chunk_cache && *chunk_cache && *chunk_cache_id == entry.archive_id)
            return read_entry_bytes(**chunk_cache, entry.offset, entry);

        const std::filesystem::path chunk_path = format_vpk_chunk_path(self->m_chunked_base, entry.archive_id);
        auto *chunk_stream = new std::ifstream(chunk_path, std::ios::binary | std::ios::in);
        if (!chunk_stream->is_open()) {
            delete chunk_stream;
            PyErr_Format(PyExc_OSError, "Failed to open archive %d for VPK entry '%s'",
                         (int) entry.archive_id, entry.name.c_str());
            return nullptr;
        }
        PyObject *result = read_entry_bytes(*chunk_stream, entry.offset, entry);
        if (chunk_cache) {
            delete *chunk_cache;
            *chunk_cache = chunk_stream;
            *chunk_cache_id = entry.archive_id;
        } else {
            delete chunk_stream;
        }
        return result;
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_OSError, e.what());
        return nullptr;
    }
}

static PyObject *VPKFile_glob(VPKFile *self, PyObject *arg) {
    PyObject *lower_pat;
    try {
        std::string lowered;
        if (!key_from_arg(arg, "pattern", lowered))
            return nullptr;
        lower_pat = PyBytes_FromStringAndSize(lowered.data(), (Py_ssize_t) lowered.size());
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
    if (!lower_pat)
        return nullptr;

    auto *it = (VPKGlobIter *) PyType_GenericAlloc(GlobIterType, 0);
    if (!it) {
        Py_DECREF(lower_pat);
        return nullptr;
    }
    Py_INCREF((PyObject *) self);
    it->owner = self;
    it->index = 0;
    it->pattern_obj = lower_pat;
    it->chunk = nullptr;
    it->chunk_id = -1;
    return (PyObject *) it;
}

static PyObject *VPKFile_check(VPKFile *self, PyObject *arg) {
    try {
        std::string key;
        if (!key_from_arg(arg, "name", key))
            return nullptr;
        return PyBool_FromLong(self->m_index->contains(key));
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
}

static PyObject *VPKFile_repr(VPKFile *self) {
    if (!self->m_path_str)
        return PyUnicode_FromFormat("VPKFile(path=None, entries=%zu)", self->m_entries->size());
    return PyUnicode_FromFormat("VPKFile(path=%U, entries=%zu)", self->m_path_str, self->m_entries->size());
}

PyDoc_STRVAR(VPKFile_find_file_doc,
             "find_file($self, name, /)\n"
             "--\n"
             "\n"
             "Return the contents of the file `name` (case-insensitive) as bytes, or None if\n"
             "the VPK has no such file. Raises OSError/ValueError if the data cannot be read.\n");

PyDoc_STRVAR(VPKFile_check_doc,
             "check($self, name, /)\n"
             "--\n"
             "\n"
             "Return True if the file `name` (case-insensitive) exists in the VPK, False otherwise.\n");

PyDoc_STRVAR(VPKFile_glob_doc,
             "glob($self, pattern, /)\n"
             "--\n"
             "\n"
             "Return an iterator of (name, data) tuples for files matching `pattern`.\n"
             "Matching follows fnmatch.fnmatchcase on the lowercased name and pattern:\n"
             "'*' matches any run of characters including '/', '?' one character,\n"
             "'[seq]'/'[!seq]' character classes.\n");

static PyMethodDef VPKFile_class_methods[] = {
        {"find_file", CPF(VPKFile_find_file), METH_O, VPKFile_find_file_doc},
        {"check",     CPF(VPKFile_check),     METH_O, VPKFile_check_doc},
        {"glob",      CPF(VPKFile_glob),      METH_O, VPKFile_glob_doc},
        {nullptr, nullptr, 0, nullptr}
};

static PyType_Slot VPKFile_class_slots[] = {
        {Py_tp_new,     (void *) VPKFile_new},
        {Py_tp_init,    (void *) VPKFile_init},
        {Py_tp_dealloc, (void *) VPKFile_dealloc},
        {Py_tp_repr,    (void *) VPKFile_repr},
        {Py_tp_methods, (void *) VPKFile_class_methods},
        {0,             nullptr}
};

PyType_Spec VPKFile_class_spec = {
        "pylib.VPKFile",
        sizeof(VPKFile),
        0,
        Py_TPFLAGS_DEFAULT,
        VPKFile_class_slots
};

int VPK_InitTypes(PyObject *module) {
    if (add_type(module, "VPKFile", &VPKFile_class_spec) < 0)
        return -1;
    if (!GlobIterType) {
        GlobIterType = (PyTypeObject *) PyType_FromSpec(&GlobIter_spec);
        if (!GlobIterType)
            return -1;
    }
    return 0;
}

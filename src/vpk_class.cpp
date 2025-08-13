#include "vpk_class.h"
#include "vpk_glob_iterator.h"
#include <format>

#if defined(_MSC_VER)
#include <shlwapi.h>
#else
#include <fnmatch.h>
#endif

PyObject *VPKFile_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    VPKFile *self;
    self = (VPKFile *) PyType_GenericAlloc(type, 0);
    return (PyObject *) self;
}

void VPKFile_dealloc(VPKFile *self) {
    delete self->m_stream;
    delete self->m_entries;
    freefunc(PyType_GetSlot(Py_TYPE(self), Py_tp_free))(self);
}

// Pack struct to 1 byte
#pragma pack(push, 1)
struct VampirePakFooter {
    unsigned int file_count;
    unsigned int directory_offset;    // Absolute from the start of the VPK
    unsigned char version;            // Always 0
};


std::string build_entry_name(const std::string &directory, const std::string &name, const std::string &extension);

#pragma pack(pop)

std::string read_zero_terminated_string(std::ifstream &stream) {
    char buffer[1024];
    size_t i = 0;
    for (; i < sizeof(buffer); ++i) {
        char c;
        if (!stream.get(c)) break;
        if (c == '\0') break;
        buffer[i] = c;
    }
    return {buffer, i};
}

int VPKFile_init(VPKFile *self, PyObject *args, PyObject *kwds) {
    static const char *kwlist[] = {"path", nullptr};
    const char *path = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s",  const_cast<char **>(kwlist), &path))
        return -1;

    self->m_path = std::string(path);
    self->m_stream = new std::ifstream(self->m_path, std::ios::binary | std::ios::in);
    self->m_entries = new std::vector<VPKEntry>;
    if (!self->m_stream->is_open()) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to open VPK file");
        return -1;
    }
    std::ifstream &stream = *self->m_stream;
    uint32_t magic;
    stream.read((char *) &magic, 4);
    if (magic != 0x55AA1234) {
        stream.seekg(0, std::ios::end);
        size_t file_size = stream.tellg();
        stream.seekg(-(int32_t) sizeof(VampirePakFooter), std::ios::end);
        VampirePakFooter footer{};
        stream.read((char *) &footer, sizeof(VampirePakFooter));
        if (footer.directory_offset > file_size) {
            return -1;
        }
        self->m_embedded_data_start = 0;
        stream.seekg(footer.directory_offset);
        for (int i = 0; i < footer.file_count; ++i) {
            VPKEntry &entry = self->m_entries->emplace_back();
            uint32_t name_len;
            stream.read((char *) &name_len, 4);
            entry.name.resize(name_len);
            stream.read(entry.name.data(), name_len);
            stream.read((char *) &entry.offset, 4);
            stream.read((char *) &entry.size, 4);
            entry.archive_id = -1;
        }
    } else {
        self->m_chunked_base = self->m_path.substr(0, self->m_path.size() - 7);

        uint16_t version[2];
        uint32_t tree_size;
        stream.read((char *) &version, 4);
        stream.read((char *) &tree_size, 4);
        if (version[0] == 1) {
            stream.seekg(12);
        } else if (version[0] == 2 && version[1] == 0) {
            stream.seekg(12 + 16);
        } else if (version[0] == 2 && version[1] == 3) {
            stream.seekg(12 + 4);
        }
        while (true) {
            auto type_name = read_zero_terminated_string(stream);
            if (type_name.empty())
                break;
            while (true) {
                auto directory_name = read_zero_terminated_string(stream);
                if (directory_name.empty())
                    break;
                while (true) {
                    auto file_name = read_zero_terminated_string(stream);
                    if (file_name.empty())
                        break;
                    auto entry_name = build_entry_name(directory_name, file_name, type_name);
                    struct {
                        uint32_t crc;
                        uint16_t preload_size, archive_id;
                        uint32_t offset, size;

                    } entry{};
                    stream.read((char *) &entry, sizeof(entry));
                    uint16_t terminator;
                    stream.read((char *) &terminator, 2);
                    if (terminator != 0xFFFF) {
                        PyErr_SetString(PyExc_RuntimeError, "Invalid VPK file: missing terminator");
                        return -1;
                    }
                    auto &vpk_entry = self->m_entries->emplace_back();
                    vpk_entry.archive_id = entry.archive_id == 0x7fff ? -1 : entry.archive_id;
                    if (entry.preload_size > 0) {
                        vpk_entry.preload.resize(entry.preload_size);
                        stream.read((char *) vpk_entry.preload.data(), entry.preload_size);
                    }
                    vpk_entry.name = std::move(entry_name);
                    vpk_entry.size = entry.size;
                    vpk_entry.offset = entry.offset;
                }
            }
        }
        self->m_embedded_data_start = stream.tellg();
    }

    return 0;
}

std::string build_entry_name(const std::string &directory, const std::string &name, const std::string &extension) {
    if (directory.at(0) == ' ') {
        return name + "." + extension;
    }
    if (extension.at(0) == ' ') {
        return directory + "/" + extension;
    }
    if (name.at(0) == ' ') {
        PyErr_SetString(PyExc_RuntimeError, "Invalid entry name: name starts with space");
    }
    return directory + "/" + name + "." + extension;
}

PyObject *VPKFile_find_file(VPKFile *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "find_file(name: str) takes exactly 1 argument");
        return nullptr;
    }
    if (!PyUnicode_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "name must be a str");
        return nullptr;
    }
    Py_ssize_t name_len;
    const char *name = PyUnicode_AsUTF8AndSize(args[0], &name_len);
    if (!name) {
        PyErr_SetString(PyExc_ValueError, "Invalid string");
        return nullptr;
    }

    for (const auto &entry: *self->m_entries) {
        if (entry.name == name) {
            return get_entry_data(self, entry);
        }
    }
    Py_RETURN_NONE;
}

PyObject *get_entry_data(VPKFile *self, const VPKEntry &entry) {
    if (entry.archive_id == -1) {
        self->m_stream->seekg((uint32_t) entry.offset);
        PyObject *data = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) (entry.size + entry.preload.size()));
        if (!data) {
            return nullptr;
        }
        if (!entry.preload.empty()) {
            memcpy(PyBytes_AsString(data), entry.preload.data(), entry.preload.size());
        }
        self->m_stream->read(PyBytes_AsString(data) + entry.preload.size(), entry.size);
        return data;
    } else {
        std::string chunk_path = std::format("{}{:03}.vpk", self->m_chunked_base, entry.archive_id);
        std::ifstream chunk_stream(chunk_path, std::ios::binary | std::ios::in);
        if (!chunk_stream.is_open()) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to open chunk file");
            return nullptr;
        }
        chunk_stream.seekg((uint32_t) entry.offset);
        PyObject *data = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) (entry.size + entry.preload.size()));
        if (!data) {
            chunk_stream.close();
            return nullptr;
        }
        if (!entry.preload.empty()) {
            memcpy(PyBytes_AsString(data), entry.preload.data(), entry.preload.size());
        }
        chunk_stream.read(PyBytes_AsString(data) + entry.preload.size(), entry.size);
        chunk_stream.close();
        return data;
    }
}

//PyObject *VPKFile_glob(VPKFile *self, PyObject *const *args, Py_ssize_t nargs) {
//    if (nargs != 1) {
//        PyErr_SetString(PyExc_TypeError, "glob(pattern: str) takes exactly 1 argument");
//        return nullptr;
//    }
//    if (!PyUnicode_Check(args[0])) {
//        PyErr_SetString(PyExc_TypeError, "pattern must be a str");
//        return nullptr;
//    }
//    Py_ssize_t pattern_len;
//    const char *pattern = PyUnicode_AsUTF8AndSize(args[0], &pattern_len);
//    if (!pattern) {
//        PyErr_SetString(PyExc_ValueError, "Invalid string");
//        return nullptr;
//    }
//
//    PyObject *result = PyList_New(0);
//    if (!result) {
//        return nullptr;
//    }
//
//    for (const auto &entry: *self->m_entries) {
//#if defined(_MSC_VER)
//        if (PathMatchSpecA(entry.name.c_str(), pattern)) {
//#else
//        if (fnmatch(pattern, entry.name.c_str(), 0) == 0) {
//#endif
//            PyObject *entry_data = get_entry_data(self, entry);
//            if (!entry_data) {
//                Py_DECREF(result);
//                return nullptr;
//            }
//            PyObject *entry_tuple = PyTuple_New(2);
//            if (!entry_tuple) {
//                Py_DECREF(entry_data);
//                Py_DECREF(result);
//                return nullptr;
//            }
//            PyTuple_SetItem(entry_tuple, 0, PyUnicode_FromString(entry.name.c_str()));
//            PyTuple_SetItem(entry_tuple, 1, entry_data);
//            if (PyList_Append(result, entry_tuple) < 0) {
//                Py_DECREF(entry_tuple);
//                Py_DECREF(result);
//                return nullptr;
//            }
//            Py_DECREF(entry_tuple);
//        }
//    }
//    return result;
//}


PyObject *VPKFile_glob(VPKFile *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "glob(pattern: str) takes exactly 1 argument");
        return nullptr;
    }
    if (!PyUnicode_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "pattern must be a str");
        return nullptr;
    }

    Py_ssize_t pat_len = 0;
    const char *pat_c = PyUnicode_AsUTF8AndSize(args[0], &pat_len);
    if (!pat_c) {
        PyErr_SetString(PyExc_ValueError, "invalid UTF-8");
        return nullptr;
    }

    if (EnsureGlobIterType() < 0)
        return nullptr;

// allocate iterator instance
    VPKGlobIter *it = (VPKGlobIter *) PyType_GenericNew((PyTypeObject *) GlobIter_TypeObj, nullptr, nullptr);
    if (!it) return nullptr;

    it->owner = self;
    Py_INCREF(self);
    it->index = 0;
    it->pattern_obj = (PyObject *) args[0];
    Py_INCREF(it->pattern_obj);
    it->pattern_c = pat_c;

    return (PyObject *) it;
}

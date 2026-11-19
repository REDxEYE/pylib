//
// pylib.studiomdl: declarations shared by geometry.cpp and flex.cpp (shapes, names the C++ API can't read back).
//

#ifndef PYLIB_STUDIOMDL_GEOMETRY_SHARED_H
#define PYLIB_STUDIOMDL_GEOMETRY_SHARED_H

#include "modules/studiomdl/bind.h"

namespace pysmdl {

// A shape of a geometry (the id is the order of Geometry.add_shape calls).
struct ShapeRef {
    smdl::Geometry geometry;
    uint32_t id;

    // Identity for equality / hashing / option keys: ids stay below 65536 (the format allows far fewer shapes)
    uintptr_t handle() const { return reinterpret_cast<uintptr_t>(geometry.handle()) * 65536u + id; }
};

// The C++ API has no getters for names (shapes, flexes, controllers, eyeballs, ...): they are remembered in the
// model's options dict under the key (kind, scope, id). Only plain values (str, tuples of numbers) are stored.
void store_value(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id, PyObject *value);
// Stores the value only when none is stored yet.
void store_value_default(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id, PyObject *value);
// The stored value (new reference), None when there is none.
PyObject *stored_value(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id);
// The id of the item stored with this string value.
bool find_stored_name(ModelObject *owner, const char *kind, uintptr_t scope, const std::string &name, uint32_t &id);

// A shape of `geometry` from a Shape, a shape name or a shape index; None is the base shape (smdl::NoId) when
// allow_base. Throws TypeError/ValueError/NotFoundError.
uint32_t shape_arg(ModelObject *owner, const smdl::Geometry &geometry, PyObject *object, const char *name,
                   bool allow_base);

} // namespace pysmdl

#endif //PYLIB_STUDIOMDL_GEOMETRY_SHARED_H

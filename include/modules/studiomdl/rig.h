//
// pylib.studiomdl: view values of the rig objects (rig.cpp), shared with animation.cpp, which takes them as arguments.
//

#ifndef PYLIB_STUDIOMDL_RIG_H
#define PYLIB_STUDIOMDL_RIG_H

#include "modules/studiomdl/bind.h"

namespace pysmdl {

// Tag of the Motion flag word (registered by animation.cpp)
struct MotionFlags {};

struct AttachmentRef {
    smdl::AttachmentId id;
};
struct HitboxSetRef {
    smdl::HitboxSetId id;
};
struct PoseParamRef {
    smdl::PoseParamId id;
};
struct IkChainRef {
    smdl::IkChainId id;
};
struct WeightListRef {
    smdl::WeightListId id;
};

template<>
inline uintptr_t view_key(const AttachmentRef &value) { return value.id; }
template<>
inline uintptr_t view_key(const HitboxSetRef &value) { return value.id; }
template<>
inline uintptr_t view_key(const PoseParamRef &value) { return value.id; }
template<>
inline uintptr_t view_key(const IkChainRef &value) { return value.id; }
template<>
inline uintptr_t view_key(const WeightListRef &value) { return value.id; }

// Arguments: the view of this model, an object name (str), or an id (int). Throws PyError.
smdl::AttachmentId attachment_arg(ModelObject *owner, PyObject *object, const char *name);
smdl::PoseParamId pose_param_arg(ModelObject *owner, PyObject *object, const char *name);
smdl::IkChainId ik_chain_arg(ModelObject *owner, PyObject *object, const char *name);
smdl::WeightListId weight_list_arg(ModelObject *owner, PyObject *object, const char *name);

// Views of ids (new reference); throws PyError.
PyObject *attachment_view(ModelObject *owner, smdl::AttachmentId id);
PyObject *pose_param_view(ModelObject *owner, smdl::PoseParamId id);

} // namespace pysmdl

#endif //PYLIB_STUDIOMDL_RIG_H

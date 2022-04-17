
#ifndef PY_HELPERS
#define PY_HELPERS

#define ADD_OBJECT(MOD, SUBMOD, SUBMOD_NAME)                                   \
  do {                                                                         \
    Py_XINCREF(MOD);                                                           \
    if (PyModule_AddObject(MOD, SUBMOD_NAME, SUBMOD) < 0) {                    \
      Py_XDECREF(SUBMOD);                                                      \
      Py_CLEAR(SUBMOD);                                                        \
      Py_DECREF(MOD);                                                          \
      return nullptr;                                                          \
    }                                                                          \
  } while (0)

#endif // PY_HELPERS

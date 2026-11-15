# libSceSystemService

## add Functional

- [sceLncUtilIsAppLaunched guest output](../../../core/libs/prx/libSceSystemService/Export.cpp) (libSceSystemService) - returning `LNC_UTIL_ERROR_INVALID_ARGUMENT` with caller errno preserved for an unwritable launched pointer is a host-safety fallback; console behavior on inaccessible output memory is unmeasured. The title ID input is still read directly and may fault if inaccessible.

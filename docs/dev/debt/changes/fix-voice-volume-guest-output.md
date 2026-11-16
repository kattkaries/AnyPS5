# libSceVoice

## add Functional

- [sceVoiceGetVolume](../../../core/libs/prx/libSceVoice/Export.cpp) (libSceVoice) - returning `InvalidArgument` (`0x804e0805`) for an unwritable non-null volume output via `GuestCopyOut_nid_no_patch` is a host-safety fallback, not measured on a console. The copy preserves host `errno`; initialization and port checks still precede output access. A failed boundary-crossing copy may write the accessible prefix.

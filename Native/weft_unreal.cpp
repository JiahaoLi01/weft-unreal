// What the DLL was built from, for the plugin's log: the weft commit ("+changes" when its checkout had uncommitted
// changes). Borrowed static text.
extern "C" const char* weft_unreal_build() {
    return WEFT_UNREAL_COMMIT;
}

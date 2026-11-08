//type:fc
//options:--c++17 --create_pch=hello.pch
//require:BACK_END_TYPE 3
// Precompiled headers are refused: the IL is streamed to the back end and
// freed as it goes, so there is nothing to save (--use_pch is refused too).
int main() { return 0; }

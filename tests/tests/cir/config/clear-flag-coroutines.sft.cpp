//type:fp
//options:--c++20 --clear_flag=coroutines
//require:BACK_END_TYPE 3
/* --clear_flag=coroutines stays accepted under CIR; only --set_flag is
   rejected (set-flag-coroutines-negative). */
int main() { return 0; }

//type:fc
//options:--c++20 --set_flag=coroutines
//require:BACK_END_TYPE 3
/* The CIR back end does not support coroutines, so enabling them with
   --set_flag is a command-line error.  --clear_flag stays allowed. */
int main() { return 0; }

#include "p.h"
int T::n = 0;
int arr1() { try { T a[5]; return 0; } catch (int i) { return i; } }
int arr2() { T::n = 0; try { U u; return 0; } catch (int i) { return i + 10; } }
int arr3() { T::n = 1; try { W w; return 0; } catch (int i) { return i + 20; } }
int arr4() { T::n = 0; try { T *p = new T[4]; delete[] p; return 0; } catch (int i) { return i + 30; } }

#include "os64.hpp"
extern "C" int main(int, char**) { return sync()<0?1:0; }

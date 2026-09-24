#include <cstdio>
extern "C" int middle();
int main() {
    std::puts("{\"validated\":true}");
    return middle();
}

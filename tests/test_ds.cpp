#include <iostream>

void test_bitmap();
void test_circular_list();
void test_hash_map();
void test_timing_wheel();

int main() {
    test_bitmap();
    test_circular_list();
    test_hash_map();
    test_timing_wheel();
    std::cout << "All data structure tests passed.\n";
    return 0;
}

#include <iostream>

void test_bitmap();
void test_circular_list();
void test_hash_map();

int main() {
    test_bitmap();
    test_circular_list();
    test_hash_map();
    std::cout << "All data structure tests passed.\n";
    return 0;
}

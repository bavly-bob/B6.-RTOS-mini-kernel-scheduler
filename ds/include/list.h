#pragma once 

class ListNode {
    int value;
    ListNode* next;
public:
    ListNode(int value) : value(value), next(nullptr) {}

    int getValue() const { return value; }
    ListNode* getNext() const { return next; }
    void setNext(ListNode* nextNode);
    void setValue(int newValue) ;
    void addNode(ListNode* newNode) ;
    void removeNode(ListNode* nodeToRemove);

}
#ifndef LINKEDLIST_H_
#define LINKEDLIST_H_
#include <stdio.h>
#include <stdlib.h>

struct Node {
    unsigned short data;
    struct Node* next;
    struct Node* prev;
};

void insertNode(struct Node **head, struct Node **tail, unsigned short item);
void deleteNode(struct Node **head, struct Node **tail, unsigned short key);
void circularInsertNode(struct Node **head, unsigned short item);
void circularDeleteNode(struct Node **head, unsigned short key);
void moveNodeToTop(struct Node **head, struct Node **tail, unsigned short key);
void printList(struct Node *head);
void printCircularList(struct Node *head);
void freeList(struct Node *head);

#endif
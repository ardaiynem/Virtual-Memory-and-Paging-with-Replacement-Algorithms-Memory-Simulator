#include "linkedList.h"

void insertNode(struct Node **head, struct Node **tail, unsigned short item)
{
    struct Node *newNode = (struct Node *)malloc(sizeof(struct Node));
    newNode->data = item;
    newNode->next = (*head);
    newNode->prev = NULL;

    if ((*head) != NULL)
    {
        (*head)->prev = newNode;
    }
    else
    {
        // If list is empty, update tail
        *tail = newNode;
    }

    (*head) = newNode;
}

void circularInsertNode(struct Node **head, unsigned short item)
{
    struct Node *newNode = (struct Node *)malloc(sizeof(struct Node));
    newNode->data = item;

    if (*head == NULL)
    {
        newNode->next = newNode;
        newNode->prev = newNode;
        *head = newNode;
    }
    else
    {
        struct Node *last = (*head)->prev;
        newNode->next = *head;
        newNode->prev = last;
        (*head)->prev = newNode;
        last->next = newNode;
        *head = newNode;
    }
}

void deleteNode(struct Node **head, struct Node **tail, unsigned short key)
{
    struct Node *temp = *head;

    // Head node case
    if (temp != NULL && temp->data == key)
    {
        *head = temp->next;
        if (*head != NULL)
        {
            (*head)->prev = NULL;
        }
        else
        {
            // If the deleted node was the last node, update tail
            *tail = NULL;
        }
        free(temp);
        return;
    }

    while (temp != NULL && temp->data != key)
    {
        temp = temp->next;
    }

    // Not found case
    if (temp == NULL)
    {
        printf("Key not found in the list.\n");
        return;
    }

    // Intermediate node
    if (temp->next != NULL)
    {
        temp->next->prev = temp->prev;
    }

    if (temp->prev != NULL)
    {
        temp->prev->next = temp->next;
    }

    // If the deleted node is the tail, update tail
    if (temp == *tail)
    {
        *tail = temp->prev;
    }

    free(temp);
}

void circularDeleteNode(struct Node **head, unsigned short key)
{
    if (*head == NULL)
    {
        printf("List is empty.\n");
        return;
    }

    struct Node *temp = *head;
    do
    {
        if (temp->data == key)
        {
            if (temp == *head && temp->next == temp)
            {
                free(temp);
                *head = NULL;
                return;
            }
            else if (temp == *head)
            {
                *head = temp->next;
            }

            temp->next->prev = temp->prev;
            temp->prev->next = temp->next;

            free(temp);
            return;
        }
        temp = temp->next;
    } while (temp != *head);

    printf("Key not found in the list.\n");
}

void moveNodeToTop(struct Node **head, struct Node **tail, unsigned short key)
{
    struct Node *temp = *head;

    // If already at the top or list is empty
    if (temp == NULL || temp->data == key)
    {
        return;
    }

    while (temp != NULL && temp->data != key)
    {
        temp = temp->next;
    }

    if (temp == NULL)
    {
        printf("Key not found in the list.\n");
        return;
    }

    // Remove from old position
    if (temp->next != NULL)
    {
        temp->next->prev = temp->prev;
    }

    if (temp->prev != NULL)
    {
        temp->prev->next = temp->next;
    }

    // If the node is the tail, update tail
    if (temp == *tail)
    {
        *tail = temp->prev;
    }

    // Add to top
    temp->next = *head;
    temp->prev = NULL;
    (*head)->prev = temp;
    *head = temp;
}

void printList(struct Node *head)
{
    printf("\nPrintin List\n\n");

    while (head != NULL)
    {
        printf("%d\n", head->data);
        head = head->prev;
    }
}

void printCircularList(struct Node *head)
{
    struct Node *startNode = head;
    printf("\nPrintin List\n\n");

    if (head == NULL)
    {
        return;
    }

    do
    {
        printf("%d\n", head->data);
        head = head->prev;
    } while (head != startNode);
}

void freeList(struct Node *head)
{
    struct Node *current = head;
    struct Node *next = NULL;

    while (current != NULL)
    {
        next = current->next;
        free(current);
        current = next;
    }
}
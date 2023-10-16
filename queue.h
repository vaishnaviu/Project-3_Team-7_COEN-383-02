#ifndef _queue_h_
#define _queue_h_

struct node {
	struct node *next;
	void *data;
};
typedef struct node Node;

struct queue {
	Node *front;
	Node *rear;
	int size;
};

typedef struct queue Queue;

Node* createNode(void *data);
void swapNodes(Node *a, Node *b);

// Queue Implementation //
Queue *createQueue();
void enqueue(Queue *q, void * data);
void *dequeue(Queue *q);
void sort(Queue *q, int (*cmp)(void *data1, void *data2));
void displayQueue(Queue *q);

#endif
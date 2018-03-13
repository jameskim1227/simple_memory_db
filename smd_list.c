#include <stdio.h>
#include <stdlib.h>

typedef struct smd_node {
	struct smd_node *priv;
	void *data;
	struct smd_node *next;
} smd_node;

typedef struct smd_list {
	smd_node *head;
	smd_node *tail;
	int length;
} smd_list;


smd_list* smd_create_list() {
	smd_list *list = (smd_list*)malloc(sizeof(smd_list));

	list->head = NULL;
	list->tail = NULL;
	list->length = 0;

	return list;
}

void smd_add_node(smd_list *list, void *data) {

	smd_node *new_node = (smd_node*)malloc(sizeof(smd_node));
	if (!new_node) {
		printf("Alloc memory error\n");
		return;
	}

	new_node->priv = NULL;
	new_node->next = NULL;
	// XXXX: alloc data ???
	new_node->data = data;

	
	// first node
	if (list->head == NULL) {
		list->head = new_node;
		list->tail = new_node;
		list->length++;

		return;
	}

	smd_node *cur_node = list->head;

	while (cur_node->next) {
		cur_node = cur_node->next;
	}
	
	cur_node->next = new_node;
	new_node->priv = cur_node;

	list->tail = new_node;

	list->length++;
}

//void smd_delete_node(smd_list *list, void *data) {

//}

void smd_print_all_nodes(smd_list *list) {
	smd_node *cur_node = list->head;

	while (cur_node) {
		printf("data: %s\n", cur_node->data);
		cur_node = cur_node->next;
	}
}

int main() {

	smd_list *list;

	list = smd_create_list();

	smd_add_node(list, "1");
	smd_add_node(list, "2");
	smd_add_node(list, "3");

	smd_print_all_nodes(list);

	//smd_delete_node(list, data);
	//smd_release_all_nodes(list);

	return 0;
}


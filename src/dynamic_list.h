#ifndef DYNAMIC_LIST_H
#define DYNAMIC_LIST_H

typedef struct {
    void* data;
    size_t count;
    size_t capacity;
    size_t element_size;
} dynamic_list_t;

dynamic_list_t create_dynamic_list(uint64_t element_size);
void list_push_element(dynamic_list_t* list, void* element);
void list_pop_element(dynamic_list_t* list);
void list_remove_element(dynamic_list_t* list,uint64_t index);
void list_destroy(dynamic_list_t* list);
void list_insert_element(dynamic_list_t* list,uint64_t index, void* element);
#endif
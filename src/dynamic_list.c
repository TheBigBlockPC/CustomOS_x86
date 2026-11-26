#include "types.h"
#include "vmm.h"
#include "util.h"
#include "dynamic_list.h"

#define MIN_CAPACITY 8

dynamic_list_t create_dynamic_list(uint64_t element_size){
    dynamic_list_t list;
    list.capacity = MIN_CAPACITY;
    list.element_size = element_size;
    list.count = 0;
    list.data = vm_alloc(element_size * MIN_CAPACITY,0,0);
    return list;
}

void list_push_element(dynamic_list_t* list, void* element){
    if(!list->data) return;
    if(list->count == list->capacity){
        list->capacity *= 2;
        list->data = vm_realloc(list->data,list->capacity * list->element_size);
    }
    memory_copy(list->data + (list->count * list->element_size),element,list->element_size);
    list->count++;
}

void list_pop_element(dynamic_list_t* list){
    if(!list->data) return;
    if(list->count == 0) return;
    list->count--;
    if(list->count < list->capacity/4 && list->capacity > MIN_CAPACITY){
        list->capacity /= 2;
        if(list->capacity < MIN_CAPACITY){
            list->capacity = MIN_CAPACITY;
        }
        list->data = vm_realloc(list->data,list->capacity * list->element_size);
    }
}

void list_remove_element(dynamic_list_t* list,uint64_t index){
    if(!list->data) return;
    if(index == list->count-1){
        list_pop_element(list);
        return;
    }
    if(index < 0 || index >= list->count){
        return;
    }
    void* dist = list->data + index * list->element_size;
    void* src = list->data + (index+1) * list->element_size;
    uint64_t n_bytes = (list->count - index -1) * list->element_size;
    memory_copy(dist,src,n_bytes);
    list_pop_element(list);
}

void list_insert_element(dynamic_list_t* list,uint64_t index, void* element){
    if(!list->data) return;
    if(index == list->count){
        list_push_element(list,element);
        return;
    }
    if(index < 0 || index > list->count){
        return;
    }
    if(list->count == list->capacity){
        list->capacity *= 2;
        list->data = vm_realloc(list->data,list->capacity * list->element_size);
    }

    for(int i=list->count-1;i>=index;i--){
        void* src = list->data + i * list->element_size;
        void* dest = list->data + (i+1) * list->element_size;
        memory_copy(dest,src,list->element_size);
    }
    memory_copy(
        list->data + index * list->element_size,
        element,
        list->element_size
    );
}

void list_destroy(dynamic_list_t* list){
    if(!list->data) return;
    vm_free(list->data);
    list->element_size = 0;
}
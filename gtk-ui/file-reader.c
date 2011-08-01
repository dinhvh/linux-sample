#include "file-reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "carray.h"

struct sample_node * read_file(char * filename)
{
  struct sample_node * root;
  char buffer[4096];
  unsigned int level;
  carray * stack;
  FILE * f;
  struct sample_node * current_node;
  
  f = fopen(filename, "r");
  if (f == NULL)
    return NULL;
  
  root = malloc(sizeof(* root));
  root->description = NULL;
  root->children = carray_new(16);
  current_node = root;
  stack = carray_new(16);
  level = 0;
  
  while (fgets(buffer, sizeof(buffer), f)) {
    struct sample_node * node;
    unsigned int count_space;
    unsigned int i;
    char * description;
    
    if (buffer[0] != ' ')
      continue;
    
    count_space = 0;
    while (buffer[count_space] == ' ')
      count_space ++;
    i = 0;
    while (buffer[i] != '\0') {
      if (buffer[i] == '\n') {
        buffer[i] = '\0';
        break;
      }
      i ++;
    }
    
    description = buffer + count_space;
    node = malloc(sizeof(* node));
    node->description = strdup(description);
    node->children = carray_new(16);
    
    if (count_space <= level) {
      unsigned int delta;
      
      delta = level - count_space;
      for(i = 0 ; i < delta ; i ++) {
        carray_delete(stack, carray_count(stack) - 1);
        level --;
      }
      current_node = carray_get(stack, carray_count(stack) - 1);
      carray_delete(stack, carray_count(stack) - 1);
      level --;
    }
    carray_add(current_node->children, node, NULL);
    
    carray_add(stack, current_node, NULL);
    current_node = node;
    level ++;
  }
  
  fclose(f);
  
  return root;
}

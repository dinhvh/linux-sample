#include "file-reader.h"
#include "main-window.h"

#include <stdlib.h>
#include <gtk/gtk.h>

int main(int argc, char ** argv)
{
  struct sample_node * node;
  char * filename;
  
  if (argc < 2) {
    fprintf(stderr, "Syntax: gtk-sample <filename>\n");
    exit(1);
  }
  
  filename = argv[1];
  node = read_file(filename);
  gtk_init_check(&argc, &argv);
  sample_main_window_init(node);
  gtk_main();
  
  exit(0);
}

#include "mysort.h"
#include <alloca.h>
#include <assert.h>
#include <string.h>
#include <cstdlib>

//
// Sort an array of element of any type
// it uses "compFunc" to sort the elements.
// The elements are sorted such as:
//
// if ascending != 0
//   compFunc( array[ i ], array[ i+1 ] ) <= 0
// else
//   compFunc( array[ i ], array[ i+1 ] ) >= 0
//
// See test_sort to see how to use mysort.
//
void swap (void* x, void* y, int size) {
  	
  void* tmp = (void *) malloc(size);
  memcpy(tmp, x, size);
  memcpy(x, y, size);
  memcpy(y, tmp, size);
  free(tmp);
}
void mysort( int n,                      // Number of elements
             int elementSize,            // Size of each element
             void * array,               // Pointer to an array
             int ascending,              // 0 -> descending; 1 -> ascending
             CompareFunction compFunc )  // Comparison function.
{
  // Add your code here. Use any sorting algorithm you want.
  if (ascending != 0) {
    int i, j; 
    for (i = 0; i < n-1; i++){
      for (j = 0; j < n-i-1; j++) {
        if (compFunc( ((char *)array + j*elementSize), ((char *)array + (j+1)*elementSize)) >= 0) {
          swap(((char *) array + j*elementSize) , ((char *) array + (j+1)*elementSize ) , elementSize);
        }
      }
    }      
     
  } else {
    int i, j; 
    for (i = 0; i < n-1; i++){
      for (j = 0; j < n-i-1; j++) {
        if (compFunc( ((char *)array + j*elementSize), ((char *)array + (j+1)*elementSize)) <=  0) {
          swap(((char *) array + j*elementSize) , ((char *) array + (j+1)*elementSize ) , elementSize);
        }
      }
    }      
  }
}

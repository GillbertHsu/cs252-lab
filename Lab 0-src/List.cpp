//
// Implement the List class
//

#include <stdio.h>
#include "List.h"
#include <cstdlib>
#include <assert.h>

void
List::insertSorted( int val )
{
  // Complete procedure
  ListNode* L = new ListNode();
  L->_value = val;
  L->_next = NULL;

  if (!_head) _head = L;
  else{

    if (val <= _head->_value) {
      L->_next = _head;
      _head = L;
    } else {

      ListNode* current = _head;
      ListNode* prev;
      
      if (current->_next != NULL){
        prev = current;
        current = current->_next;
      } else {
        prev = current;
      }
      
    
      while (val >= current->_value && current->_next != NULL) {
        prev = current;
        current = current->_next;
      }
      if ( val >= current->_value && current->_next == NULL) {
        current->_next = L;
      } else {
        prev->_next = L;
        L->_next = current;
      }
        
      }
      
    }

  }


//
// Inserts a new element with value "val" at
// the end of the list.
//
void
List::append( int val )
{
  ListNode* L = new ListNode();
  L->_value = val;
  L->_next = NULL;
  if (!_head) _head = L;
  else{
    ListNode* current = _head;
    while (current->_next != NULL) {
       current = current->_next;
    }
    current->_next = L;
  }

}

//
// Inserts a new element with value "val" at
// the beginning of the list.
//
void
List::prepend( int val )
{

  ListNode* L = new ListNode();
  L->_value = val;
  if (_head == NULL) {
    L->_next = NULL;
    _head = L;
  } else {
    L->_next = _head;
  }
  _head = L;
}

// Removes an element with value "val" from List
// Returns 0 if succeeds or -1 if it fails
int
List:: remove( int val )
{
  // Complete procedure
  ListNode* current = _head;

  if (current->_value == val) {
    _head = current->_next;
    delete current;
    return 0;
  }
  ListNode* prev = current;
  current = current->_next;

  while (current->_value != val) {
    prev = current;
    current = current->_next;
    if (current->_value != val && current->_next == NULL) {
      return -1;
    }
  }
  prev->_next = current->_next;
  delete current;

  return 0;
}

// Prints The elements in the list.
void
List::print()
{
  ListNode* current = _head;
  while (current != NULL) {
    printf("%d ",current->_value);
    current = current->_next;
  }
}

//
// Returns 0 if "value" is in the list or -1 otherwise.
//
int
List::lookup(int val)
{
  ListNode* current = _head;
  while (current!= NULL) {
    if (current->_value == val) {
      return 0;
    }
    current = current->_next;
  }
  return -1;
}

//
// List constructor
//
List::List()
{
  // Complete procedure
  _head = NULL;
}

//
// List destructor: delete all list elements, if any.
//
List::~List()
{
   ListNode* temp = new ListNode();
  
  
  while(_head != NULL) {
    temp = _head;
    _head = _head->_next;
    delete temp;
  }
   
}

int a[] = {45, 23, 78, 12, 100, 1, 100, 34, 90, 78 };

int
main( int argc, char ** argv )
{
  List l;
  int i;

  int nelements = sizeof( a )/ sizeof( int );
  for ( i = 0; i < nelements; i++ ) {
    l.insertSorted( a[ i ] );
  }

  printf("List after sorting...\n");

  l.print();

  // Make sure that list is sorted
  ListNode *n = l._head;
  while ( n && n->_next ) {
    assert( n->_value <= n->_next->_value );
    n = n->_next;
  }

  // remove elements from the list
  assert( l.remove( 34 ) == 0 );
  assert( l.remove( 34 ) == -1 );
  assert( l.remove( 95 ) == -1 );
  assert( l.remove( 100 ) == 0 );
  assert( l.remove( 100 ) == 0 );
  assert( l.remove( 100 ) == -1 );

  // Make sure that the other elements are still there
  assert ( l.lookup( 45) == 0 );
  assert ( l.lookup( 23) == 0 );
  assert ( l.lookup( 78) == 0 );
  assert ( l.lookup( 1) == 0 );
  assert ( l.lookup( 90) == 0 );
  assert ( l.lookup( 34) == -1 );
  assert ( l.lookup( 100) == -1 );
  assert ( l.lookup( 95) == -1 );
  assert ( l.lookup( 2) == -1 );
  assert ( l.lookup( 12) == 0 );

  printf(">>> test_listd!\n");
  exit( 0 );
}
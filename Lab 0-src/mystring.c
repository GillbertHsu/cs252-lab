#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>

char *mystrcpy(char * s1, const char * s2) {
    int len = 0;
    while (*s1 != '\0') {
        len++;
        *s1++ = *(char*)"";
    }
    s1 = s1 - len;
    char *temp = s1;
    while (*s2 != '\0'){
        *temp++ = *s2++;
    }
    *temp = '\0';
    return temp;
}

size_t mystrlen(const char *s)
{
  int i;
  for (i = 0; s[i] != '\0'; i++) {
  }
  return i;

}

char *mystrdup(const char *s1)
{
  char * des;
  des = (char*)malloc(sizeof(s1));

  for (int i = 0; s1[i] != '\0'; i++) {
        des[i] = s1[i];
  }
    return (char *) des;
}

char *mystrcat(char * s1, const char * s2)
{
  char *temp = s1;
  while (*temp != '\0'){
      temp++;
  } 
  while (*s2 != '\0') {
      *temp++ = *s2++;
  }
  *temp = '\0';
  return s1;
}

char *mystrstr(char * s1, const char * s2)
{
  char *sub = (char *) s1;
  char *temp1, *temp2;

  if (*s2 == ' '){
      return (char *) s1;
  }
  

  while (*sub) {
    temp1 = sub;
    temp2 = (char *) s2;

    while (*temp1 != '\0' && *temp2 != '\0' && (*temp1 - *temp2) == 0) {
        temp1++, temp2++;
    }
    if (*temp2 == '\0'){
        return sub;
    } 
    sub++;
  }

  return NULL;
}

int mystrcmp(const char *s1, const char *s2) {
  
  int dif = 0;
  int len = mystrlen(s1) > mystrlen(s2) ? mystrlen(s1) : mystrlen(s2);

  while (--len && *s1 && *s1 == *s2) {
      s1++;
      s2++;
  }
  dif = *s1 - *s2;
  return dif;
}


int main() {
  char str[100] = "abcdefg";
  int a = mystrlen(str);
  printf("%d", a);
  return 0;
}

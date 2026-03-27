#include <stdio.h>
#include <stdlib.h>
#include "../include/flexql.h"
int cb(void*,int n,char**v,char**c){for(int i=0;i<n;i++)printf("%s=%s\n",c[i],v[i]?v[i]:"NULL");printf("\n");return 0;}
int main(){
  FlexQL*db=NULL;char*err=NULL;
  if(flexql_open("127.0.0.1",9002,&db)!=0){printf("CONNECT FAIL\n");return 1;}
  printf("Connected\n");
  flexql_exec(db,"CREATE TABLE STUDENT(ID INT PRIMARY KEY NOT NULL,NAME TEXT NOT NULL)",NULL,0,&err);
  if(err){printf("E:%s\n",err);flexql_free(err);err=NULL;}
  flexql_exec(db,"INSERT INTO STUDENT VALUES(1,'Alice')",NULL,0,&err);
  if(err){printf("E:%s\n",err);flexql_free(err);err=NULL;}
  flexql_exec(db,"INSERT INTO STUDENT VALUES(2,'Bob')",NULL,0,&err);
  if(err){printf("E:%s\n",err);flexql_free(err);err=NULL;}
  printf("-- SELECT * --\n");
  flexql_exec(db,"SELECT * FROM STUDENT",cb,NULL,&err);
  if(err){printf("E:%s\n",err);flexql_free(err);err=NULL;}
  printf("-- WHERE ID=2 --\n");
  flexql_exec(db,"SELECT * FROM STUDENT WHERE ID = 2",cb,NULL,&err);
  if(err){printf("E:%s\n",err);flexql_free(err);err=NULL;}
  printf("Done\n");
  flexql_close(db);return 0;
}

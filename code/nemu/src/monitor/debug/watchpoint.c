#include "monitor/watchpoint.h"
#include "monitor/expr.h"

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head, *free_;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = &wp_pool[i + 1];
    wp_pool[i].value = 0;
  }
  wp_pool[NR_WP - 1].next = NULL;

  head = NULL;
  free_ = wp_pool;
}

/* TODO: Implement the functionality of watchpoint */
bool new_wp(char* exp)
{
  if(exp==NULL)
  {
    printf("No expression(in new_wp)\n");
    assert(0);	
  }
  if(free_==NULL)
  {
  	printf("No empty watchpoint\n");
  	return false;
  }
  WP* new_wp=free_;
  free_=free_->next;
  new_wp->next=NULL;
  bool success;
  uint32_t result=expr(exp,&success);
  strcpy(new_wp->expr,exp);
  new_wp->value=result;
  
  if(head!=NULL)
  {
  	WP* temp=head;
  	while(temp->next!=NULL)
  	{
  		temp=temp->next;
  	}
  	temp->next=new_wp;
  	new_wp->NO=temp->NO+1;
  }
  else
  {
  	head=new_wp;
  	new_wp->NO=1;
  }
  printf("Hardware watchpoint %d: %s\n",new_wp->NO,new_wp->expr);
  return true;
}

bool free_wp(int no)
{
  if(head==NULL)
  {
  	printf("No watchpoint to delete\n");
  	return false;
  }
  WP* delete_wp=head;
  while(delete_wp->NO!=no)
  	delete_wp=delete_wp->next;
  if(delete_wp==head)
  	head=head->next;
  else if(delete_wp==NULL)
  {
  	printf("No No.%d watchpoint in pool\n",no);
  	return false;
  }
  else
  {
  	WP* temp=head;
  	while(temp->next!=delete_wp)
  		temp=temp->next;
  	temp->next=delete_wp->next;
  }
  
	if(delete_wp!=NULL)
	{
		delete_wp->next=free_;
		free_=delete_wp;
		return true;
	}
	return false;
}

bool monitor_wppool_change()
{
	if(head==NULL)
		return true;
	WP* temp=head;
	bool success;
	uint32_t result;
	while(temp!=NULL)
	{
		result=expr(temp->expr,&success);
		//printf("result:%d\n",result);
		if(result!=temp->value)
		{
			printf("Hardware watchpoint %d: %s\n",temp->NO,temp->expr);
			printf("Old Value = %d\n",temp->value);
			printf("New Value = %d\n",result);
			temp->value=result;
			return false;
		}
		temp=temp->next;
	}
	return true;
}

void print_watchpoint_status()
{
	if(head==NULL)
	{
		printf("No watchpoint in pool\n");
		return;
	}
	WP* temp=head;
	printf("NO.     expression\n");
	while(temp!=NULL)
	{
		printf("%d      %s\n",temp->NO,temp->expr);
		temp=temp->next;
	}
}

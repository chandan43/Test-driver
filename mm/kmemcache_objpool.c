/*----Object pool---*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>

#define CACHE_NAME "mycache_objpool"

static struct kmem_cache *mycache_objpool;
void *object;

typedef struct {
	int a;
	int b;
	int c;
}private_data;

private_data *handle; 

/*Constructor */

static void cache_init(void *data){
	private_data *mydata;
	mydata=(private_data *)data;
	mydata->a=0;
	mydata->b=0;
	mydata->c=0;
}
static private_data *myalloc(void){
	private_data *mydata;
	mydata=(private_data *)kmem_cache_alloc(mycache_objpool,GFP_KERNEL); 
	return mydata;
}
static void  myfree(private_data *free){
	kmem_cache_free(mycache_objpool,free); /*Free cache */
	pr_info("%s: My free invoked\n",__func__);
}
int kmemcache_init(void){
	mycache_objpool=kmem_cache_create(CACHE_NAME,sizeof(private_data),0,SLAB_HWCACHE_ALIGN,cache_init); 
	/*Craete cache P1: Name P2: Size P3: Alignment for each block by defalut ZERO,FLAG,Constructor*/
	if(!mycache_objpool)
		return -ENOMEM;
	handle=myalloc();
	return 0;
}

void kmemcache_exit(void){
	myfree(handle);/*Free cache */
	kmem_cache_destroy(mycache_objpool);    /*Destroy Cache*/
	pr_info("%s: Cache Destroy successful\n",__func__);
}

module_init(kmemcache_init);
module_exit(kmemcache_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("Kmem_cache Example-->Object pool");

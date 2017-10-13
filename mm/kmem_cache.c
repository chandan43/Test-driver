/*----Memory pool---*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>

#define CACHE_NAME "mycache"

static struct kmem_cache *mycache;
void *object;
int kmemcache_init(void){
	mycache=kmem_cache_create(CACHE_NAME,128,0,SLAB_RED_ZONE,NULL); /*Craete cache P1: Name P2: Size P3: Alignment for each block by defalut ZERO,FLAG,Constructor*/
	if(!mycache)
		return -ENOMEM;
	object=kmem_cache_alloc(mycache,GFP_KERNEL);  /*allocate a cache*/
	if(!object)
		return -ENOMEM;
	return 0;
}

void kmemcache_exit(void){
	kmem_cache_free(mycache,object); /*Free cache */
	kmem_cache_destroy(mycache);    /*Destroy Cache*/
	pr_info("%s: Cache Destroy successful\n",__func__);
}

module_init(kmemcache_init);
module_exit(kmemcache_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("cjha@gmail.com");
MODULE_DESCRIPTION("Kmem_cache Example-->Memory pool");

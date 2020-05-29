#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rwsem.h>

#define BLOCK_SIZE 4
#define BLOCK_NUM 100
#define SND_CACHE_NUM 50

typedef struct {
	char * object;
	char * tag;
	int index;
	struct list_head list;
} CACHE;

typedef struct {
	char * tag;
	int usage;
	struct list_head list;
} TAG;

LIST_HEAD(fst_level_cache_list);
CACHE snd_level_cache_list[SND_CACHE_NUM];

DECLARE_RWSEM(cache_rwsem);
static LIST_HEAD(tag_list);

CACHE* initialize_cache(CACHE* cache)
{
	cache->object = (char*)kzalloc(sizeof(char)*50, GFP_KERNEL);
	cache->tag = (char*)kzalloc(sizeof(char), GFP_KERNEL);
	cache->index = -1;
	INIT_LIST_HEAD(&cache->list);
	return cache;
}

TAG* lookup_cache_tag(const char * target_tag)
{
	TAG* tag;

	down_read(&cache_rwsem);
	list_for_each_entry(tag, &tag_list, list) {
		if(strcmp(tag->tag, target_tag) == 0) {
			up_read(&cache_rwsem);
			return tag;
		}
	}
	up_read(&cache_rwsem);
	return NULL;
}

int _cache_add(CACHE* cache, const char* target_tag)
{
	TAG* tag;
	TAG* tag_entry;
	int block_usage = 0;

	tag = lookup_cache_tag(target_tag);
	
	if(!tag)
		goto notag;
	
	down_read(&cache_rwsem);
	list_for_each_entry(tag_entry, &tag_list, list) {
		if(strcmp(tag_entry->tag, tag->tag)==0){
			block_usage++;
		}
	}
	up_read(&cache_rwsem);

	down_write(&cache_rwsem);
	cache->tag = tag->tag;
	cache->index = block_usage;
	list_add(&cache->list, &fst_level_cache_list);
	tag->usage++;
	up_write(&cache_rwsem);
	return 0;

notag:
	printk(KERN_INFO "There's no such tag\n");
	return -1;

}

void cache_add(CACHE* cache)
{
	TAG* tag;

	down_read(&cache_rwsem);
	list_for_each_entry(tag, &tag_list, list){
		if(tag->usage < 4){
			if(_cache_add(cache, tag->tag) < 0)
				goto failadd;
		}
	}
	printk(KERN_INFO "There isn't any empty block\n");
	up_read(&cache_rwsem);
failadd:
	printk(KERN_INFO "ADD CACHE FAIL\n");
}

void cache_withdraw(CACHE* cache)
{
	TAG* tag;
	int i;

	down_read(&cache_rwsem);
	list_for_each_entry(tag, &tag_list, list){
		if(strcmp(cache->tag, tag->tag)){
			break;
		}
	}
	for(i = 0; i < SND_CACHE_NUM; i++)
	{
		if(strcmp(snd_level_cache_list[i].tag, cache->tag)==0&&snd_level_cache_list[i].index == cache->index)
			break;
	}
	up_read(&cache_rwsem);

	down_write(&cache_rwsem);
	tag->usage--;
	kfree(cache->object);
	kfree(cache->tag);
	snd_level_cache_list[i].index = -1;
	snd_level_cache_list[i].tag[0] = 0;
	list_del(&cache->list);
	kfree(cache);
	up_write(&cache_rwsem);
}

void cache_add_snd(CACHE* cache)
{
        int count = 0;
        int i;

        down_read(&cache_rwsem);
        for(i = 0; i < SND_CACHE_NUM; i++)
                if(snd_level_cache_list[i].index>=0)
                        count++;
        up_read(&cache_rwsem);

        down_write(&cache_rwsem);
        if(count >= SND_CACHE_NUM)
        {
                snd_level_cache_list[0].index = -1;
                snd_level_cache_list[0].tag[0] = 0;
        }

        for(i = 0; i < SND_CACHE_NUM; i++)
                if(snd_level_cache_list[i].index == -1)
                {
                        strcpy(snd_level_cache_list[i].object, cache->object);
                        strcpy(snd_level_cache_list[i].tag, cache->tag);
                        snd_level_cache_list[i].index = cache->index;
                }
        up_write(&cache_rwsem);
}

void refer_cache(CACHE* cache)
{
	CACHE* fst_cache;
	int i;

	down_read(&cache_rwsem);
	for(i = 0; i < SND_CACHE_NUM; i++)
		if(snd_level_cache_list[i].index == cache->index && strcmp(cache->tag, snd_level_cache_list[i].tag)==0)
		{
			printk(KERN_INFO "%s was looked up in CACHE LEVEL 2\n", cache->object);
			up_read(&cache_rwsem);
			return;
		}

	list_for_each_entry(fst_cache, &fst_level_cache_list, list){
		if(strcmp(fst_cache->tag, cache->tag)==0&& fst_cache->index == cache->index){
			printk(KERN_INFO "%s was looked up in CACHE LEVEL 1\n", cache->object);
			cache_add_snd(cache);
			up_read(&cache_rwsem);
			return;
		}
	}
	printk(KERN_INFO "There is no such cache\n");
	up_read(&cache_rwsem);
}

int cache_init(void)
{
	CACHE* cache;
	int i, count = 0;
	printk(KERN_INFO "MODULE IS INITIALIZED\n");

	for(i = 0; i < BLOCK_NUM; i++)
	{
		TAG* tag;
		printk(KERN_INFO "NOW TAGS ARE INCLUDING\n");
		
		tag = (TAG*)kzalloc(sizeof(TAG), GFP_KERNEL);
		tag->tag[0] = i+1;
		tag->usage = 0;
		INIT_LIST_HEAD(&tag->list);
		list_add(&tag->list, &tag_list);
	}
	printk(KERN_INFO "ALL TAGS ARE ADDED SUCCESSFULLY\n");
	for(i = 0; i < BLOCK_SIZE* BLOCK_NUM; i++)
	{
		CACHE* new_cache = (CACHE*)kzalloc(sizeof(CACHE), GFP_KERNEL);
		char object[50];
		new_cache = initialize_cache(new_cache);
		sprintf(object, "(CACHE %d)", i);
		strcpy(new_cache->object, object);
		cache_add(new_cache);
	}
	printk(KERN_INFO "ALL CACHES ARE ADDED SUCCESSFULLY\n");
	list_for_each_entry(cache, &fst_level_cache_list, list){
		count++;
		if(count%10 == 0)
			refer_cache(cache);
	}
	printk(KERN_INFO "SOME CACHES ARE REFERRED\n");
	list_for_each_entry(cache, &fst_level_cache_list, list){
               refer_cache(cache);
        }
	printk(KERN_INFO "CACHE_INIT RUNS SUCCESSFULLY\n");
	return 0;
}

void cache_exit(void)
{
	CACHE* cache;
	printk(KERN_INFO "MODULE IS EXITING\n");

	list_for_each_entry(cache, &fst_level_cache_list, list){
		cache_withdraw(cache);
	}
	printk(KERN_INFO "MODULE IS EXITED SUCCESSFULLY\n");
}

module_init(cache_init);
module_exit(cache_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSCACHE IMPLEMENTATION");
MODULE_AUTHOR("SGG");

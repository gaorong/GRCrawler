#include <dlfcn.h>
#include "dso.h"
#include "spider.h" 
#include "qstring.h"

/* The modules in this queue are used before pushing Url object into surl_queue */
vector<Module *> modules_pre_surl;  
/* The modules in this queue are used after parsing out http header */
vector<Module *> modules_post_header;
/* The modules in this queue are used after finishing read html */
vector<Module *> modules_post_html;

Module * dso_load(const char *path, const char *name)
{
    void *rv = NULL;
    void *handle = NULL;
    Module *module = NULL;

		//自定义函数，拼接多个字符串
    char * npath = strcat2(3, path, name, ".so");

    if ((handle = dlopen(npath, RTLD_GLOBAL | RTLD_NOW)) == NULL) {	
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "Load module fail(dlopen): %s", dlerror());
    }
    if ((rv = dlsym(handle, name)) == NULL) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "Load module fail(dlsym): %s", dlerror());
    }
    
    module = (Module *)rv;
    module->init(module);


    //SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Load module done : %s", name);
    return module;
}

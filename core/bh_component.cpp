/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the
GNU Lesser General Public License along with Bohrium.

If not, see <http://www.gnu.org/licenses/>.
*/

#include <bh.h>
#include <string.h>
#include <assert.h>
#include <iostream>
#include <sstream>
#include <string>
#include <boost/algorithm/string.hpp>

#ifdef _WIN32

#include <windows.h>
#include <dlfcn-win32.h>

#define HOME_INI_PATH "%APPDATA%\\bohrium\\config.ini"
#define SYSTEM_INI_PATH_1 "%PROGRAMFILES%\\bohrium\\config.ini"
#define SYSTEM_INI_PATH_2 "%PROGRAMFILES%\\bohrium\\config.ini"

//We need a buffer for path expansion
char _expand_buffer1[MAX_PATH];
char _expand_buffer2[MAX_PATH];
char _expand_buffer3[MAX_PATH];

//Nasty function renaming
#define snprintf _snprintf
#define strcasecmp _stricmp

#else

#include <dlfcn.h>
#include <limits.h>

#define HOME_INI_PATH "~/.bohrium/config.ini"
#define SYSTEM_INI_PATH_1 "/usr/local/etc/bohrium/config.ini"
#define SYSTEM_INI_PATH_2 "/usr/etc/bohrium/config.ini"

//We need a buffer for path expansion
char _expand_buffer[PATH_MAX];

#endif

// Check whether the given component exists.
static int component_exists(dictionary *dict, const char *name)
{
    char tmp[BH_COMPONENT_NAME_SIZE];
    snprintf(tmp, BH_COMPONENT_NAME_SIZE, "%s:type", name);
    char *s = iniparser_getstring(dict, tmp, NULL);
    return (NULL != s);
}

//Return the component type of the component named 'name'
static bh_component_type get_type(dictionary *dict, const char *name)
{
    char tmp[BH_COMPONENT_NAME_SIZE];
    snprintf(tmp, BH_COMPONENT_NAME_SIZE, "%s:type", name);
    char *s = iniparser_getstring(dict, tmp, NULL);
    if(s == NULL)
    {
        fprintf(stderr,"In section \"%s\" type is not set. "\
                       "Should be bridge, filter, fuser, vem or ve.\n",name);
        return BH_COMPONENT_ERROR;
    }
    else
    {
        if(!strcasecmp(s, "bridge"))
            return BH_BRIDGE;
        if(!strcasecmp(s, "vem"))
            return BH_VEM;
        if(!strcasecmp(s, "ve"))
            return BH_VE;
        if(!strcasecmp(s, "filter"))
            return BH_FILTER;
        if(!strcasecmp(s, "fuser"))
            return BH_FUSER;
        if(!strcasecmp(s, "stack"))
            return BH_STACK;
    }
    fprintf(stderr,"In section \"%s\" type is unknown: \"%s\" \n", name, s);
    return BH_COMPONENT_ERROR;
}

static void *get_dlsym(void *handle, const char *name,
                       bh_component_type type, const char *fun)
{
    if (!((type == BH_BRIDGE) || (type == BH_VEM) || (type == BH_VE) || 
          (type == BH_FILTER) || (type == BH_FUSER))) {
        fprintf(stderr, "Internal error get_dlsym() got unknown type\n");
        return NULL;
    }

    char tmp[1024];
    snprintf(tmp, BH_COMPONENT_NAME_SIZE, "bh_%s_%s", name, fun);
    dlerror(); //Clear old errors.
    void* ret = dlsym(handle, tmp);
    char *err = dlerror();
    if (err != NULL) {
        fprintf(stderr, "Failed to load %s() from %s (%s).\n"
                        "Make sure to define all four interface functions, eg. the NODE-VEM "
                        "must define: bh_vem_node_init(), bh_vem_node_shutdown(), "
                        "bh_vem_node_reg_func(), and bh_vem_node_execute().\n", fun, name, err);
        return NULL;
    }

    return ret;
}

/**
 *  Extract component symbol from the given string.
 *  
 *  Expects a string like: "anythinglibbh_COMPONENTNAME.anything"
 *  The returned pointer must be freed.
 *
 *  No error-checking in this thing...
 *
 */
char* get_component_symbol(const char* source)
{
    const char* prefix = "libbh_";                          // Component prefix
    int prefix_len = strlen(prefix);

    const char* filename = strstr(source, prefix);          // Filename
    filename += prefix_len;                                 // Skip the prefix
    int filename_len = strlen(filename);

    char* match = (char*)malloc(sizeof(char)*filename_len); // Allocate symbol
    strncpy(match, filename, filename_len);

    for(int i=0; (match[i]!='\0') || (i>=(filename_len-1)); ++i) {
        if (match[i] == '.') {                              // Terminate at extension
            *(match+i) = '\0';
            break;
        }
    }
    return match;
}

/* Initilize children of the given component
 *
 * @self   The component of which children will be initialized
 * @stack  The stack configuration to use, if NULL use children-chaining.
 * @return Error codes (BH_SUCCESS, BH_ERROR)
 */
static bh_error component_children_init(bh_component *self, char* stack)
{
    char tmp[BH_COMPONENT_NAME_SIZE];
    if (stack) {
        snprintf(tmp, BH_COMPONENT_NAME_SIZE, "%s:%s", stack, self->name);
    } else {
        snprintf(tmp, BH_COMPONENT_NAME_SIZE, "%s:children", self->name);
    }
    char *children_str = iniparser_getstring(self->config, tmp, NULL);
    if(children_str == NULL)
        return BH_SUCCESS;//No children -- we are finished

    //Handle one child at a time.
    char *child_str = strtok(children_str,",");
    self->nchildren = 0;
    while(child_str != NULL)
    {
        bh_component_iface *child       = &self->children[self->nchildren];
        bh_component_type child_type    = get_type(self->config,child_str);
        if (child_type == BH_COMPONENT_ERROR) {
            return BH_ERROR;
        }

        // Store component name
        strncpy(child->name, child_str, BH_COMPONENT_NAME_SIZE);

        if (!iniparser_find_entry(self->config,child_str)) {
            fprintf(stderr,"Reference \"%s\" is not declared.\n",child_str);
            return BH_ERROR;
        }

        // Path to implemention object
        snprintf(tmp, BH_COMPONENT_NAME_SIZE, "%s:impl", child_str);
        char *impl = iniparser_getstring(self->config, tmp, NULL);
        if (impl == NULL) {
            fprintf(stderr,"in section \"%s\" impl is not set.\n",child_str);
    	    return BH_ERROR;
        }
        void *lib_handle = dlopen(impl, RTLD_NOW);
        if (lib_handle == NULL) {
            fprintf(stderr, "Error in [%s:impl]: %s\n", child_str, dlerror());
    	    return BH_ERROR;
        }

        // Load interface functions
        char* component_name = get_component_symbol(impl);
        child->init = (bh_init)get_dlsym(lib_handle, component_name, child_type, "init");
        if (child->init == NULL) {
            return BH_ERROR;
        }
        child->shutdown = (bh_shutdown)get_dlsym(lib_handle, component_name, child_type, "shutdown");
        if (child->shutdown == NULL) {
            return BH_ERROR;
        }
        child->execute = (bh_execute)get_dlsym(lib_handle, component_name, child_type, "execute");
        if (child->execute == NULL) {
            return BH_ERROR;
        }

        child->extmethod = (bh_extmethod)get_dlsym(lib_handle, component_name, child_type, "extmethod");
        if (child->extmethod == NULL) {
            return BH_ERROR;
        }
        free(component_name);

        if (++self->nchildren > BH_COMPONENT_MAX_CHILDS) {
            fprintf(stderr,"Number of children of %s is greater "
                           "than BH_COMPONENT_MAX_CHILDS.\n",self->name);
            return BH_ERROR;
        }

        //Go to next child
        child_str = strtok(NULL,",");
    }
    return BH_SUCCESS;
}

/** 
 * Find configuration file
 *
 */
bh_error bh_component_config_find(bh_component *self)
{
    const char* homepath = HOME_INI_PATH;
    const char* syspath1 = SYSTEM_INI_PATH_1;
    const char* syspath2 = SYSTEM_INI_PATH_2;
   
    //
    // Find the configuration file
    //

    // Start by looking a path set via environment variable.
    const char *env = getenv("BH_CONFIG");
    if (env != NULL)
    {
        FILE *fp = fopen(env,"r");
        if( fp )
            fclose(fp);
        else
            env = NULL;//Did not exist.
    }

    // Then the home directory.
    if(env == NULL)
    {
#if _WIN32
        DWORD result = ExpandEnvironmentStrings(
            homepath,
            _expand_buffer1,
            MAX_PATH-1
        );

        if (result != 0)
        {
            homepath = _expand_buffer1;
        }
#else
        char* h = getenv("HOME");
        if (h != NULL)
        {
            snprintf(_expand_buffer, PATH_MAX, "%s/%s", h, homepath+1);
            homepath = _expand_buffer;
        }
#endif
        FILE *fp = fopen(homepath,"r");
        if( fp ) {
            env = homepath;
            fclose(fp);
        }
    }

    //And then system-wide.
    if(env == NULL)
    {
#if _WIN32
        DWORD result = ExpandEnvironmentStrings(
            syspath1,
            _expand_buffer2,
            MAX_PATH-1
        );

        if(result != 0)
        {
            syspath1 = _expand_buffer2;
        }
#endif
        FILE *fp = fopen(syspath1,"r");
        if(fp)
        {
            env = syspath1;
            fclose(fp);
        }
    }

    //And then system-wide.
    if(env == NULL)
    {
#if _WIN32
        DWORD result = ExpandEnvironmentStrings(
            syspath2,
            _expand_buffer3,
            MAX_PATH-1
        );

        if(result != 0)
        {
            syspath2 = _expand_buffer3;
        }
#endif
        FILE *fp = fopen(syspath2,"r");
        if(fp)
        {
            env = syspath2;
            fclose(fp);
        }
    }
    // We could not find the configuration file anywhere
    if(env == NULL)
    {
        fprintf(stderr, "Error: Bohrium could not find the config file.\n"
            " The search is:\n"
            "\t* The environment variable BH_CONFIG.\n"
            "\t* The home directory \"%s\".\n"
            "\t* The local directory \"%s\".\n"
            "\t* And system-wide \"%s\".\n", homepath, syspath1, syspath2);
        return BH_ERROR;
    }

    // Load the bohrium configuration file
    self->config = iniparser_load(env);
    if(self->config == NULL)
    {
        fprintf(stderr, "Error: Bohrium could not read the config file.\n");
        return BH_ERROR;
    }
    return BH_SUCCESS;
}

/* Initilize the component object
 *
 * @self   The component object to initilize
 * @name   The name of the component. If NULL "bridge" will be used.
 * @return Error codes (BH_SUCCESS, BH_ERROR)
 */
bh_error bh_component_init(bh_component *self, const char* name)
{
    memset(self, 0, sizeof(bh_component));  // Clear component-memory
                                            // Find configuration-file
    bh_error found_config = bh_component_config_find(self);
    if (BH_SUCCESS != found_config) {
        return found_config;
    }

    char* stack = getenv("BH_STACK");       // Get the stack from environment
    int default_stack = 0;
    if (NULL == stack) {                    // Default to "stack_default"
        stack = (char*)"stack_default";
        default_stack = 1;
    }
    
    int stack_exists = component_exists(self->config, stack);
    if ((!default_stack) && (!stack_exists)) {
        fprintf(stderr, "The requested stack configuration(%s) does not exist,"
                        " falling back to children-chaining.\n", stack);
    }
    if (!stack_exists) {
        stack = NULL;
    }

    if (name == NULL) {                                 // Assign name
        if (stack) {
            strcpy(self->name, stack);
        } else {
            strcpy(self->name, "bridge");
        }
    } else {
        strcpy(self->name, name);
    }
    
    self->type = get_type(self->config, self->name);    // Assign type
    if (BH_COMPONENT_ERROR == self->type) {
        return BH_ERROR;
    }

    return component_children_init(self, stack);   // Initialize children
}

/* Destroyes the component object.
 *
 * @self   The component object to destroy
 */
void bh_component_destroy(bh_component *self)
{
    iniparser_freedict(self->config);
}

/* Retrieves an extension method implementation.
 *
 * @self      The component object.
 * @name      Name of the extension method e.g. matmul
 * @extmethod Pointer to the method (output)
 * @return    Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY,
 *                         BH_EXTMETHOD_NOT_SUPPORTED)
 */
bh_error bh_component_extmethod(const bh_component *self,
                                const char *name,
                                bh_extmethod_impl *extmethod)
{
    //We search the libs in the config file to find the user-defined function.
    char *lib_paths = bh_component_config_lookup(self,"libs");
    if(lib_paths != NULL)
    {
        //Lets make a working copy
        lib_paths = strdup(lib_paths);
        if(lib_paths == NULL)
            return BH_OUT_OF_MEMORY;

        //Handle one library path at a time.
        char *path = strtok(lib_paths,",");
        while(path != NULL)
        {
            void *lib_handle = dlopen(path, RTLD_NOW);
            if(lib_handle != NULL)
            {
                char tname[BH_COMPONENT_NAME_SIZE];
                sprintf(tname, "bh_%s", name);
                dlerror();//Clear old errors.
                *extmethod = (bh_extmethod_impl)dlsym(lib_handle, tname);
                if(dlerror() == NULL)//No errors, we found the function
                {
                    free(lib_paths);
                    return BH_SUCCESS;
                }
            }
            path = strtok(NULL,",");
        }
        free(lib_paths);
    }
    *extmethod = NULL;//Make sure it is NULL on error.
    return BH_EXTMETHOD_NOT_SUPPORTED;
}

/* Look up a key in the environment
 * Private function
 *
 * @component The component.
 * @key       The key to lookup in the config file
 * @return    The value if found, otherwise NULL
 */
static char *lookup_env(const bh_component *component, const char* key)
{
    using namespace std;
    stringstream ss;
    ss << "BH_" << component->name << "_" << key;
    string s = boost::to_upper_copy(ss.str());
    return getenv(s.c_str());
}

/* Look up a key in the config file
 *
 * @component The component.
 * @key       The key to lookup in the config file
 * @return    The value if found, otherwise NULL
 */
char* bh_component_config_lookup(const bh_component *component, const char* key)
{
    char *env = lookup_env(component, key);
    if(env != NULL)
        return env;

    char dictkey[BH_COMPONENT_NAME_SIZE];
    snprintf(dictkey, BH_COMPONENT_NAME_SIZE, "%s:%s", component->name, key);
    return iniparser_getstring(component->config, dictkey, NULL);
}

/*
 * @brief     Lookup a keys value in the config fil converted to a bool
 * @component The component.
 * @key       The key to lookup in the config file
 * @notfound  Value to return in case of error
 * @return    int(bool)
*/
int bh_component_config_lookup_bool(const bh_component *component,
                                    const char* key, int notfound)
{
    char dictkey[BH_COMPONENT_NAME_SIZE];
    snprintf(dictkey, BH_COMPONENT_NAME_SIZE, "%s:%s", component->name, key);
    return iniparser_getboolean(component->config, dictkey, notfound);
}

/*
 * @brief     Lookup a keys value in the config fil converted to an int
 * @component The component.
 * @key       The key to lookup in the config file
 * @notfound  Value to return in case of error
 * @return    int
*/
int bh_component_config_lookup_int(const bh_component *component,
                                   const char* key, int notfound)
{
    char dictkey[BH_COMPONENT_NAME_SIZE];
    snprintf(dictkey, BH_COMPONENT_NAME_SIZE, "%s:%s", component->name, key);
    return iniparser_getint(component->config, dictkey, notfound);
}

/*
 * @brief     Lookup a keys value in the config fil converted to a double
 * @component The component.
 * @key       The key to lookup in the config file
 * @notfound  Value to return in case of error
 * @return    double
*/
double bh_component_config_lookup_double(const bh_component *component,
                                         const char* key, double notfound)
{
    char dictkey[BH_COMPONENT_NAME_SIZE];
    snprintf(dictkey, BH_COMPONENT_NAME_SIZE, "%s:%s", component->name, key);
    return iniparser_getdouble(component->config, dictkey, notfound);
}

bh_error bh_component_config_int_option(const bh_component* component,
                                        const char* option_name,
                                        int min,
                                        int max,
                                        bh_intp* option)
{
    char* raw = bh_component_config_lookup(component, option_name);
    if (!raw) {
        fprintf(stderr, "parameter(%s) is missing.\n", option_name);
        return BH_ERROR;
    }
    *option = (bh_intp)atoi(raw);
    if ((*option < min) || (*option > max)) {
        fprintf(
            stderr,
            "%s should be within range [%d,%d].\n",
            option_name, min, max
        );
        return BH_ERROR;
    }
    return BH_SUCCESS;
}

bh_error bh_component_config_string_option(const bh_component* component,
                                           const char* option_name,
                                           char** option)
{
    *option = bh_component_config_lookup(component, option_name);
    if (!option) {
        fprintf(stderr, "%s is missing.\n", option_name);
        return BH_ERROR;
    }
    return BH_SUCCESS;
}

bh_error bh_component_config_path_option(const bh_component* component,
                                         const char* option_name,
                                         char** option)
{
    *option = bh_component_config_lookup(component, option_name);

    if (!option) {
        fprintf(stderr, "Path is not set; option (%s).\n", option_name);
        return BH_ERROR;
    }
    if (0 != access(*option, F_OK)) {
        if (ENOENT == errno) {
            fprintf(stderr, "Path does not exist; path (%s).\n", *option);
        } else if (ENOTDIR == errno) {
            fprintf(stderr, "Path is not a directory; path (%s).\n", *option);
        } else {
            fprintf(stderr, "Path is broken somehow; path (%s).\n", *option);
        }
        return BH_ERROR;
    }
    return BH_SUCCESS;
}

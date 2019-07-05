#include <dlfcn.h>
#include "ngx_core_run_module.h"


static void* ngx_run_create_conf(ngx_cycle_t* cycle);
static char* ngx_run_conf_lib(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static char* ngx_run_conf_fn(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static ngx_int_t ngx_http_run_init_process(ngx_cycle_t* cycle);
static void ngx_http_run_exit_process(ngx_cycle_t* cycle);

ngx_int_t run_get_variable(ngx_http_request_t* r, ngx_http_variable_value_t* v, uintptr_t data);
static ngx_int_t execute_fn(ngx_cycle_t* cycle, void* entrypoint, run_fn_conf_t* fn);

typedef void(*run_func_ptr)(uint32_t input_count, void** input, uint32_t* input_size);

static ngx_core_module_t ngx_core_run_module_ctx = {
	ngx_string("run"),
	&ngx_run_create_conf,
	NULL
};

static ngx_command_t ngx_run_commands[] = {

	{ ngx_string("run_lib"),
	  NGX_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_TAKE1,
	  ngx_run_conf_lib,
	  0,
	  offsetof(ngx_run_conf_t, lib),
	  NULL},
	{ ngx_string("run_init"),
	  NGX_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
	  ngx_run_conf_fn,
	  0,
	  offsetof(ngx_run_conf_t, init),
	  NULL},
	{ ngx_string("run_exit"),
	  NGX_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_1MORE,
	  ngx_run_conf_fn,
	  0,
	  offsetof(ngx_run_conf_t, dispose),
	  NULL},
	  ngx_null_command
};

ngx_module_t ngx_core_run_module = {
	NGX_MODULE_V1,
	&ngx_core_run_module_ctx, /* module context */
	ngx_run_commands, /* module directives */
	NGX_CORE_MODULE, /* module type */
	NULL, /* init master */
	NULL, /* init module */
	&ngx_http_run_init_process, /* init process */
	NULL, /* init thread */
	NULL, /* exit thread */
	&ngx_http_run_exit_process, /* exit process */
	NULL, /* exit master */
	NGX_MODULE_V1_PADDING
};

static void* ngx_run_create_conf(ngx_cycle_t* cycle)
{
	ngx_run_conf_t* cf;

	cf = ngx_pcalloc(cycle->pool, sizeof(ngx_run_conf_t));
	if (cf == NULL) {
		return NULL;
	}

	ngx_memzero(cf, sizeof(ngx_run_conf_t));

	return cf;
}

static char* ngx_run_conf_lib(ngx_conf_t* cf, ngx_command_t* cmd, void* conf) {
	ngx_conf_set_str_slot(cf, cmd, conf);
	ngx_run_conf_t* c = conf;

	if (c->lib.data == NULL || c->lib.len == 0)
		return NGX_CONF_ERROR;
	
	char tmp[2049];
	ngx_memcpy(tmp, c->lib.data, c->lib.len);
	tmp[c->lib.len] = 0;
	void* entrypoint = ngx_dlopen(tmp);
	if (entrypoint == 0) {
		ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
			ngx_dlopen_n " \"%s\" failed (%s)", tmp, ngx_dlerror());
		return NGX_CONF_ERROR;
	}
	c->entrypoint = entrypoint;

	return NGX_CONF_OK;
}

static char* ngx_run_conf_fn(ngx_conf_t* cf, ngx_command_t* cmd, void* conf)
{
	run_fn_conf_t* c = (run_fn_conf_t*)((char*)conf + cmd->offset);

	ngx_str_t* values;
	values = cf->args->elts;

	c->name.data = ngx_pstrdup(cf->cycle->pool, &values[1]);
	if (c->name.data == NULL)
		return NGX_CONF_ERROR;
	c->name.len = values[1].len;

	if (cf->args->nelts == 2)
		return NGX_CONF_OK;

	values+=2;

	c->args_cnt = cf->args->nelts - 2;
	size_t i;
	for (i = 0; i < c->args_cnt; ++i) {
		c->args[i].data = ngx_pstrdup(cf->cycle->pool, &values[i]);
		if (c->args[i].data == NULL)
			return NGX_CONF_ERROR;
		c->args[i].len = values[i].len;
	}

	return NGX_CONF_OK;
}

static ngx_int_t ngx_http_run_init_process(ngx_cycle_t* cycle)
{
	ngx_run_conf_t* c = (ngx_run_conf_t*)
		ngx_get_conf(cycle->conf_ctx, ngx_core_run_module);

	if (c == NULL)
		return NGX_ERROR;

	return c->init.name.len > 0 ? execute_fn(cycle, c->entrypoint, &c->init) : NGX_OK;
}

static void ngx_http_run_exit_process(ngx_cycle_t* cycle)
{
	ngx_run_conf_t* c = (ngx_run_conf_t*)
		ngx_get_conf(cycle->conf_ctx, ngx_core_run_module);

	if (c == NULL)
		return;

	if (c->dispose.name.len > 0)
		execute_fn(cycle, c->entrypoint, &c->dispose);

	if (ngx_dlclose(c->entrypoint) != 0) {
		ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
			ngx_dlclose_n " failed (%s)", ngx_dlerror());
	}
}

static ngx_int_t execute_fn(ngx_cycle_t* cycle, void* entrypoint, run_fn_conf_t* fn)
{
	char tmp[2049];
	ngx_memcpy(tmp, fn->name.data, fn->name.len);
	tmp[fn->name.len] = 0;
	run_func_ptr run = ngx_dlsym(entrypoint, tmp);
	if (run == 0) {
		ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
			ngx_dlsym_n " \"%s\", \"%s\" failed (%s)",
			tmp, "ngx_modules", ngx_dlerror());
		return NGX_ERROR;
	}

	void* inputParams[NGX_CONF_MAX_ARGS];
	uint32_t inputSizes[NGX_CONF_MAX_ARGS];
	ngx_uint_t i;
	for (i = 0; i < fn->args_cnt; ++i) {
		inputParams[i] = fn->args[i].data;
		inputSizes[i] = fn->args[i].len;
	}

	run(fn->args_cnt, inputParams, inputSizes);

	return NGX_OK;
}